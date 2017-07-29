// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <mxtl/intrusive_double_list.h>
#include <mxtl/macros.h>
#include <mxtl/unique_ptr.h>

#include "hw.h"
#include "iommu_page.h"

namespace intel_iommu {

class DeviceContext;
class IommuImpl;

class ContextTableState : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<ContextTableState>> {
public:
    ~ContextTableState();

    // Create a ContextTableState for the given bus.
    // If extended is true, then this will represent a reg::ExtendedContextTable,
    // and the table will handle translations for either the lower (dev<16) or
    // upper half of this bus.  Otherwise it represents a reg::ContextTable.
    static status_t Create(uint8_t bus, bool extended, bool upper,
                           IommuImpl* parent, volatile ds::RootEntrySubentry* root_entry,
                           mxtl::unique_ptr<ContextTableState>* table);

    // Check if this ContextTableState is for the given BDF
    bool includes_bdf(uint8_t bus, uint8_t dev_func) const {
        if (bus != bus_) return false;
        if (!extended_) return true;
        return (dev_func >= 0x80) == upper_;
    }

    // Create a new DeviceContext representing the given BDF.  It is a fatal error
    // to try to create a context for a BDF that already has one.
    status_t CreateDeviceContext(uint8_t bus, uint8_t dev_func, DeviceContext** context);

    status_t GetDeviceContext(uint8_t bus, uint8_t dev_func, DeviceContext** context);
private:
    ContextTableState(uint8_t bus, bool extended, bool upper, IommuImpl* parent,
                      volatile ds::RootEntrySubentry* root_entry, IommuPage page);

    DISALLOW_COPY_ASSIGN_AND_MOVE(ContextTableState);

    volatile ds::ContextTable* table() const {
        DEBUG_ASSERT(!extended_);
        return reinterpret_cast<volatile ds::ContextTable*>(page_.vaddr());
    }

    volatile ds::ExtendedContextTable* extended_table() const {
        DEBUG_ASSERT(extended_);
        return reinterpret_cast<volatile ds::ExtendedContextTable*>(page_.vaddr());
    }

    // Pointer to IOMMU that owns this ContextTableState
    IommuImpl* const parent_;
    // Pointer to the half of the Root Table Entry that decodes to this
    // ContextTable.
    volatile ds::RootEntrySubentry* const root_entry_;

    // Page backing the ContextTable/ExtendedContextTable
    const IommuPage page_;

    // List of device configurations beneath this ContextTable.
    mxtl::DoublyLinkedList<mxtl::unique_ptr<DeviceContext>> devices_;

    const uint8_t bus_;
    const bool extended_;
    // Only valid if extended_ is true
    const bool upper_;
};

} // namespace intel_iommu
