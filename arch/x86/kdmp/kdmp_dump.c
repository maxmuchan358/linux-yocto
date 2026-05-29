/*
 * kdmp_dump.c - dump CPU registers and chipset registers for panic dump
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/kmsg_dump.h>
#include <linux/smp.h>
#include <linux/swap.h>
#include <linux/module.h>
#include <linux/timekeeping.h>
#include <asm/apic.h>
#include <asm/page.h>
#include <asm/debugreg.h>
#include <asm/fpu/api.h>
#include <asm/msr.h>
#include <asm/special_insns.h>
#include <asm/kdmp.h>
#include "kdmp_local.h"
#include "kdmp_localapic.h"
#include "kdmp_msrs.h"
#include "kdmp_ioregs.h"

#define KDMP_PANIC		0
#define KDMP_NMI		1
#define KDMP_IPI		2
#define KDMP_DIR_PANIC		3

static int kdmp_cpuid_defs_num = ARRAY_SIZE(kdmp_cpuid_defs);
static int kdmp_msr_defs_num = ARRAY_SIZE(kdmp_msr_defs);
static int kdmp_mcmsr_defs_num = ARRAY_SIZE(kdmp_mcmsr_defs);
static int kdmp_pcireg_map_count = ARRAY_SIZE(kdmp_pcireg_map);
static int kdmp_ioreg_map_count = ARRAY_SIZE(kdmp_ioreg_map);
static int kdmp_localapic_defs_num = ARRAY_SIZE(kdmp_localapic_defs);

/* local function prototypes */
static void dump_ecxt_gprs(struct pdmp_data_t *dmpbuf, int status,
		struct pt_regs *ecxt_regs);
static int dump_kstack(struct pdmp_data_t *dmpbuf, int status,
		struct pt_regs *ecxt_regs);
static int setup_header(struct pdmp_data_t *dmpbuf);
static int dump_arch_regs(struct pdmp_data_t *dmpbuf, int status);
static int dump_msrs(struct pdmp_data_t *dmpbuf, int status);
static int dump_local_apic(struct pdmp_data_t *dmpbuf, int status);
static int dump_pci_regs(struct pdmp_data_t *dmpbuf);
static int dump_io_regs(struct pdmp_data_t *dmpbuf);
static int kdmp_printk_tail(u8 *dest, int len);
static void kdmp_dump_gprs(int status);
static void kdmp_dump_x86(int status);
static unsigned long kdmp_get_sp(int status, unsigned long sp);
static bool kdmp_local_apic_read_safe(u32 offset, u32 *value);
static inline void pdmp_clts(void)
{
	asm volatile ("clts");
}

static int kdmp_printk_tail(u8 *dest, int len)
{
	struct kmsg_dump_iter iter;
	size_t copied = 0;

	if (!dest || len <= 0)
		return 0;

	kmsg_dump_rewind(&iter);
	if (!kmsg_dump_get_buffer(&iter, true, dest, len, &copied))
		return 0;

	return copied;
}

static void pdmp_set_flag(struct pdmp_data_t *dmpbuf, u32 flag)
{
	dmpbuf->head.status |= flag;
}

static void pdmp_set_flag_core(struct pdmp_data_t *dmpbuf, int status, u8 flag)
{
	dmpbuf->head.flags_status[status] |= flag;
}

/**
 * Panic dump
 * (called by panic notifier)
 */
int kdmp_panicdump_exec(void)
{
	int cc;
	struct pdmp_data_t *dmpbuf;

	/* output address */
	dmpbuf = kdmp_current_dump_region();
	if (dmpbuf == NULL)
		return NOTIFY_DONE;
	/* get pci registers */
	dump_pci_regs(dmpbuf);
	/* get IO registers (peripheral registers) */
	dump_io_regs(dmpbuf);

	/* printk buffer save */
	cc = kdmp_printk_tail(dmpbuf->printk_buf, sizeof(dmpbuf->printk_buf));
	DBG("%s: printk get %08x bytes\n", __func__, cc);
	pdmp_set_flag(dmpbuf, PDMP_OK_PRINTK); /* set data flag */

	return NOTIFY_DONE;
}

static struct pt_regs *dump_get_regs_addr(int status)
{
	int cpu = raw_smp_processor_id();
	struct pt_regs *regs = NULL;

	if (cpu < 0 || cpu >= PDMP_N_CORE)
		cpu = 0;

	switch (status) {
	case KDMP_PANIC:
		regs = kdmp_ecxt_regs[cpu];
		break;
	case KDMP_NMI:
		regs = kdmp_nmi_regs[cpu];
		break;
	case KDMP_IPI:
		regs = kdmp_ipi_regs[cpu];
		break;
	default:
		break;
	}

	return regs;
}

static int dump_check_call_status(int *status)
{
	int cpu = raw_smp_processor_id();

	if (cpu < 0 || cpu >= PDMP_N_CORE)
		cpu = 0;

	if (status == NULL)
		return -EINVAL;

	if (*status == KDMP_PANIC) {
		if (kdmp_nmi_regs[cpu] != NULL)
			return -EBUSY;
		if (kdmp_ecxt_regs[cpu] == NULL)
			*status = KDMP_DIR_PANIC;
	} else if (*status == KDMP_IPI) {
		if (kdmp_nmi_regs[cpu] != NULL)
			return -EBUSY;
	}

	return 0;
}

static unsigned long kdmp_get_sp(int status, unsigned long sp)
{
	struct pdmp_data_t *dmpbuf;

	dmpbuf = kdmp_current_dump_region();
	if (dmpbuf == NULL)
		return sp;

	if (dmpbuf->head.flags_status[status] & PDMP_OK_GPRS)
		sp = dmpbuf->status[status].x86_regs.saved_sp;

	return sp;
}

static void kdmp_dump_x86(int status)
{
	struct pdmp_data_t *dmpbuf;
	struct pt_regs *ecxt_regs;

	dmpbuf = kdmp_current_dump_region();
	if (dmpbuf == NULL)
		return;

	if (dump_check_call_status(&status) != 0)
		return;

	ecxt_regs = dump_get_regs_addr(status);

	setup_header(dmpbuf);
	dump_ecxt_gprs(dmpbuf, status, ecxt_regs);
	dump_kstack(dmpbuf, status, ecxt_regs);
	dump_arch_regs(dmpbuf, status);
	dump_msrs(dmpbuf, status);
	dump_local_apic(dmpbuf, status);
}

/**
 * get GPRs (bank registers)
 * (called by panic())
 */
static void kdmp_dump_gprs(int status)
{
	struct pdmp_data_t *dmpbuf; /* output address */
	struct pdmp_x86_gprs_t gprs = {0};
	register unsigned long current_sp asm ("rsp");
	unsigned long flags;
	/* General Purpose Register */
	asm("movl %%eax, %0" : "=r"(gprs.eax));
	asm("movl %%ebx, %0" : "=r"(gprs.ebx));
	asm("movl %%ecx, %0" : "=r"(gprs.ecx));
	asm("movl %%edx, %0" : "=r"(gprs.edx));
	asm("movl %%esi, %0" : "=r"(gprs.esi));
	asm("movl %%edi, %0" : "=r"(gprs.edi));
	asm("movl %%ebp, %0" : "=r"(gprs.ebp));
	asm("movl %%esp, %0" : "=r"(gprs.esp));
	/* Segment Register */
	asm("movw %%cs, %0" : "=r"(gprs.cs));
	asm("movw %%ds, %0" : "=r"(gprs.ds));
	asm("movw %%ss, %0" : "=r"(gprs.ss));
	asm("movw %%es, %0" : "=r"(gprs.es));
	asm("movw %%fs, %0" : "=r"(gprs.fs));
	asm("movw %%gs, %0" : "=r"(gprs.gs));
	/* EFLAGS */
	asm volatile ("pushfq; popq %0" : "=rm"(flags));
	gprs.eflags = flags;

	dmpbuf = kdmp_current_dump_region(); /* output address */
	if (dmpbuf == NULL)
		return;

	if (dump_check_call_status(&status) != 0)
		return;

	dmpbuf->status[status].x86_regs.gprs = gprs;
	dmpbuf->status[status].x86_regs.saved_sp = current_sp;
	/* set data flag */
	pdmp_set_flag_core(dmpbuf, status, PDMP_OK_GPRS);

	return;
}

/**
 * get GPRs from excpetion context.
 */
static void dump_ecxt_gprs(struct pdmp_data_t *dmpbuf, int status,
		struct pt_regs *ecxt_regs)
{
	struct pt_regs *dst;

	if (ecxt_regs != NULL) {
		dst = &dmpbuf->status[status].x86_regs.excp_gprs;
		memcpy(dst, ecxt_regs, sizeof(struct pt_regs));
		/* set data flag */
		pdmp_set_flag_core(dmpbuf, status, PDMP_OK_GPRS_EXCP);
	}
}

/**
 * get kernel stack
 */
static int dump_kstack(struct pdmp_data_t *dmpbuf, int status,
		struct pt_regs *ecxt_regs)
{
	register unsigned long current_sp asm ("rsp");
	unsigned long sp;
	unsigned int stack_size = 0;
	unsigned int area_size = 0;
	uint8_t *kstack;
	struct pdmp_kstack_head_t *kstack_head;
	unsigned int stack_pos = sizeof(struct pdmp_kstack_head_t);

	/* use sp in exception context */
	if (ecxt_regs != NULL) {
		/* get sp from "struct pt_regs" */
		if (user_mode(ecxt_regs))
			sp = ecxt_regs->sp;
		else
			sp = kernel_stack_pointer(ecxt_regs);
	} else
		sp = kdmp_get_sp(status, current_sp);

	stack_size = THREAD_SIZE - ((THREAD_SIZE - 1) & sp);
	/*
	 * if stack pointer locate in user space, adjust size in 4K page.
	 * if copy over 4K boundary, it may cause page faults.
	 */
	if (sp < PAGE_OFFSET) {
		unsigned int page_bottom = (sp | (PAGE_SIZE - 1)) + 1;
		if ((sp + stack_size) > page_bottom)
			stack_size = page_bottom - sp;
	}
	/* adjust to dump area size. */
	area_size = PDMP_SZ_KSTACK - sizeof(struct pdmp_kstack_head_t);
	if (stack_size > area_size)
		stack_size = area_size;

	/* select dump area. */
	kstack = dmpbuf->status[status].kstack;
	kstack_head = (struct pdmp_kstack_head_t *)kstack;
	/* copy kernel stack */
	kstack_head->sp = sp;
	kstack_head->stack_size = stack_size;
	memcpy(&kstack[stack_pos], (void *)sp, stack_size);
	/* set data flag */
	pdmp_set_flag_core(dmpbuf, status, PDMP_OK_KSTACK);

	return 0;
}

/**
 * setup dump header
 */
static int setup_header(struct pdmp_data_t *dmpbuf)
{
	struct pdmp_head_t *head = &dmpbuf->head;
	struct timespec64 ts;

	/* initialize dump header */
	head->magic = PDMP_MAGIC_NUM;
	head->format = PDMP_FORMAT_VERSION;
	/* time stamp (jiffies) */
	head->timestamp = jiffies_64;
	/* time information */
	if (ktime_get_real_ts64_try(&ts)) {
		head->timeval.tv_sec = ts.tv_sec;
		head->timeval.tv_usec = ts.tv_nsec / NSEC_PER_USEC;
	} else {
		head->timeval.tv_sec = -1;
		head->timeval.tv_usec = -1;
	}
	/* set data flag */
	pdmp_set_flag(dmpbuf, PDMP_OK_HEAD);

	/* sysinfo */
	memset(&dmpbuf->sysinfo, 0xFF, sizeof(dmpbuf->sysinfo));
	dmpbuf->sysinfo.totalram = totalram_pages();
	dmpbuf->sysinfo.sharedram = global_node_page_state(NR_SHMEM);
	dmpbuf->sysinfo.freeram = global_zone_page_state(NR_FREE_PAGES);
	if (!nr_blockdev_pages_trylock(&dmpbuf->sysinfo.bufferram)) {
		dmpbuf->sysinfo.bufferram = ~0UL;
	}
	dmpbuf->sysinfo.totalhigh = totalhigh_pages();
	dmpbuf->sysinfo.freehigh = nr_free_highpages();
	dmpbuf->sysinfo.mem_unit = PAGE_SIZE;
	if (!si_swapinfo_trylock(&dmpbuf->sysinfo)) {
		dmpbuf->sysinfo.freeswap = dmpbuf->sysinfo.totalswap = ~0UL;
	}
	/* set data flag */
	pdmp_set_flag(dmpbuf, PDMP_OK_STAT);

	return 0;
}

static int dump_arch_regs(struct pdmp_data_t *dmpbuf, int status)
{
	struct pdmp_x86_regs_t *x86_regs = &dmpbuf->status[status].x86_regs;

	uint64_t *mmxrs = x86_regs->mmxrs; /* mm0-mm7 */
	struct pdmp_x86_xmmrs_t *xmmrs = &x86_regs->xmmrs;
	uint32_t *crs = x86_regs->crs; /* CR0,CR2-4 */
	struct pdmp_x86_memmrs_t *memrs = &x86_regs->memrs;
	uint32_t *dbgrs = x86_regs->dbgrs; /* DR0-3,6,7 */

	/* Read Control Registeres */
	crs[0] = read_cr0();
	crs[1] = read_cr2();
	crs[2] = __read_cr3();
	crs[3] = __read_cr4();

	/* Read Memory Management Registers */
	store_gdt(&memrs->gdtr);
	store_idt(&memrs->idtr);
	store_ldt(memrs->ldtr);
	store_tr(memrs->tr);

	/* Read Debug Registeres */
	get_debugreg(dbgrs[0], 0);
	get_debugreg(dbgrs[1], 1);
	get_debugreg(dbgrs[2], 2);
	get_debugreg(dbgrs[3], 3);
	get_debugreg(dbgrs[4], 6);
	get_debugreg(dbgrs[5], 7);

	/* set data flag */
	pdmp_set_flag_core(dmpbuf, status, PDMP_OK_CR_MM_DR);

	/* clear Task Swtich bit */
	if (crs[0] & X86_CR0_TS)
		pdmp_clts();

	/* dump x87 FPU */
	if ((crs[0] & X86_CR0_EM) == 0) {
		asm("fnsave %0 ; fwait" : "=m"(x86_regs->fpurs));
		pdmp_set_flag_core(dmpbuf, status, PDMP_OK_FPU);
	}

	/* Read MMX Registers */
	asm("movq %%mm0, %0" : "=m"(mmxrs[0]));
	asm("movq %%mm1, %0" : "=m"(mmxrs[1]));
	asm("movq %%mm2, %0" : "=m"(mmxrs[2]));
	asm("movq %%mm3, %0" : "=m"(mmxrs[3]));
	asm("movq %%mm4, %0" : "=m"(mmxrs[4]));
	asm("movq %%mm5, %0" : "=m"(mmxrs[5]));
	asm("movq %%mm6, %0" : "=m"(mmxrs[6]));
	asm("movq %%mm7, %0" : "=m"(mmxrs[7]));
	pdmp_set_flag_core(dmpbuf, status, PDMP_OK_MMX);

	/* Read XMM Registeres */
	asm("movups %%xmm0, %0" : "=m"(xmmrs->xmm[0]));
	asm("movups %%xmm1, %0" : "=m"(xmmrs->xmm[1]));
	asm("movups %%xmm2, %0" : "=m"(xmmrs->xmm[2]));
	asm("movups %%xmm3, %0" : "=m"(xmmrs->xmm[3]));
	asm("movups %%xmm4, %0" : "=m"(xmmrs->xmm[4]));
	asm("movups %%xmm5, %0" : "=m"(xmmrs->xmm[5]));
	asm("movups %%xmm6, %0" : "=m"(xmmrs->xmm[6]));
	asm("movups %%xmm7, %0" : "=m"(xmmrs->xmm[7]));
	asm("stmxcsr %0" : "=m"(xmmrs->mxcsr));
	pdmp_set_flag_core(dmpbuf, status, PDMP_OK_XMM);

	return 0;
}

/* dump CPUID and MSR */
static
void cpuid1(unsigned int leaf, struct pdmp_cpuid_t *r)
{
	asm volatile (
		"cpuid"
		: "=a"(r->eax), "=b"(r->ebx), "=c"(r->ecx), "=d"(r->edx)
		: "a"(leaf)
	);
	return;
}

static
void cpuid2(unsigned int leaf, unsigned int subleaf, struct pdmp_cpuid_t *r)
{
	asm volatile (
		"cpuid"
		: "=a"(r->eax), "=b"(r->ebx), "=c"(r->ecx), "=d"(r->edx)
		: "a"(leaf), "c"(subleaf)
	);
	return;
}

/**
 * dump MSRs(Model-Specific Registers)
 */
static int dump_msrs(struct pdmp_data_t *dmpbuf, int status)
{
	struct pdmp_x86_regs_t *x86_regs = &dmpbuf->status[status].x86_regs;
	struct pdmp_cpuid_t *cpuids = x86_regs->cpuids;
	struct pdmp_cpuid_t cpuid;
	unsigned int leaf, subleaf;
	unsigned long maxleaf, maxleaf_ex;
	struct pdmp_x86_msr_t *mcmsrs = x86_regs->mcmsrs;
	struct pdmp_x86_msr_t *msrs = x86_regs->msrs;
	int i;
	/* CPUID */
	cpuid1(CPUID_LEAF_MIN, &cpuid);
	maxleaf = cpuid.eax;
	cpuid1(CPUID_LEAF_EX_MIN, &cpuid);
	maxleaf_ex = cpuid.eax;
	for (i = 0; i < kdmp_cpuid_defs_num; i++) {
		leaf = kdmp_cpuid_defs[i].leaf;
		subleaf = kdmp_cpuid_defs[i].subleaf;
		if ((leaf >= CPUID_LEAF_MIN && leaf <= maxleaf)
			|| (leaf >= CPUID_LEAF_EX_MIN && leaf <= maxleaf_ex)) {
			if (subleaf == CPUID_SUBLEAF_NA)
				cpuid1(leaf, &cpuids[i]);
			else
				cpuid2(leaf, subleaf, &cpuids[i]);
		}
	}

	/* Machine Check MSR */
	for (i = 0; i < kdmp_mcmsr_defs_num; i++) {
		/* read msr. */
		mcmsrs[i].err = rdmsrq_safe(kdmp_mcmsr_defs[i].msr,
				&mcmsrs[i].data);
	}
	/* model-specific register */
	for (i = 0; i < kdmp_msr_defs_num; i++) {
		/* read msr. */
		msrs[i].err = rdmsrq_safe(kdmp_msr_defs[i].msr, &msrs[i].data);
	}
	/* set data flag */
	pdmp_set_flag_core(dmpbuf, status, PDMP_OK_MSRS);
	return 0;
}

static int dump_local_apic(struct pdmp_data_t *dmpbuf, int status)
{
	struct pdmp_x86_regs_t *x86_regs = &dmpbuf->status[status].x86_regs;
	uint32_t *apic_dst = x86_regs->local_apic_regs;
	int i;

	for (i = 0; i < kdmp_localapic_defs_num; i++) {
		if (!kdmp_local_apic_read_safe(kdmp_localapic_defs[i].offset,
					      &apic_dst[i]))
			apic_dst[i] = 0xffffffff;
	}

	pdmp_set_flag(dmpbuf, PDMP_OK_LOCAL_APIC);
	return 0;
}

static bool kdmp_local_apic_read_safe(u32 offset, u32 *value)
{
	u64 msr;

	if (!value)
		return false;

	if (!x2apic_enabled()) {
		*value = kdmp_apic_read(offset);
		return true;
	}

	if (rdmsrq_safe(APIC_BASE_MSR + (offset >> 4), &msr))
		return false;

	*value = (u32)msr;
	return true;
}


static int dump_pci_regs(struct pdmp_data_t *dmpbuf)
{
	struct pdmp_reg_map_t *regmap;
	int i;
	int ret = -1;
	u8 *dst;
	u32 pos = 0;

	dst = dmpbuf->pcidevregs;
	for (i = 0; i < kdmp_pcireg_map_count; ++i) {
		regmap = &kdmp_pcireg_map[i];
		if (regmap->pos != 0)
			pos = regmap->pos;

		/* dump register */
		if (regmap->idx_offset == IDX_NON)
			ret = kdmp_read_reg(regmap->regid, regmap->offset,
					&dst[pos], regmap->size);
		else {
			/* index register */
			ret = kdmp_write_reg(regmap->regid, regmap->idx_offset,
					regmap->idx_val, 1);
			if (ret == 0)
				ret = kdmp_read_reg(regmap->regid,
						regmap->offset, &dst[pos],
						regmap->size);
		}
		if (ret == 0)
			dmpbuf->head.dbg |= 0x1ULL << regmap->regid;

		pos += regmap->size;
	}

	/* set data flag */
	pdmp_set_flag(dmpbuf, PDMP_OK_PCI_REGS);
	return 0;
}

/**
 * get IO registers
 */
static int dump_io_regs(struct pdmp_data_t *dmpbuf)
{
	struct pdmp_reg_map_t *regmap;
	u32 pos = 0;
	u8 *dst;
	int i;
	int ret = -1;

	dst = dmpbuf->ioregs;
	for (i = 0; i < kdmp_ioreg_map_count; i++) {
		regmap = &kdmp_ioreg_map[i];
		if (regmap->pos != 0)
			pos = regmap->pos;

		/* dump register */
		if (regmap->idx_offset == IDX_NON)
			ret = kdmp_read_reg(regmap->regid, regmap->offset,
					&dst[pos], regmap->size);
		else {
			/* index register */
			ret = kdmp_write_reg(regmap->regid, regmap->idx_offset,
					regmap->idx_val, 1);
			if (ret == 0)
				ret = kdmp_read_reg(regmap->regid,
						regmap->offset, &dst[pos],
						regmap->size);
		}

		if (ret == 0)
			dmpbuf->head.dbg |= 1ULL << regmap->regid;

		pos += regmap->size;
	}
	/* set data flag */
	pdmp_set_flag(dmpbuf, PDMP_OK_IO_REGS);
	return 0;
}

void dump_call_panic(void)
{
	kdmp_dump_gprs(KDMP_PANIC);
	kdmp_dump_x86(KDMP_PANIC);
}

void dump_call_ipi(void)
{
	kdmp_dump_gprs(KDMP_IPI);
	kdmp_dump_x86(KDMP_IPI);
}

void dump_call_nmi(void)
{
	kdmp_dump_gprs(KDMP_NMI);
	kdmp_dump_x86(KDMP_NMI);
}

/* end of kdmp_dump.c */
