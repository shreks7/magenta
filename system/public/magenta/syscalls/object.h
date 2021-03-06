// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

__BEGIN_CDECLS

// ask clang format not to mess up the indentation:
// clang-format off

// Valid topics for mx_object_get_info.
typedef enum {
    MX_INFO_NONE                       = 0,
    MX_INFO_HANDLE_VALID               = 1,
    MX_INFO_HANDLE_BASIC               = 2,  // mx_info_handle_basic_t[1]
    MX_INFO_PROCESS                    = 3,  // mx_info_process_t[1]
    MX_INFO_PROCESS_THREADS            = 4,  // mx_koid_t[n]
    MX_INFO_VMAR                       = 7,  // mx_info_vmar_t[1]
    MX_INFO_JOB_CHILDREN               = 8,  // mx_koid_t[n]
    MX_INFO_JOB_PROCESSES              = 9,  // mx_koid_t[n]
    MX_INFO_THREAD                     = 10, // mx_info_thread_t[1]
    MX_INFO_THREAD_EXCEPTION_REPORT    = 11, // mx_exception_report_t[1]
    MX_INFO_TASK_STATS                 = 12, // mx_info_task_stats_t[1]
    MX_INFO_PROCESS_MAPS               = 13, // mx_info_maps_t[n]
    MX_INFO_PROCESS_VMOS               = 14, // mx_info_vmo_t[n]
    MX_INFO_THREAD_STATS               = 15, // mx_info_thread_stats_t[1]
    MX_INFO_CPU_STATS                  = 16, // mx_info_cpu_stats_t[n]
    MX_INFO_KMEM_STATS                 = 17, // mx_info_kmem_stats_t[1]
    MX_INFO_RESOURCE                   = 18, // mx_info_resource_t[1]
    MX_INFO_LAST
} mx_object_info_topic_t;

typedef enum {
    MX_OBJ_TYPE_NONE                = 0,
    MX_OBJ_TYPE_PROCESS             = 1,
    MX_OBJ_TYPE_THREAD              = 2,
    MX_OBJ_TYPE_VMO                 = 3,
    MX_OBJ_TYPE_CHANNEL             = 4,
    MX_OBJ_TYPE_EVENT               = 5,
    MX_OBJ_TYPE_PORT                = 6,
    MX_OBJ_TYPE_INTERRUPT           = 9,
    MX_OBJ_TYPE_IOMAP               = 10,
    MX_OBJ_TYPE_PCI_DEVICE          = 11,
    MX_OBJ_TYPE_LOG                 = 12,
    MX_OBJ_TYPE_SOCKET              = 14,
    MX_OBJ_TYPE_RESOURCE            = 15,
    MX_OBJ_TYPE_EVENT_PAIR          = 16,
    MX_OBJ_TYPE_JOB                 = 17,
    MX_OBJ_TYPE_VMAR                = 18,
    MX_OBJ_TYPE_FIFO                = 19,
    MX_OBJ_TYPE_GUEST               = 20,
    MX_OBJ_TYPE_VCPU                = 21,
    MX_OBJ_TYPE_TIMER               = 22,
    MX_OBJ_TYPE_LAST
} mx_obj_type_t;

typedef enum {
    MX_OBJ_PROP_NONE            = 0,
    MX_OBJ_PROP_WAITABLE        = 1,
} mx_obj_props_t;

typedef struct mx_info_handle_basic {
    // The unique id assigned by kernel to the object referenced by the
    // handle.
    mx_koid_t koid;

    // The immutable rights assigned to the handle. Two handles that
    // have the same koid and the same rights are equivalent and
    // interchangeable.
    mx_rights_t rights;

    // The object type: channel, event, socket, etc.
    uint32_t type;                // mx_obj_type_t;

    // The koid of the logical counterpart or parent object of the
    // object referenced by the handle. Otherwise this value is zero.
    mx_koid_t related_koid;

    // Set to MX_OBJ_PROP_WAITABLE if the object referenced by the
    // handle can be waited on; zero otherwise.
    uint32_t props;               // mx_obj_props_t;
} mx_info_handle_basic_t;

typedef struct mx_info_process {
    // The process's return code; only valid if |exited| is true.
    // Guaranteed to be non-zero if the process was killed by |mx_task_kill|.
    int return_code;

    // True if the process has ever left the initial creation state,
    // even if it has exited as well.
    bool started;

    // If true, the process has exited and |return_code| is valid.
    bool exited;

    // True if a debugger is attached to the process.
    bool debugger_attached;
} mx_info_process_t;

typedef struct mx_info_thread {
    // One of MX_THREAD_STATE_* values.
    uint32_t state;

    // If nonzero, the thread has gotten an exception and is waiting for
    // the exception to be handled by the specified port.
    // The value is one of MX_EXCEPTION_PORT_TYPE_*.
    uint32_t wait_exception_port_type;
} mx_info_thread_t;

typedef struct mx_info_thread_stats {
    // Total accumulated running time of the thread.
    mx_time_t total_runtime;
} mx_info_thread_stats_t;

// Statistics about resources (e.g., memory) used by a task. Can be relatively
// expensive to gather.
typedef struct mx_info_task_stats {
    // The total size of mapped memory ranges in the task.
    // Not all will be backed by physical memory.
    size_t mem_mapped_bytes;

    // For the fields below, a byte is considered committed if it's backed by
    // physical memory. Some of the memory may be double-mapped, and thus
    // double-counted.

    // Committed memory that is only mapped into this task.
    size_t mem_private_bytes;

    // Committed memory that is mapped into this and at least one other task.
    size_t mem_shared_bytes;

    // A number that estimates the fraction of mem_shared_bytes that this
    // task is responsible for keeping alive.
    //
    // An estimate of:
    //   For each shared, committed byte:
    //   mem_scaled_shared_bytes += 1 / (number of tasks mapping this byte)
    //
    // This number is strictly smaller than mem_shared_bytes.
    size_t mem_scaled_shared_bytes;
} mx_info_task_stats_t;

typedef struct mx_info_vmar {
    // Base address of the region.
    uintptr_t base;

    // Length of the region, in bytes.
    size_t len;
} mx_info_vmar_t;


// Types and values used by MX_INFO_PROCESS_MAPS.

// Describes a VM mapping.
typedef struct mx_info_maps_mapping {
    // MMU flags for the mapping.
    // Bitwise OR of MX_VM_FLAG_PERM_{READ,WRITE,EXECUTE} values.
    uint32_t mmu_flags;
    // koid of the mapped VMO.
    mx_koid_t vmo_koid;
    // The number of PAGE_SIZE pages in the mapped region of the VMO
    // that are backed by physical memory.
    size_t committed_pages;
} mx_info_maps_mapping_t;

// Types of entries represented by mx_info_maps_t.
// Can't use mx_obj_type_t because not all of these are
// user-visible kernel object types.
typedef enum mx_info_maps_type {
    MX_INFO_MAPS_TYPE_NONE    = 0,
    MX_INFO_MAPS_TYPE_ASPACE  = 1,
    MX_INFO_MAPS_TYPE_VMAR    = 2,
    MX_INFO_MAPS_TYPE_MAPPING = 3,
    MX_INFO_MAPS_TYPE_LAST
} mx_info_maps_type_t;

// Describes a node in the aspace/vmar/mapping hierarchy for a user process.
typedef struct mx_info_maps {
    // Name if available; empty string otherwise.
    char name[MX_MAX_NAME_LEN];
    // Base address.
    mx_vaddr_t base;
    // Size in bytes.
    size_t size;

    // The depth of this node in the tree.
    // Can be used for indentation, or to rebuild the tree from an array
    // of mx_info_maps_t entries, which will be in depth-first pre-order.
    size_t depth;
    // The type of this entry; indicates which union entry is valid.
    uint32_t type; // mx_info_maps_type_t
    union {
        mx_info_maps_mapping_t mapping;
        // No additional fields for other types.
    } u;
} mx_info_maps_t;


// Values and types used by MX_INFO_PROCESS_VMOS.

// The VMO is backed by RAM, consuming memory.
// Mutually exclusive with MX_INFO_VMO_TYPE_PHYSICAL.
// See MX_INFO_VMO_TYPE(flags)
#define MX_INFO_VMO_TYPE_PAGED              (1u<<0)

// The VMO points to a physical address range, and does not consume memory.
// Typically used to access memory-mapped hardware.
// Mutually exclusive with MX_INFO_VMO_TYPE_PAGED.
// See MX_INFO_VMO_TYPE(flags)
#define MX_INFO_VMO_TYPE_PHYSICAL           (0u<<0)

// Returns a VMO's type based on its flags, allowing for checks like
// if (MX_INFO_VMO_TYPE(f) == MX_INFO_VMO_TYPE_PAGED)
#define MX_INFO_VMO_TYPE(flags)             ((flags) & (1u<<0))

// The VMO is a clone, and is a copy-on-write clone.
#define MX_INFO_VMO_IS_COW_CLONE            (1u<<2)

// When reading a list of VMOs pointed to by a process, indicates that the
// process has a handle to the VMO, which isn't necessarily mapped.
#define MX_INFO_VMO_VIA_HANDLE              (1u<<3)

// When reading a list of VMOs pointed to by a process, indicates that the
// process maps the VMO into a VMAR, but doesn't necessarily have a handle to
// the VMO.
#define MX_INFO_VMO_VIA_MAPPING             (1u<<4)

// Describes a VMO. For mapping information, see |mx_info_maps_t|.
typedef struct mx_info_vmo {
    // The koid of this VMO.
    mx_koid_t koid;

    // The name of this VMO.
    char name[MX_MAX_NAME_LEN];

    // The size of this VMO; i.e., the amount of virtual address space it
    // would consume if mapped.
    uint64_t size_bytes;

    // If this VMO is a clone, the koid of its parent. Otherwise, zero.
    // See |flags| for the type of clone.
    mx_koid_t parent_koid;

    // The number of clones of this VMO, if any.
    size_t num_children;

    // The number of times this VMO is currently mapped into VMARs.
    // Note that the same process will often map the same VMO twice,
    // and both mappings will be counted here. (I.e., this is not a count
    // of the number of processes that map this VMO; see share_count.)
    size_t num_mappings;

    // An estimate of the number of unique address spaces that
    // this VMO is mapped into. Every process has its own address space,
    // and so does the kernel.
    size_t share_count;

    // Bitwise OR of MX_INFO_VMO_* values.
    uint32_t flags;

    // If |MX_INFO_VMO_TYPE(flags) == MX_INFO_VMO_TYPE_PAGED|, the amount of
    // memory currently allocated to this VMO; i.e., the amount of physical
    // memory it consumes. Undefined otherwise.
    uint64_t committed_bytes;

    // If |flags & MX_INFO_VMO_VIA_HANDLE|, the handle rights.
    // Undefined otherwise.
    mx_rights_t handle_rights;
} mx_info_vmo_t;

// kernel statistics per cpu
typedef struct mx_info_cpu_stats {
    uint32_t cpu_number;
    uint32_t flags;

    mx_time_t idle_time;

    // kernel scheduler counters
    uint64_t reschedules;
    uint64_t context_switches;
    uint64_t irq_preempts;
    uint64_t preempts;
    uint64_t yields;

    // cpu level interrupts and exceptions
    uint64_t ints;          // hardware interrupts, minus timer interrupts or inter-processor interrupts
    uint64_t timer_ints;    // timer interrupts
    uint64_t timers;        // timer callbacks
    uint64_t page_faults;   // page faults
    uint64_t exceptions;    // exceptions such as undefined opcode
    uint64_t syscalls;

    // inter-processor interrupts
    uint64_t reschedule_ipis;
    uint64_t generic_ipis;
} mx_info_cpu_stats_t;

// Information about kernel memory usage.
// Can be expensive to gather.
typedef struct mx_info_kmem_stats {
    // The total amount of physical memory available to the system.
    uint64_t total_bytes;

    // The amount of unallocated memory.
    uint64_t free_bytes;

    // The amount of memory reserved by and mapped into the kernel for reasons
    // not covered by other fields in this struct. Typically for readonly data
    // like the ram disk and kernel image, and for early-boot dynamic memory.
    uint64_t wired_bytes;

    // The amount of memory allocated to the kernel heap.
    uint64_t total_heap_bytes;

    // The portion of |total_heap_bytes| that is not in use.
    uint64_t free_heap_bytes;

    // The amount of memory committed to VMOs, both kernel and user.
    // A superset of all userspace memory.
    // Does not include certain VMOs that fall under |wired_bytes|.
    //
    // TODO(dbort): Break this into at least two pieces: userspace VMOs that
    // have koids, and kernel VMOs that don't. Or maybe look at VMOs
    // mapped into the kernel aspace vs. everything else.
    uint64_t vmo_bytes;

    // The amount of memory used for architecture-specific MMU metadata
    // like page tables.
    uint64_t mmu_overhead_bytes;

    // Non-free memory that isn't accounted for in any other field.
    uint64_t other_bytes;
} mx_info_kmem_stats_t;

typedef struct mx_info_resource {
    // The resource kind, one of:
    // {MX_RSRC_KIND_ROOT, MX_RSRC_KIND_MMIO, MX_RSRC_KIND_IOPORT, MX_RSRC_KIND_IRQ}
    uint32_t kind;
    // Resource's low value (inclusive)
    uint64_t low;
    // Resource's high value (inclusive)
    uint64_t high;
} mx_info_resource_t;

#define MX_INFO_CPU_STATS_FLAG_ONLINE       (1u<<0)

// Object properties.

// Argument is a uint32_t.
#define MX_PROP_NUM_STATE_KINDS             2u
// Argument is a char[MX_MAX_NAME_LEN].
#define MX_PROP_NAME                        3u

#if __x86_64__
// Argument is a uintptr_t.
#define MX_PROP_REGISTER_FS                 4u
#endif

// Argument is the value of ld.so's _dl_debug_addr, a uintptr_t.
#define MX_PROP_PROCESS_DEBUG_ADDR          5u

// Argument is the base address of the vDSO mapping (or zero), a uintptr_t.
#define MX_PROP_PROCESS_VDSO_BASE_ADDRESS   6u

// Argument is an mx_job_importance_t value.
#define MX_PROP_JOB_IMPORTANCE             7u

// Describes how important a job is.
typedef int32_t mx_job_importance_t;

// Valid mx_job_importance_t values and range.
// The non-negative values must fit in 8 bits.

// A job with this importance will inherit its actual importance from
// the closest ancestor with a non-INHERITED importance property value.
#define MX_JOB_IMPORTANCE_INHERITED ((mx_job_importance_t)-1)

// The lowest importance. Jobs with this importance value are likely to be
// killed first in an out-of-memory situation.
#define MX_JOB_IMPORTANCE_MIN       ((mx_job_importance_t)0)

// The highest importance.
#define MX_JOB_IMPORTANCE_MAX       ((mx_job_importance_t)255)

// Values for mx_info_thread_t.state.
#define MX_THREAD_STATE_NEW                 0u
#define MX_THREAD_STATE_RUNNING             1u
#define MX_THREAD_STATE_SUSPENDED           2u
#define MX_THREAD_STATE_BLOCKED             3u
#define MX_THREAD_STATE_DYING               4u
#define MX_THREAD_STATE_DEAD                5u

__END_CDECLS
