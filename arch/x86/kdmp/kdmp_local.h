#ifndef _KDMP_LOCAL_H_
#define _KDMP_LOCAL_H_

#include <asm/kdmp.h>

/*#define DEBUG_PRINT_ENABLE*/

#ifdef DEBUG_PRINT_ENABLE
#define DBG(fmt_, ...)   printk(KERN_INFO fmt_, __VA_ARGS__);
#else  /* DEBUG_PRINT_ENABLE */
#define DBG(fmt_, ...)   do { } while (0)
#endif /* DEBUG_PRINT_ENABLE */

/* panic dump function prototype. */
int kdmp_panicdump_exec(void);
struct kdmp_data_t *kdmp_current_dump_region(void);
#if IS_ENABLED(CONFIG_CUSTOM_CRASHDUMP)
void dump_call_panic(void);
void dump_call_nmi(void);
void dump_call_ipi(void);
#endif

struct kdmp_cpuid_def_t {
	unsigned int leaf;
	unsigned int subleaf;
};
#define CPUID_LEAF_MIN	0x00000000
#define CPUID_LEAF_EX_MIN	0x80000000

struct kdmp_msr_def_t {
	unsigned int msr;
};

struct kdmp_localapic_def_t {
	uint32_t offset;
};

/* PCI */
#define PCI_CONFIG_ADDRESS	0x0cf8
#define PCI_CONFIG_DATA	0x0cfc	/* 0x0cfc-0cff */


/* bit0-1   fixed:0 */
/* bit2-7   reg_addr */
/* bit8-10  func num */
/* bit11-15 device num */
/* bit16-23 bus num */
/* 24-30    reserve */
/* bit31    Enable bit fixed:1 */
#define PCI_CONF_ADDR(bus, dev, fn, reg)	\
	(0x80000000				\
	| ((bus & 0xFF) << 16)		\
	| ((dev & 0x1F) << 11)		\
	| ((fn & 0x7) << 8)			\
	| ((reg & 0x3F << 2)))

/* generate bfn.*/
#define PCI_CONF_BDF(bus, dev, fn)	PCI_CONF_ADDR(bus, dev, fn, 0)
/* generate config address by bdf. */
#define PCI_CONF_ADDR_BDF(bdf, reg)	(bdf | (reg & 0x3F << 2))


/**
 * relation of register's src and dst.
 */
struct kdmp_reg_map_t {
	int regid;			/*!< register id */
	unsigned long offset;	/*!< read register offset */
	unsigned long size;		/*!< read/write data size */
	unsigned long idx_offset;	/*!< index register offset */
	unsigned long idx_val;	/*!< index data */
	unsigned long pos;		/*!< write area offset */
};

#define IDX_NON	0xFFFFFFFF	/*!< disabled kdmp_reg_map_t.idx_offset */

/* Dump GPRs in panic */
extern void (*panic_dump_gprs)(void); /* kernel/panic.c */
#if IS_ENABLED(CONFIG_CUSTOM_CRASHDUMP)
/* GPRs in exception context */
extern void (*ipi_dump_gprs)(void);
extern void (*nmi_dump_gprs)(void);
#endif

#if IS_ENABLED(CONFIG_CUSTOM_CRASHDUMP)
/* arch/x86/kernel/dumpstack.c */
extern struct pt_regs *kdmp_ecxt_regs[PDMP_N_CORE];
extern struct pt_regs *kdmp_nmi_regs[PDMP_N_CORE];
extern struct pt_regs *kdmp_ipi_regs[PDMP_N_CORE];
#endif
/* Dump regions */
extern void *kdmp_buf[];


extern int kdmp_init_reg_info(void);
extern int kdmp_conf_reg_info(void);

extern int kdmp_read_reg(int regid, int offset, void *buf, int size);
extern int kdmp_write_reg(int regid, int offset, u32 data, int size);

int kdmp_live_init(void);
void kdmp_live_fini(void);
void kdmp_live_capture(struct pt_regs *regs, u32 source, u32 id,
		       unsigned long data);

/* in arch/x86/pci/mmconfig_32.c*/
extern int kdmp_pci_mmcfg_read(unsigned int seg, unsigned int bdf,
		int reg, int len, u8 *value);

#endif /* _KDMP_LOCAL_H_ */

/* end of kdmp_local.h */
