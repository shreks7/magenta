// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define PERI_CRG_PEREN2             0x020
#define PERI_CRG_PERDIS2            0x024
#define PERI_CRG_PERCLKEN2          0x028
#define PERI_CRG_PERSTAT2           0x02C
#define PERI_CRG_PEREN4             0x040
#define PERI_CRG_PERDIS4            0x044
#define PERI_CRG_PERCLKEN4          0x048
#define PERI_CRG_PERSTAT4           0x04C
#define PERI_CRG_PERRSTEN2          0x078
#define PERI_CRG_PERRSTDIS2         0x07C
#define PERI_CRG_PERRSTSTAT2        0x080
#define PERI_CRG_PERRSTEN3          0x084
#define PERI_CRG_PERRSTDIS3         0x088
#define PERI_CRG_PERRSTSTAT3        0x08C
#define PERI_CRG_PERRSTEN4          0x090
#define PERI_CRG_PERRSTDIS4         0x094
#define PERI_CRG_PERRSTSTAT4        0x098
#define PERI_CRG_ISOEN              0x144
#define PERI_CRG_ISODIS             0x148
#define PERI_CRG_ISOSTAT            0x14C

#define PEREN4_GT_ACLK_USB3OTG                  (1 << 1)
#define PEREN4_GT_CLK_USB3OTG_REF               (1 << 0)

#define PERRSTEN4_USB3OTG_MUX                   (1 << 8)
#define PERRSTEN4_USB3OTG_AHBIF                 (1 << 7)
#define PERRSTEN4_USB3OTG_32K                   (1 << 6)
#define PERRSTEN4_USB3OTG                       (1 << 5)
#define PERRSTEN4_USB3OTGPHY_POR                (1 << 3)

#define PERISOEN_USB_REFCLK_ISO_EN              (1 << 25)

#define PERI_CRG_CLK_EN4        (0x40)
#define PERI_CRG_CLK_DIS4       (0x44)
#define PERI_CRG_RSTDIS4        (0x94)
#define PERI_CRG_RSTEN4         (0x90)
#define SC_CLK_USB3PHY_3MUX1_SEL                (1 << 25)

#define PCTRL_CTRL3                             0x10
#define PCTRL_CTRL24                            0x064

#define PCTRL_CTRL3_USB_TXCO_EN                 (1 << 1)
#define PCTRL_CTRL24_USB3PHY_3MUX1_SEL          (1 << 25)

#define USB_TCXO_EN                             (1 << 1)

#define GT_CLK_USB3OTG_REF                      (1 << 0)
#define GT_ACLK_USB3OTG                         (1 << 1)
#define GT_CLK_USB3PHY_REF                      (1 << 2)

// BC registers
#define USBOTG3_CTRL0       0x00
#define USBOTG3_CTRL1       0x04
#define USBOTG3_CTRL2       0x08
#define USBOTG3_CTRL3       0x0C
#define USBOTG3_CTRL4       0x10
#define USBOTG3_CTRL5       0x14
#define USBOTG3_CTRL6       0x18
#define USBOTG3_CTRL7       0x1C
#define USBOTG3_STS0        0x20
#define USBOTG3_STS1        0x24
#define USBOTG3_STS2        0x28
#define USBOTG3_STS3        0x2C
#define BC_CTRL0            0x30
#define BC_CTRL1            0x34
#define BC_CTRL2            0x38
#define BC_STS0             0x3C
#define RAM_CTRL            0x40
#define USBOTG3_STS4        0x44
#define USB3PHY_CTRL        0x48
#define USB3PHY_STS         0x4C
#define USB3PHY_CR_STS      0x50
#define USB3PHY_CR_CTRL     0x54
#define USB3_RES            0x58


#define USB3OTG_CTRL0_SC_USB3PHY_ABB_GT_EN      (1 << 15)
#define USB3OTG_CTRL2_TEST_POWERDOWN_SSP        (1 << 1)
#define USB3OTG_CTRL2_TEST_POWERDOWN_HSP        (1 << 0)
#define USB3OTG_CTRL3_VBUSVLDEXT                (1 << 6)
#define USB3OTG_CTRL3_VBUSVLDEXTSEL             (1 << 5)
#define USB3OTG_CTRL7_REF_SSP_EN                (1 << 16)
#define USB3OTG_PHY_CR_DATA_OUT(x)              (((x) & 0xFFFF) << 1)
#define USB3OTG_PHY_CR_ACK                      (1 << 0)
#define USB3OTG_PHY_CR_DATA_IN(x)               (((x) & 0xFFFF) << 4)
#define USB3OTG_PHY_CR_WRITE                    (1 << 3)
#define USB3OTG_PHY_CR_READ                     (1 << 2)
#define USB3OTG_PHY_CR_CAP_DATA                 (1 << 1)
#define USB3OTG_PHY_CR_CAP_ADDR                 (1 << 0)
