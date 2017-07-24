// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/reg.h>
#include <stdio.h>

#include "hi3660-bus.h"
#include "hi3660-regs.h"

mx_status_t hi3360_usb_init(hi3660_bus_t* bus) {
printf("hi3360_usb_init\n");
    volatile void* usb3otg_bc = bus->usb3otg_bc.vaddr;
    volatile void* peri_crg = bus->peri_crg.vaddr;
    volatile void* pctrl = bus->pctrl.vaddr;
 
	writel(PERRSTEN4_USB3OTG, peri_crg + PERI_CRG_PERRSTEN4);
	writel(PERRSTEN4_USB3OTGPHY_POR, peri_crg + PERI_CRG_PERRSTEN4);
	writel(PERRSTEN4_USB3OTG_MUX | PERRSTEN4_USB3OTG_AHBIF | PERRSTEN4_USB3OTG_32K, peri_crg + PERI_CRG_PERRSTEN4);
	writel(PEREN4_GT_ACLK_USB3OTG | PEREN4_GT_CLK_USB3OTG_REF, peri_crg + PERI_CRG_PERDIS4);

	writel(~PCTRL_CTRL24_USB3PHY_3MUX1_SEL, pctrl + PCTRL_CTRL24);
	writel((PCTRL_CTRL3_USB_TXCO_EN << 16) | 0, pctrl + PCTRL_CTRL3);

    mx_nanosleep(mx_deadline_after(MX_MSEC(10)));

// release part
    mx_nanosleep(mx_deadline_after(MX_MSEC(10)));

   /* enable USB REFCLK ISO */
	writel(PERISOEN_USB_REFCLK_ISO_EN, peri_crg + PERI_CRG_ISODIS);
 
  /* enable USB_TXCO_EN */
	writel((PCTRL_CTRL3_USB_TXCO_EN << 16) | PCTRL_CTRL3_USB_TXCO_EN, pctrl + PCTRL_CTRL3);

	writel(~PCTRL_CTRL24_USB3PHY_3MUX1_SEL, pctrl + PCTRL_CTRL24);
	writel(PEREN4_GT_ACLK_USB3OTG | PEREN4_GT_CLK_USB3OTG_REF, peri_crg + PERI_CRG_PEREN4);
	writel(PERRSTEN4_USB3OTG_MUX | PERRSTEN4_USB3OTG_AHBIF | PERRSTEN4_USB3OTG_32K, peri_crg + PERI_CRG_PERRSTDIS4);

	writel(PERRSTEN4_USB3OTG | PERRSTEN4_USB3OTGPHY_POR, peri_crg + PERI_CRG_PERRSTEN4);


  /* enable PHY REF CLK */
    uint32_t temp = readl(usb3otg_bc + USBOTG3_CTRL0);
 	writel(temp | USB3OTG_CTRL0_SC_USB3PHY_ABB_GT_EN, usb3otg_bc + USBOTG3_CTRL0);

    temp = readl(usb3otg_bc + USBOTG3_CTRL7);
 	writel(temp | USB3OTG_CTRL7_REF_SSP_EN, usb3otg_bc + USBOTG3_CTRL7);

  /* exit from IDDQ mode */
    temp = readl(usb3otg_bc + USBOTG3_CTRL2);
 	writel(temp & ~(USB3OTG_CTRL2_TEST_POWERDOWN_SSP | USB3OTG_CTRL2_TEST_POWERDOWN_HSP), usb3otg_bc + USBOTG3_CTRL2);

    mx_nanosleep(mx_deadline_after(MX_MSEC(10)));

	writel(PERRSTEN4_USB3OTGPHY_POR, peri_crg + PERI_CRG_PERRSTDIS4);
	writel(PERRSTEN4_USB3OTG, peri_crg + PERI_CRG_PERRSTDIS4);

    mx_nanosleep(mx_deadline_after(MX_MSEC(10)));

    temp = readl(usb3otg_bc + USBOTG3_CTRL3);
 	writel(temp | USB3OTG_CTRL3_VBUSVLDEXT | USB3OTG_CTRL3_VBUSVLDEXTSEL, usb3otg_bc + USBOTG3_CTRL7);

    mx_nanosleep(mx_deadline_after(MX_MSEC(10)));

	writel(0x1c466e3, usb3otg_bc + USBOTG3_CTRL4);
 
 #if 0
	/* enable usb_tcxo_en */
	writel(USB_TCXO_EN | (USB_TCXO_EN << PERI_CTRL3_MSK_START),
			pctrl + PCTRL_PERI_CTRL3);

	/* select usbphy clk from abb */
	uint32_t temp = readl(pctrl + PCTRL_PERI_CTRL24);
	temp &= ~SC_CLK_USB3PHY_3MUX1_SEL;
	writel(temp, pctrl + PCTRL_PERI_CTRL24);

	/* open clk gate */
	writel(GT_CLK_USB3OTG_REF | GT_ACLK_USB3OTG,
			peri_crg + PERI_CRG_CLK_EN4);

#endif
    return MX_OK;
}
