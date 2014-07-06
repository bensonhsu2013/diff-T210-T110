/*
 * P3060 QDS Setup
 *
 * Copyright 2011 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/phy.h>
#include <asm/machdep.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <linux/of_platform.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>
#include <asm/ehv_pic.h>
#include "corenet_ds.h"

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init p3060_qds_probe(void)
{
	unsigned long root = of_get_flat_dt_root();
#ifdef CONFIG_SMP
	extern struct smp_ops_t smp_85xx_ops;
#endif

	if (of_flat_dt_is_compatible(root, "fsl,P3060QDS"))
		return 1;

	/* Check if we're running under the Freescale hypervisor */
	if (of_flat_dt_is_compatible(root, "fsl,P3060QDS-hv")) {
		ppc_md.init_IRQ = ehv_pic_init;
		ppc_md.get_irq = ehv_pic_get_irq;
		ppc_md.restart = fsl_hv_restart;
		ppc_md.power_off = fsl_hv_halt;
		ppc_md.halt = fsl_hv_halt;
#ifdef CONFIG_SMP
		/*
		 * Disable the timebase sync operations because we can't write
		 * to the timebase registers under the hypervisor.
		 */
		smp_85xx_ops.give_timebase = NULL;
		smp_85xx_ops.take_timebase = NULL;
#endif
		return 1;
	}

	return 0;
}

define_machine(p3060_qds) {
	.name			= "P3060 QDS",
	.probe			= p3060_qds_probe,
	.setup_arch		= corenet_ds_setup_arch,
	.init_IRQ		= corenet_ds_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
	.get_irq		= mpic_get_coreint_irq,
	.restart		= fsl_rstcr_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
	.power_save		= e500_idle,
};

machine_device_initcall(p3060_qds, corenet_ds_publish_devices);

#ifdef CONFIG_SWIOTLB
machine_arch_initcall(p3060_qds, swiotlb_setup_bus_notifier);
#endif
