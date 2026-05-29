/**
 * kdmp.h - panic dump header definitions
 */
#ifndef INC_PANIC_DUMP_HEADER_H_
#define INC_PANIC_DUMP_HEADER_H_

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/ioport.h>
#include <linux/sysinfo.h>
#include <uapi/linux/time.h>
#include <asm/ptrace.h>
#include <asm/desc.h>
#include <asm/fpu/types.h>
#else  /* __KERNEL__ */
#include <inttypes.h>
#include <sys/time.h>
#include <sys/sysinfo.h>
#endif /* __KERNEL__ */

#define PDMP_N_CORE			4
#define PDMP_N_STATUS			4
/* Location of the reserved area for the panic dump */
extern struct resource kdmp_res;
extern bool kdmp_active;
/* Crash-analysis friendly shortcuts for the reserved kdmp range. */
extern phys_addr_t kdmp_phys_base;
extern resource_size_t kdmp_phys_size;
extern u32 kdmp_apic_read(u32 reg);

extern struct pt_regs *kdmp_ecxt_regs[PDMP_N_CORE];
extern struct pt_regs *kdmp_nmi_regs[PDMP_N_CORE];
extern struct pt_regs *kdmp_ipi_regs[PDMP_N_CORE];
/* Typed symbols to inspect custom dump slots directly in crash. */
extern struct pdmp_data_t *kdmp_pdmp_primary;
extern struct pdmp_data_t *kdmp_pdmp_slot[PDMP_N_CORE];

/**
 * Panic Dump Format Identifier
 */
#define PDMP_FORMAT_ID			0x55504343  /* "CCPU" */

/**
 * Panic Dump Format Version
 * Increment this macro if you update dump format.
 */
#define PDMP_FORMAT_VERSION		0x00000001

/**
 * Panic Dump Data Flags
 * (pdmp_head_t::status)
 */
#define PDMP_OK_HEAD          0x00000001
#define PDMP_OK_STAT          0x00000002
#define PDMP_OK_PRINTK        0x00000004
#define PDMP_OK_PCI_REGS      0x00000008
#define PDMP_OK_IO_REGS       0x00000010
#define PDMP_OK_LOCAL_APIC    0x00000020

/**
 * Panic Dump Data Flags
 * (pdmp_head_t::flags_status[n])
 */
#define PDMP_OK_GPRS          0x01
#define PDMP_OK_GPRS_EXCP     0x02
#define PDMP_OK_CR_MM_DR      0x04
#define PDMP_OK_FPU           0x08
#define PDMP_OK_MMX           0x10
#define PDMP_OK_XMM           0x20
#define PDMP_OK_MSRS          0x40
#define PDMP_OK_KSTACK        0x80

struct pdmp_head_t {
	uint32_t magic;
#define PDMP_MAGIC_NUM  PDMP_FORMAT_ID
	uint32_t format;
	uint32_t status;
	uint8_t flags_status[PDMP_N_STATUS];
	uint64_t timestamp;
	struct __kernel_old_timeval timeval;
	uint64_t dbg;
	uint8_t rsv1[24];
};

struct pdmp_x86_gprs_t {
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint32_t esi;
	uint32_t edi;
	uint32_t ebp;
	uint32_t esp;
	uint16_t cs;
	uint16_t ds;
	uint16_t ss;
	uint16_t es;
	uint16_t fs;
	uint16_t gs;
	uint32_t eflags;
};

struct pdmp_x86_xmmrs_t {
	uint8_t xmm[8][16];
	uint32_t mxcsr;
};

struct pdmp_x86_memmrs_t {
	struct desc_ptr gdtr;
	struct desc_ptr idtr;
	uint32_t ldtr;
	uint32_t tr;
};

struct pdmp_cpuid_t {
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;
};

struct pdmp_x86_msr_t {
	int err;
	unsigned long long data;
};

struct pdmp_x86_regs_t {
	struct pdmp_x86_gprs_t gprs;
	uint64_t saved_sp;
	uint8_t rsv0[24];
	struct pt_regs excp_gprs;
	uint8_t rsv1[108];
	struct fregs_state fpurs;
	uint8_t rsv2[48];
	uint64_t mmxrs[8];
	uint8_t rsv3[192];
	struct pdmp_x86_xmmrs_t xmmrs;
	uint8_t rsv4[124];
	uint32_t crs[4];
	uint8_t rsv5[240];
	struct pdmp_x86_memmrs_t memrs;
	uint8_t rsv6[60];
	uint32_t dbgrs[6];
	uint8_t rsv7[152];
	struct pdmp_cpuid_t cpuids[32];
	uint8_t rsv8[448];
	struct pdmp_x86_msr_t mcmsrs[57];
	uint8_t rsv9[84];
	struct pdmp_x86_msr_t msrs[141];
	uint8_t rsv10[676];
	uint32_t local_apic_regs[47];
	uint8_t rsv11[2372];
};

struct pdmp_kstack_head_t {
	uint32_t sp;
	uint32_t stack_size;
};

#define PDMP_SZ_KSTACK		(8UL<<10)
#define PDMP_SZ_PRINTK_BUF	(8UL<<10)
#define PDMP_SZ_GLOBAL_TBL	(4UL<<10)
#define PDMP_SZ_IOREG_INFO	(32UL<<10)
#define PDMP_SZ_PCIREG_INFO	(128UL<<10)

#define PDMP_SZ_CORE_RSV	\
	((24UL<<10) - sizeof(struct pdmp_x86_regs_t) - PDMP_SZ_KSTACK)
#define PDMP_SZ_DATA_RSV0	\
	((1UL<<10) - sizeof(struct pdmp_head_t) - sizeof(struct sysinfo))
#define PDMP_SZ_DATA_RSV1	\
	((15UL<<10) - PDMP_SZ_PRINTK_BUF)

struct pdmp_core_info_t {
	struct pdmp_x86_regs_t x86_regs;
	uint8_t kstack[PDMP_SZ_KSTACK];
	uint8_t rsv[PDMP_SZ_CORE_RSV];
};

struct pdmp_data_t {
	struct pdmp_head_t head;
	struct sysinfo sysinfo;
	uint8_t rsv0[PDMP_SZ_DATA_RSV0];
	struct pdmp_core_info_t status[PDMP_N_STATUS];
	uint8_t printk_buf[PDMP_SZ_PRINTK_BUF];
	uint8_t rsv1[PDMP_SZ_DATA_RSV1];
	uint8_t global_tbl[PDMP_SZ_GLOBAL_TBL];
	uint8_t pcidevregs[PDMP_SZ_PCIREG_INFO];
	uint8_t ioregs[PDMP_SZ_IOREG_INFO];
};

#define PDMP_SZ_DATA		(1<<20)

#endif /* INC_PANIC_DUMP_HEADER_H_ */