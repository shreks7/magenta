// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Transfer Request Block
typedef volatile struct {
    union {
    uint64_t ptr;
        struct {
            uint32_t ptr_low;
            uint32_t ptr_high;
        } __PACKED;
    } __PACKED;
    uint32_t status;
    uint32_t control;
} __PACKED dwc3_trb_t;

// TRB status fields
#define TRB_BUFSIZ_START    0           // Buffer Size
#define TRB_BUFSIZ_BITS     24
#define TRB_PCM1_START      24          // Packet Count M1
#define TRB_PCM1_BITS       2
#define TRB_SPR             (1 << 26)   // Short Packet Received
#define TRB_TRBSTS_START    28          // TRB Status
#define TRB_TRBSTS_BITS     4

// TRB control fields
#define TRB_HWO             (1 << 0)   // Hardware Owner of Descriptor
#define TRB_LST             (1 << 1)   // Last TRB
#define TRB_CHN             (1 << 2)   // Chain Buffers
#define TRB_CSP             (1 << 3)   // Continue on Short Packet
#define TRB_TRBCTL_START    4          // TRB Control
#define TRB_TRBCTL_BITS     6
#define TRB_ISP             (1 << 10)  // Interrupt on Short Packet
#define TRB_IMI             (1 << 10)  // Interrupt on Missed ISOC
#define TRB_IOC             (1 << 11)  // Interrupt on Complete
#define TRB_STREAM_ID_START 14          // Stream ID
#define TRB_STREAM_ID_BITS  16
#define TRB_SOF_NUM_START   14          // SOF Number
#define TRB_SOF_NUM_BITS    16
