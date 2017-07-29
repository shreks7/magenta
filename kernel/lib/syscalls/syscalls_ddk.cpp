// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <dev/interrupt.h>
#include <dev/iommu.h>
#include <dev/udisplay.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_object_paged.h>
#include <kernel/vm/vm_object_physical.h>
#include <lib/user_copy.h>
#include <lib/user_copy/user_ptr.h>

#if ARCH_X86
#include <platform/pc/bootloader.h>
#endif

#include <magenta/bus_transaction_initiator_dispatcher.h>
#include <magenta/handle_owner.h>
#include <magenta/interrupt_dispatcher.h>
#include <magenta/interrupt_event_dispatcher.h>
#include <magenta/io_mapping_dispatcher.h>
#include <magenta/iommu_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/syscalls/iommu.h>
#include <magenta/syscalls/pci.h>
#include <magenta/user_copy.h>
#include <magenta/vm_object_dispatcher.h>
#include <mxalloc/new.h>
#include <mxtl/auto_call.h>
#include <mxtl/inline_array.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

static_assert(MX_CACHE_POLICY_CACHED == ARCH_MMU_FLAG_CACHED,
              "Cache policy constant mismatch - CACHED");
static_assert(MX_CACHE_POLICY_UNCACHED == ARCH_MMU_FLAG_UNCACHED,
              "Cache policy constant mismatch - UNCACHED");
static_assert(MX_CACHE_POLICY_UNCACHED_DEVICE == ARCH_MMU_FLAG_UNCACHED_DEVICE,
              "Cache policy constant mismatch - UNCACHED_DEVICE");
static_assert(MX_CACHE_POLICY_WRITE_COMBINING == ARCH_MMU_FLAG_WRITE_COMBINING,
              "Cache policy constant mismatch - WRITE_COMBINING");

mx_handle_t sys_interrupt_create(mx_handle_t hrsrc, uint32_t vector, uint32_t options) {
    LTRACEF("vector %u options 0x%x\n", vector, options);

    mx_status_t status;
    if ((status = validate_resource_irq(hrsrc, vector)) < 0) {
        return status;
    }

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    status_t result = InterruptEventDispatcher::Create(vector, options, &dispatcher, &rights);
    if (result != MX_OK)
        return result;

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));

    auto up = ProcessDispatcher::GetCurrent();
    mx_handle_t hv = up->MapHandleToValue(handle);
    up->AddHandle(mxtl::move(handle));
    return hv;
}

mx_status_t sys_interrupt_complete(mx_handle_t handle_value) {
    LTRACEF("handle %x\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<InterruptDispatcher> interrupt;
    mx_status_t status = up->GetDispatcher(handle_value, &interrupt);
    if (status != MX_OK)
        return status;

    return interrupt->InterruptComplete();
}

mx_status_t sys_interrupt_wait(mx_handle_t handle_value) {
    LTRACEF("handle %x\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<InterruptDispatcher> interrupt;
    mx_status_t status = up->GetDispatcher(handle_value, &interrupt);
    if (status != MX_OK)
        return status;

    return interrupt->WaitForInterrupt();
}

mx_status_t sys_interrupt_signal(mx_handle_t handle_value) {
    LTRACEF("handle %x\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<InterruptDispatcher> interrupt;
    mx_status_t status = up->GetDispatcher(handle_value, &interrupt);
    if (status != MX_OK)
        return status;

    return interrupt->UserSignal();
}

mx_status_t sys_vmo_create_contiguous(mx_handle_t hrsrc, size_t size,
                                      uint32_t alignment_log2,
                                      user_ptr<mx_handle_t> _out) {
    LTRACEF("size 0x%zu\n", size);

    if (size == 0) return MX_ERR_INVALID_ARGS;
    if (alignment_log2 == 0)
        alignment_log2 = PAGE_SIZE_SHIFT;
    // catch obviously wrong values
    if (alignment_log2 < PAGE_SIZE_SHIFT ||
            alignment_log2 >= (8 * sizeof(uint64_t)))
        return MX_ERR_INVALID_ARGS;

    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource(hrsrc, MX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    size = ROUNDUP_PAGE_SIZE(size);
    // create a vm object
    mxtl::RefPtr<VmObject> vmo;
    status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, size, &vmo);
    if (status != MX_OK)
        return status;

    // always immediately commit memory to the object
    uint64_t committed;
    // CommitRangeContiguous takes a uint8_t for the alignment
    auto align_log2_arg = static_cast<uint8_t>(alignment_log2);
    status = vmo->CommitRangeContiguous(0, size, &committed, align_log2_arg);
    if (status < 0 || (size_t)committed < size) {
        LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", size / PAGE_SIZE,
                (size_t)committed / PAGE_SIZE);
        return MX_ERR_NO_MEMORY;
    }

    // create a Vm Object dispatcher
    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t result = VmObjectDispatcher::Create(mxtl::move(vmo), &dispatcher, &rights);
    if (result != MX_OK)
        return result;

    // create a handle and attach the dispatcher to it
    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return MX_ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    if (_out.copy_to_user(up->MapHandleToValue(handle)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));
    return MX_OK;
}

mx_status_t sys_vmo_create_physical(mx_handle_t hrsrc, uintptr_t paddr, size_t size,
                                    user_ptr<mx_handle_t> _out) {
    LTRACEF("size 0x%zu\n", size);

    // TODO: attempting to create a physical VMO that points to memory should be an error

    mx_status_t status;
    if ((status = validate_resource_mmio(hrsrc, paddr, size)) < 0) {
        return status;
    }

    size = ROUNDUP_PAGE_SIZE(size);

    // create a vm object
    mxtl::RefPtr<VmObject> vmo;
    mx_status_t result = VmObjectPhysical::Create(paddr, size, &vmo);
    if (result != MX_OK) {
        return result;
    }

    // create a Vm Object dispatcher
    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    result = VmObjectDispatcher::Create(mxtl::move(vmo), &dispatcher, &rights);
    if (result != MX_OK)
        return result;

    // create a handle and attach the dispatcher to it
    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return MX_ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    if (_out.copy_to_user(up->MapHandleToValue(handle)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));
    return MX_OK;
}

mx_status_t sys_bootloader_fb_get_info(user_ptr<uint32_t> format, user_ptr<uint32_t> width, user_ptr<uint32_t> height, user_ptr<uint32_t> stride) {
#if ARCH_X86
    if (!bootloader.fb_base ||
            format.copy_to_user(bootloader.fb_format) ||
            width.copy_to_user(bootloader.fb_width) ||
            height.copy_to_user(bootloader.fb_height) ||
            stride.copy_to_user(bootloader.fb_stride)) {
        return MX_ERR_INVALID_ARGS;
    } else {
        return MX_OK;
    }
#else
    return MX_ERR_NOT_SUPPORTED;
#endif
}

mx_status_t sys_set_framebuffer(mx_handle_t hrsrc, user_ptr<void> vaddr, uint32_t len, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource(hrsrc, MX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    intptr_t paddr = vaddr_to_paddr(vaddr.get());
    udisplay_set_framebuffer(paddr, len);

    struct display_info di;
    memset(&di, 0, sizeof(struct display_info));
    di.format = format;
    di.width = width;
    di.height = height;
    di.stride = stride;
    di.flags = DISPLAY_FLAG_HW_FRAMEBUFFER;
    udisplay_set_display_info(&di);

    return MX_OK;
}

mx_status_t sys_set_framebuffer_vmo(mx_handle_t hrsrc, mx_handle_t vmo_handle, uint32_t len, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    mx_status_t status;
    if ((status = validate_resource(hrsrc, MX_RSRC_KIND_ROOT)) < 0)
        return status;

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmObjectDispatcher> vmo;
    status = up->GetDispatcher(vmo_handle, &vmo);
    if (status != MX_OK)
        return status;

    status = udisplay_set_framebuffer_vmo(vmo->vmo());
    if (status != MX_OK)
        return status;

    struct display_info di;
    memset(&di, 0, sizeof(struct display_info));
    di.format = format;
    di.width = width;
    di.height = height;
    di.stride = stride;
    di.flags = DISPLAY_FLAG_HW_FRAMEBUFFER;
    udisplay_set_display_info(&di);

    return MX_OK;
}

/**
 * Gets info about an I/O mapping object.
 * @param handle Handle associated with an I/O mapping object.
 * @param out_vaddr Mapped virtual address for the I/O range.
 * @param out_len Mapped size of the I/O range.
 */
mx_status_t sys_io_mapping_get_info(mx_handle_t handle,
                                    user_ptr<uintptr_t> _out_vaddr,
                                    user_ptr<uint64_t> _out_size) {
    LTRACEF("handle %x\n", handle);

    if (!_out_vaddr || !_out_size)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<IoMappingDispatcher> io_mapping;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &io_mapping);
    if (status != MX_OK)
        return status;

    // If we do not have read rights, or we are calling from a different address
    // space than the one that this mapping exists in, refuse to tell the user
    // the vaddr/len of the mapping.
    if (ProcessDispatcher::GetCurrent()->aspace() != io_mapping->aspace())
        return MX_ERR_ACCESS_DENIED;

    uintptr_t vaddr = reinterpret_cast<uintptr_t>(io_mapping->vaddr());
    uint64_t  size  = io_mapping->size();

    status = _out_vaddr.copy_to_user(vaddr);
    if (status != MX_OK)
        return status;

    return _out_size.copy_to_user(size);
}

// TODO: Write docs for this syscall
mx_status_t sys_iommu_create(mx_handle_t rsrc_handle, uint32_t type, user_ptr<const void> desc,
                             uint32_t desc_len, user_ptr<mx_handle_t> out) {
    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource(rsrc_handle, MX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    TRACEF("IOMMU Create\n");
    static mxtl::RefPtr<Dispatcher> main_iommu = nullptr;
    static Mutex m;
    static mx_rights_t main_iommu_rights;
    AutoLock guard(&m);
    if (type == MX_IOMMU_TYPE_DUMMY && main_iommu) {
        TRACEF("Using stashed IOMMU\n");
        HandleOwner handle(MakeHandle(main_iommu, main_iommu_rights));

        auto up = ProcessDispatcher::GetCurrent();
        if (out.copy_to_user(up->MapHandleToValue(handle)) != MX_OK)
            return MX_ERR_INVALID_ARGS;

        up->AddHandle(mxtl::move(handle));
        return MX_OK;
    }

    if (desc_len > MX_IOMMU_MAX_DESC_LEN) {
        return MX_ERR_INVALID_ARGS;
    }

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;

    {
        // Copy the descriptor into the kernel and try to create the dispatcher
        // using it.
        AllocChecker ac;
        mxtl::unique_ptr<uint8_t[]> copied_desc(new (&ac) uint8_t[desc_len]);
        if (!ac.check()) {
            return MX_ERR_NO_MEMORY;
        }
        if ((status = desc.copy_array_from_user(copied_desc.get(), desc_len)) != MX_OK) {
            return status;
        }
        status = IommuDispatcher::Create(type,
                                         mxtl::unique_ptr<const uint8_t[]>(copied_desc.release()),
                                         desc_len, &dispatcher, &rights);
        if (status != MX_OK) {
            return status;
        }
    }

    main_iommu = dispatcher;
    main_iommu_rights = rights;

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));

    auto up = ProcessDispatcher::GetCurrent();
    if (out.copy_to_user(up->MapHandleToValue(handle)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));
    return MX_OK;
}

#if ARCH_X86
#include <arch/x86/descriptor.h>
#include <arch/x86/ioport.h>

mx_status_t sys_mmap_device_io(mx_handle_t hrsrc, uint32_t io_addr, uint32_t len) {
    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource(hrsrc, MX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    LTRACEF("addr 0x%x len 0x%x\n", io_addr, len);

    return IoBitmap::GetCurrent().SetIoBitmap(io_addr, len, 1);
}
#else
mx_status_t sys_mmap_device_io(mx_handle_t hrsrc, uint32_t io_addr, uint32_t len) {
    // doesn't make sense on non-x86
    return MX_ERR_NOT_SUPPORTED;
}
#endif

uint64_t sys_acpi_uefi_rsdp(mx_handle_t hrsrc) {
    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource(hrsrc, MX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }
#if ARCH_X86
    return bootloader.acpi_rsdp;
#endif
    return 0;
}

mx_status_t sys_acpi_cache_flush(mx_handle_t hrsrc) {
    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource(hrsrc, MX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }
    // TODO(teisenbe): This should be restricted to when interrupts are
    // disabled, but we haven't added support for letting the ACPI process
    // disable interrupts yet.  It only uses this for S-state transitions
    // like poweroff and (more importantly) sleep.
#if ARCH_X86
    __asm__ volatile ("wbinvd");
    return MX_OK;
#else
    return MX_ERR_NOT_SUPPORTED;
#endif
}

mx_status_t sys_bti_create(mx_handle_t iommu, uint64_t bti_id, user_ptr<mx_handle_t> out) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<IommuDispatcher> iommu_dispatcher;
    // TODO(teisenbe): This should probably have a right on it.
    mx_status_t status = up->GetDispatcherWithRights(iommu, MX_RIGHT_NONE, &iommu_dispatcher);
    if (status != MX_OK) {
        return status;
    }

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    // TODO(teisenbe): Migrate BusTransactionInitiatorDispatcher::Create to
    // taking the iommu_dispatcher
    status = BusTransactionInitiatorDispatcher::Create(iommu_dispatcher->iommu(), bti_id,
                                                       &dispatcher, &rights);
    if (status != MX_OK) {
        return status;
    }
    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));

    mx_handle_t hv = up->MapHandleToValue(handle);
    if ((status = out.copy_to_user(hv)) != MX_OK) {
        return status;
    }

    up->AddHandle(mxtl::move(handle));
    return MX_OK;
}

mx_status_t sys_bti_pin(mx_handle_t bti, mx_handle_t vmo, uint64_t offset, uint64_t size,
        uint32_t perms, user_ptr<uint64_t> extents, uint32_t extents_len,
        user_ptr<uint32_t> actual_extents_len) {
    auto up = ProcessDispatcher::GetCurrent();

    if (!IS_PAGE_ALIGNED(offset)) {
        return MX_ERR_INVALID_ARGS;
    }

    mxtl::RefPtr<BusTransactionInitiatorDispatcher> bti_dispatcher;
    mx_status_t status = up->GetDispatcherWithRights(bti, MX_RIGHT_MAP, &bti_dispatcher);
    if (status != MX_OK) {
        return status;
    }

    mxtl::RefPtr<VmObjectDispatcher> vmo_dispatcher;
    mx_rights_t vmo_rights;
    status = up->GetDispatcherAndRights(vmo, &vmo_dispatcher, &vmo_rights);
    if (status != MX_OK) {
        return status;
    }
    if (!(vmo_rights & MX_RIGHT_MAP)) {
        return MX_ERR_ACCESS_DENIED;
    }

    // Convert requested permissions and check against VMO rights
    uint32_t iommu_perms = 0;
    if (perms & MX_VM_FLAG_PERM_READ) {
        if (!(vmo_rights & MX_RIGHT_READ)) {
            return MX_ERR_ACCESS_DENIED;
        }
        iommu_perms |= IOMMU_FLAG_PERM_READ;
        perms &= ~MX_VM_FLAG_PERM_READ;
    }
    if (perms & MX_VM_FLAG_PERM_WRITE) {
        if (!(vmo_rights & MX_RIGHT_WRITE)) {
            return MX_ERR_ACCESS_DENIED;
        }
        iommu_perms |= IOMMU_FLAG_PERM_WRITE;
        perms &= ~MX_VM_FLAG_PERM_WRITE;
    }
    if (perms & MX_VM_FLAG_PERM_EXECUTE) {
        if (!(vmo_rights & MX_RIGHT_EXECUTE)) {
            return MX_ERR_ACCESS_DENIED;
        }
        iommu_perms |= IOMMU_FLAG_PERM_EXECUTE;
        perms &= ~MX_VM_FLAG_PERM_EXECUTE;
    }
    if (perms) {
        return MX_ERR_INVALID_ARGS;
    }

    AllocChecker ac;
    mxtl::InlineArray<dev_vaddr_t, 4u> mapped_extents(&ac, extents_len);
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }

    size_t actual_len;
    status = bti_dispatcher->Pin(vmo_dispatcher->vmo(), offset, size, iommu_perms,
                                 mapped_extents.get(), extents_len, &actual_len);
    if (status != MX_OK) {
        return status;
    }

    auto pin_cleanup = mxtl::MakeAutoCall([&bti_dispatcher, &mapped_extents, actual_len]() {
        bti_dispatcher->Unpin(mapped_extents.get(), actual_len);
    });

    static_assert(sizeof(dev_vaddr_t) == sizeof(uint64_t), "mismatched types");
    if ((status = extents.copy_array_to_user(mapped_extents.get(), actual_len)) != MX_OK) {
        return status;
    }
    if ((status = actual_extents_len.copy_to_user(static_cast<uint32_t>(actual_len))) != MX_OK) {
        return status;
    }

    pin_cleanup.cancel();
    return MX_OK;
}

mx_status_t sys_bti_unpin(mx_handle_t bti, user_ptr<const uint64_t> extents, uint32_t extents_len) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<BusTransactionInitiatorDispatcher> bti_dispatcher;
    mx_status_t status = up->GetDispatcherWithRights(bti, MX_RIGHT_MAP, &bti_dispatcher);
    if (status != MX_OK) {
        return status;
    }

    AllocChecker ac;
    mxtl::InlineArray<dev_vaddr_t, 4u> mapped_extents(&ac, extents_len);
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }
    static_assert(sizeof(dev_vaddr_t) == sizeof(uint64_t), "mismatched types");
    if ((status = extents.copy_array_from_user(mapped_extents.get(), extents_len)) != MX_OK) {
        return status;
    }

    return bti_dispatcher->Unpin(mapped_extents.get(), extents_len);

}
