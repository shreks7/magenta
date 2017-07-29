// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/magenta.h>

#include <pow2.h>
#include <trace.h>

#include <kernel/auto_lock.h>
#include <kernel/cmdline.h>
#include <kernel/mutex.h>

#include <lk/init.h>

#include <lib/console.h>
#include <lib/oom.h>

#include <magenta/diagnostics.h>
#include <magenta/dispatcher.h>
#include <magenta/excp_port.h>
#include <magenta/handle.h>
#include <magenta/job_dispatcher.h>
#include <magenta/policy_manager.h>
#include <magenta/process_dispatcher.h>
#include <magenta/resource_dispatcher.h>
#include <magenta/state_tracker.h>

// The next two includes should be removed. See DeleteHandle().
#include <magenta/io_mapping_dispatcher.h>

#include <mxtl/arena.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/type_support.h>

#include <platform.h>

#define LOCAL_TRACE 0

// The number of possible handles in the arena.
constexpr size_t kMaxHandleCount = 256 * 1024u;

// Warning level: high_handle_count() is called when
// there are this many outstanding handles.
constexpr size_t kHighHandleCount = (kMaxHandleCount * 7) / 8;

// The handle arena and its mutex. It also guards Dispatcher::handle_count_.
static Mutex handle_mutex;
static mxtl::Arena TA_GUARDED(handle_mutex) handle_arena;
static size_t outstanding_handles TA_GUARDED(handle_mutex) = 0u;

size_t internal::OutstandingHandles() {
    AutoLock lock(&handle_mutex);
    return outstanding_handles;
}

// The system exception port.
static mutex_t system_exception_mutex = MUTEX_INITIAL_VALUE(system_exception_mutex);
static mxtl::RefPtr<ExceptionPort> system_exception_port TA_GUARDED(system_exception_mutex);

// All jobs and processes are rooted at the |root_job|.
static mxtl::RefPtr<JobDispatcher> root_job;

// The singleton policy manager, for jobs and processes. This is
// a magenta internal class (not a dispatcher-derived).
static PolicyManager* policy_manager;

// Masks for building a Handle's base_value, which ProcessDispatcher
// uses to create mx_handle_t values.
//
// base_value bit fields:
//   [31..30]: Must be zero
//   [29..kHandleGenerationShift]: Generation number
//                                 Masked by kHandleGenerationMask
//   [kHandleGenerationShift-1..0]: Index into handle_arena
//                                  Masked by kHandleIndexMask
static constexpr uint32_t kHandleIndexMask = kMaxHandleCount - 1;
static_assert((kHandleIndexMask & kMaxHandleCount) == 0,
              "kMaxHandleCount must be a power of 2");
static constexpr uint32_t kHandleGenerationMask =
    ~kHandleIndexMask & ~(3 << 30);
static constexpr uint32_t kHandleGenerationShift =
    log2_uint_floor(kMaxHandleCount);
static_assert(((3 << (kHandleGenerationShift - 1)) & kHandleGenerationMask) ==
                  1 << kHandleGenerationShift,
              "Shift is wrong");
static_assert((kHandleGenerationMask >> kHandleGenerationShift) >= 255,
              "Not enough room for a useful generation count");
static_assert(((3 << 30) ^ kHandleGenerationMask ^ kHandleIndexMask) ==
                  0xffffffffu,
              "Masks do not agree");

// Returns a new |base_value| based on the value stored in the free
// |handle_arena| slot pointed to by |addr|. The new value will be different
// from the last |base_value| used by this slot.
static uint32_t GetNewHandleBaseValue(void* addr) TA_REQ(handle_mutex) {
    // Get the index of this slot within handle_arena.
    auto va = reinterpret_cast<Handle*>(addr) -
              reinterpret_cast<Handle*>(handle_arena.start());
    uint32_t handle_index = static_cast<uint32_t>(va);
    DEBUG_ASSERT((handle_index & ~kHandleIndexMask) == 0);

    // Check the free memory for a stashed base_value.
    uint32_t v = *reinterpret_cast<uint32_t*>(addr);
    uint32_t old_gen;
    if (v == 0) {
        // First time this slot has been allocated.
        old_gen = 0;
    } else {
        // This slot has been used before.
        DEBUG_ASSERT((v & kHandleIndexMask) == handle_index);
        old_gen = (v & kHandleGenerationMask) >> kHandleGenerationShift;
    }
    return (((old_gen + 1) << kHandleGenerationShift) & kHandleGenerationMask) | handle_index;
}

// Destroys, but does not free, the Handle, and fixes up its memory to protect
// against stale pointers to it. Also stashes the Handle's base_value for reuse
// the next time this slot is allocated.
void internal::TearDownHandle(Handle* handle) TA_EXCL(handle_mutex) {
    uint32_t base_value = handle->base_value();

    // Calling the handle dtor can cause many things to happen, so it is
    // important to call it outside the lock.
    handle->~Handle();

    // There may be stale pointers to this slot. Zero out most of its fields
    // to ensure that the Handle does not appear to belong to any process
    // or point to any Dispatcher.
    memset(handle, 0, sizeof(Handle));

    // Hold onto the base_value for the next user of this slot, stashing
    // it at the beginning of the free slot.
    *reinterpret_cast<uint32_t*>(handle) = base_value;

    // Double-check that the process_id field is zero, ensuring that
    // no process can refer to this slot while it's free. This isn't
    // completely legal since |handle| points to unconstructed memory,
    // but it should be safe enough for an assertion.
    DEBUG_ASSERT(handle->process_id_ == 0);
}

static void high_handle_count(size_t count) {
    // TODO: Avoid calling this for every handle after kHighHandleCount;
    // printfs are slow and |handle_mutex| is held by our caller.
    printf("WARNING: High handle count: %zu handles\n", count);
}

Handle* MakeHandle(mxtl::RefPtr<Dispatcher> dispatcher, mx_rights_t rights) {
    uint32_t* handle_count = nullptr;
    void* addr;
    uint32_t base_value;

    {
        AutoLock lock(&handle_mutex);
        addr = handle_arena.Alloc();
        if (addr == nullptr) {
            const auto oh = outstanding_handles;
            lock.release();
            printf("WARNING: Could not allocate new handle (%zu outstanding)\n",
                   oh);
            return nullptr;
        }
        if (++outstanding_handles > kHighHandleCount)
            high_handle_count(outstanding_handles);

        handle_count = dispatcher->get_handle_count_ptr();
        (*handle_count)++;
        if (*handle_count != 2u)
            handle_count = nullptr;

        base_value = GetNewHandleBaseValue(addr);
    }

    auto state_tracker = dispatcher->get_state_tracker();
    if (state_tracker != nullptr)
        state_tracker->UpdateLastHandleSignal(handle_count);

    return new (addr) Handle(mxtl::move(dispatcher), rights, base_value);
}

Handle* DupHandle(Handle* source, mx_rights_t rights, bool is_replace) {
    mxtl::RefPtr<Dispatcher> dispatcher(source->dispatcher());
    uint32_t* handle_count;
    void* addr;
    uint32_t base_value;

    {
        AutoLock lock(&handle_mutex);
        addr = handle_arena.Alloc();
        if (addr == nullptr) {
            const auto oh = outstanding_handles;
            lock.release();
            printf(
                "WARNING: Could not allocate duplicate handle (%zu outstanding)\n", oh);
            return nullptr;
        }
        if (++outstanding_handles > kHighHandleCount)
            high_handle_count(outstanding_handles);

        handle_count = dispatcher->get_handle_count_ptr();
        (*handle_count)++;
        if (*handle_count != 2u)
            handle_count = nullptr;

        base_value = GetNewHandleBaseValue(addr);
    }

    auto state_tracker = dispatcher->get_state_tracker();
    if (!is_replace && (state_tracker != nullptr))
        state_tracker->UpdateLastHandleSignal(handle_count);

    return new (addr) Handle(source, rights, base_value);
}

void DeleteHandle(Handle* handle) {
    mxtl::RefPtr<Dispatcher> dispatcher(handle->dispatcher());
    auto state_tracker = dispatcher->get_state_tracker();

    if (state_tracker) {
        state_tracker->Cancel(handle);
    } else {
        // This code is sad but necessary because certain dispatchers
        // have complicated Close() logic which cannot be untangled at
        // this time.
        switch (dispatcher->get_type()) {
        case MX_OBJ_TYPE_IOMAP: {
            // DownCastDispatcher moves the reference so we need a copy
            // because we use |dispatcher| after the cast.
            auto disp = dispatcher;
            auto iodisp = DownCastDispatcher<IoMappingDispatcher>(&disp);
            if (iodisp)
                iodisp->Close();
            break;
        }
        default:
            break;
            // This is fine. See for example the LogDispatcher.
        }
    }

    // Destroys, but does not free, the Handle, and fixes up its memory
    // to protect against stale pointers to it. Also stashes the Handle's
    // base_value for reuse the next time this slot is allocated.
    internal::TearDownHandle(handle);

    bool zero_handles = false;
    uint32_t* handle_count;
    {
        AutoLock lock(&handle_mutex);
        --outstanding_handles;

        handle_count = dispatcher->get_handle_count_ptr();
        (*handle_count)--;
        if (*handle_count == 0u)
            zero_handles = true;
        else if (*handle_count != 1u)
            handle_count = nullptr;

        handle_arena.Free(handle);
    }

    if (zero_handles) {
        dispatcher->on_zero_handles();
        return;
    }

    if (state_tracker)
        state_tracker->UpdateLastHandleSignal(handle_count);

    // If |dispatcher| is the last reference then the dispatcher object
    // gets destroyed here.
}

bool HandleInRange(void* addr) {
    AutoLock lock(&handle_mutex);
    return handle_arena.in_range(addr);
}

Handle* MapU32ToHandle(uint32_t value) TA_NO_THREAD_SAFETY_ANALYSIS {
    auto index = value & kHandleIndexMask;
    auto va = &reinterpret_cast<Handle*>(handle_arena.start())[index];
    if (!HandleInRange(va))
        return nullptr;
    Handle* handle = reinterpret_cast<Handle*>(va);
    return handle->base_value() == value ? handle : nullptr;
}

void internal::DumpHandleTableInfo() {
    AutoLock lock(&handle_mutex);
    handle_arena.Dump();
}

mx_status_t SetSystemExceptionPort(mxtl::RefPtr<ExceptionPort> eport) {
    DEBUG_ASSERT(eport->type() == ExceptionPort::Type::SYSTEM);

    AutoLock lock(&system_exception_mutex);
    if (system_exception_port)
        return MX_ERR_BAD_STATE; // TODO(dje): ?
    system_exception_port = eport;
    return MX_OK;
}

bool ResetSystemExceptionPort() {
    AutoLock lock(&system_exception_mutex);
    if (system_exception_port == nullptr) {
        // Attempted to unbind when no exception port is bound.
        return false;
    }
    system_exception_port->OnTargetUnbind();
    system_exception_port.reset();
    return true;
}

mxtl::RefPtr<ExceptionPort> GetSystemExceptionPort() {
    AutoLock lock(&system_exception_mutex);
    return system_exception_port;
}

mxtl::RefPtr<JobDispatcher> GetRootJobDispatcher() {
    return root_job;
}

PolicyManager* GetSystemPolicyManager() {
    return policy_manager;
}

bool magenta_rights_check(const Handle* handle, mx_rights_t desired) {
    auto actual = handle->rights();
    if ((actual & desired) == desired)
        return true;
    LTRACEF("rights check fail!! has 0x%x, needs 0x%x\n", actual, desired);
    return false;
}

mx_status_t magenta_sleep(mx_time_t deadline) {
    /* sleep with interruptable flag set */
    return thread_sleep_etc(deadline, true);
}

mx_status_t validate_resource(mx_handle_t handle, uint32_t kind) {
    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<ResourceDispatcher> resource;
    auto status = up->GetDispatcher(handle, &resource);
    if (status != MX_OK) {
        return status;
    }
    uint32_t rkind = resource->get_kind();
    if ((rkind == MX_RSRC_KIND_ROOT) || (rkind == kind)) {
        return MX_OK;
    }
    return MX_ERR_ACCESS_DENIED;
}

mx_status_t validate_ranged_resource(mx_handle_t handle, uint32_t kind, uint64_t low,
                                     uint64_t high) {
    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<ResourceDispatcher> resource;
    auto status = up->GetDispatcher(handle, &resource);
    if (status != MX_OK) {
        return status;
    }
    uint32_t rsrc_kind = resource->get_kind();
    if (rsrc_kind == MX_RSRC_KIND_ROOT) {
        // root resource is valid for everything
        return MX_OK;
    } else if (rsrc_kind == kind) {
        uint64_t rsrc_low, rsrc_high;
        resource->get_range(&rsrc_low, &rsrc_high);
        if (low >= rsrc_low && high <= rsrc_high) {
            return MX_OK;
        }
    }

    return MX_ERR_ACCESS_DENIED;
}

mx_status_t get_process(ProcessDispatcher* up,
                        mx_handle_t proc_handle,
                        mxtl::RefPtr<ProcessDispatcher>* proc) {
    return up->GetDispatcherWithRights(proc_handle, MX_RIGHT_WRITE, proc);
}

// Counts and optionally prints all job/process descendants of a job.
namespace {
class OmmJobEnumerator final : public JobEnumerator {
public:
    // If |prefix| is non-null, also prints each job/process.
    OmmJobEnumerator(const char* prefix)
        : prefix_(prefix) { reset_counts(); }
    void reset_counts() {
        num_jobs_ = 0;
        num_processes_ = 0;
        num_running_processes_ = 0;
    }
    size_t num_jobs() const { return num_jobs_; }
    size_t num_processes() const { return num_processes_; }
    size_t num_running_processes() const { return num_running_processes_; }

private:
    bool OnJob(JobDispatcher* job) final {
        if (prefix_ != nullptr) {
            char name[MX_MAX_NAME_LEN];
            job->get_name(name);
            printf("%sjob %6" PRIu64 " '%s'\n", prefix_, job->get_koid(), name);
        }
        num_jobs_++;
        return true;
    }
    bool OnProcess(ProcessDispatcher* process) final {
        // Since we want to free memory by actually killing something, only
        // count running processes that aren't attached to a debugger.
        // It's a race, but will stop us from re-killing a job that only has
        // blocked-by-debugger processes.
        mx_info_process_t info = {};
        process->GetInfo(&info);
        if (info.started && !info.exited && !info.debugger_attached) {
            num_running_processes_++;
        }
        if (prefix_ != nullptr) {
            const char* tag = "new";
            if (info.started) {
                tag = "run";
            }
            if (info.exited) {
                tag = "dead";
            }
            if (info.debugger_attached) {
                tag = "dbg";
            }
            char name[MX_MAX_NAME_LEN];
            process->get_name(name);
            printf("%sproc %5" PRIu64 " %4s '%s'\n",
                   prefix_, process->get_koid(), tag, name);
        }
        num_processes_++;
        return true;
    }

    const char* prefix_;
    size_t num_jobs_;
    size_t num_processes_;
    size_t num_running_processes_;
};
} // namespace

// Called from a dedicated kernel thread when the system is low on memory.
static void oom_lowmem(size_t shortfall_bytes) {
    printf("OOM: oom_lowmem(shortfall_bytes=%zu) called\n", shortfall_bytes);
    printf("OOM: Process mapped committed bytes:\n");
    DumpProcessMemoryUsage("OOM:   ", /*min_pages=*/8 * MB / PAGE_SIZE);
    printf("OOM: Finding a job to kill...\n");

    OmmJobEnumerator job_counter(nullptr);
    OmmJobEnumerator job_printer("OOM:        + ");

    bool killed = false;
    int next = 3; // Used to print a few "up next" jobs.
    JobDispatcher::ForEachJobByImportance([&](JobDispatcher* job) {
        // TODO(dbort): Consider adding an "immortal" bit on jobs and skip them
        // here if they (and and all of their ancestors) have it set.
        bool kill = false;
        if (!killed) {
            // We want to kill a job that will actually free memory by dying, so
            // look for one with running process descendants (i.e., started,
            // non-exited, not blocked in a debugger).
            job_counter.reset_counts();
            job->EnumerateChildren(&job_counter, /*recurse=*/true);
            kill = job_counter.num_running_processes() > 0;
        }

        const char* tag;
        if (kill) {
            tag = "*KILL*";
        } else if (!killed) {
            tag = "(skip)";
        } else {
            tag = "(next)";
        }
        char name[MX_MAX_NAME_LEN];
        job->get_name(name);
        printf("OOM:   %s job %6" PRIu64 " '%s'\n", tag, job->get_koid(), name);
        if (kill) {
            // Print the descendants of the job we're about to kill.
            job_printer.reset_counts();
            job->EnumerateChildren(&job_printer, /*recurse=*/true);
            printf("OOM:        = %zu running procs (%zu total), %zu jobs\n",
                   job_printer.num_running_processes(),
                   job_printer.num_processes(), job_printer.num_jobs());
            // TODO(dbort): Join on dying processes/jobs to make sure we've
            // freed memory before returning control to the OOM thread?
            // TODO(MG-961): 'kill -9' these processes (which will require new
            // ProcessDispatcher features) so we can reclaim the memory of
            // processes that are stuck in a debugger or in the crashlogger.
            job->Kill();
            killed = true;
        } else if (killed) {
            if (--next == 0) {
                return MX_ERR_STOP;
            }
        }
        return MX_OK;
    });
}

void magenta_init(uint level) TA_NO_THREAD_SAFETY_ANALYSIS {
    handle_arena.Init("handles", sizeof(Handle), kMaxHandleCount);
    root_job = JobDispatcher::CreateRootJob();
    policy_manager = PolicyManager::Create();
    // Be sure to update kernel_cmdline.md if any of these defaults change.
    oom_init(cmdline_get_bool("kernel.oom.enable", true),
             LK_SEC(cmdline_get_uint64("kernel.oom.sleep-sec", 1)),
             cmdline_get_uint64("kernel.oom.redline-mb", 50) * MB,
             oom_lowmem);
}

LK_INIT_HOOK(magenta, magenta_init, LK_INIT_LEVEL_THREADING);
