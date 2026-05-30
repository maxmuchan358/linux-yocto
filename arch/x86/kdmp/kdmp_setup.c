/*
 * kdmp_setup.c - setup panic dump variables and notifier
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/panic_notifier.h>
#include <linux/smp.h>
#include <asm/io.h>
#include <asm/kdmp.h>
#include "kdmp_local.h"


/* initialization flag */
static int kdmp_panic_ready;

/* local function prototypes */
static int kdmp_panicdump(struct notifier_block *this,
		unsigned long event,
		void *ptr);

struct kdmp_data_t *kdmp_current_dump_region(void)
{
	int cpu = raw_smp_processor_id();

	if (cpu < 0 || cpu >= PDMP_N_CORE)
		cpu = 0;

	return kdmp_buf[cpu];
}

/* panic notifier block */
static struct notifier_block kdmp_panic_notifier = {
	.notifier_call = kdmp_panicdump,
};

/**
 * notifier function
 */
static int kdmp_panicdump(struct notifier_block *this,
		unsigned long event,
		void *ptr)
{
	if (!kdmp_panic_ready)
		return NOTIFY_DONE;

	/* call panic dump function */
	kdmp_panicdump_exec();
	return NOTIFY_DONE;
}

/* panic dump output (virtual addresses by ioremap()) */
void *kdmp_buf[PDMP_N_CORE] = { NULL, NULL, NULL, NULL };

struct kdmp_data_t *kdmp_kdmp_primary;
EXPORT_SYMBOL_GPL(kdmp_kdmp_primary);

struct kdmp_data_t *kdmp_kdmp_slot[PDMP_N_CORE];
EXPORT_SYMBOL_GPL(kdmp_kdmp_slot);


/**
 * initialize panic dump variables.
 */
static int __init kdmp_initialize(void)
{
	resource_size_t phys_addr;
	int i;

	/* clear initialized flag */
	kdmp_panic_ready = 0;
	kdmp_active = false;

	/* initialize output region */
	for (i = 0; i < PDMP_N_CORE; i++) {
		phys_addr = kdmp_res.start + (PDMP_SZ_DATA * i);
		kdmp_buf[i] = ioremap(phys_addr, PDMP_SZ_DATA);
		kdmp_kdmp_slot[i] = (struct kdmp_data_t *)kdmp_buf[i];

		if (kdmp_buf[i] != NULL)
			memset(kdmp_buf[i], 0, PDMP_SZ_DATA);
	}
	kdmp_kdmp_primary = kdmp_kdmp_slot[0];

	/* init pointer at arch/x86/kernel/dumpstack.c */
#if IS_ENABLED(CONFIG_CUSTOM_CRASHCUMP)
	memset(kdmp_ecxt_regs, 0, sizeof(kdmp_ecxt_regs));
	memset(kdmp_nmi_regs, 0, sizeof(kdmp_nmi_regs));
	memset(kdmp_ipi_regs, 0, sizeof(kdmp_ipi_regs));
#endif

	/* set call back handler to x86 regs dump */
	panic_dump_gprs = dump_call_panic;
	nmi_dump_gprs = dump_call_nmi;
	#ifdef CONFIG_SMP
	ipi_dump_gprs = dump_call_ipi;
	#endif

	/* init register info. */
	kdmp_init_reg_info();

	/* register kdmp_panicdump() to panic_notifier_list */
	atomic_notifier_chain_register(
		&panic_notifier_list, &kdmp_panic_notifier);

	printk(KERN_INFO "kdmp: init panicdump "
			"notifier=%p buf0=%p buf1=%p buf2=%p buf3=%p\n",
			kdmp_panicdump,
			kdmp_buf[0], kdmp_buf[1], kdmp_buf[2], kdmp_buf[3]);

	return 0;
}

/**
 * configure panic dump variables.
 *
 * call after pci initialization. (subsys_initcall).
 */
static int __init kdmp_configure(void)
{
	/* coufigure register info. */
	kdmp_conf_reg_info();

	kdmp_active = true;
	kdmp_panic_ready = 1;

	return 0;
}

#ifndef MODULE
subsys_initcall(kdmp_initialize);
subsys_initcall_sync(kdmp_configure);
#else
static int __init kdmp_module_init(void)
{
	int ret;

	ret = kdmp_initialize();
	if (ret)
		return ret;

	return kdmp_configure();
}

module_init(kdmp_module_init);
/*
 * Unloading is intentionally unsupported: core crash paths retain callback
 * hooks and the early reserved kdmp memory outlives the module lifecycle.
 */
MODULE_DESCRIPTION("x86 custom crashdump support");
MODULE_LICENSE("GPL");
#endif

/* end of kdmp_setup.c */
