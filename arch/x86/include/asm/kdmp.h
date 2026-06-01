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

enum kdmp_event_source {
	KDMP_EVENT_EXCEPTION = 1,
	KDMP_EVENT_PAGE_FAULT = 2,
	KDMP_EVENT_IRQ = 3,
	KDMP_EVENT_NMI = 4,
	KDMP_EVENT_IPI = 5,
	KDMP_EVENT_PANIC = 6,
	KDMP_EVENT_DIRECT_PANIC = 7,
	KDMP_EVENT_SYSVEC = 8,
};

#ifdef __KERNEL__
typedef void (*kdmp_event_hook_t)(struct pt_regs *regs, u32 source, u32 id,
				   unsigned long data);
#endif

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
#if IS_ENABLED(CONFIG_CUSTOM_CRASHCUMP)
extern kdmp_event_hook_t kdmp_event_hook;
/* Typed symbols to inspect custom dump slots directly in crash. */
extern struct kdmp_data_t *kdmp_kdmp_primary;
extern struct kdmp_data_t *kdmp_kdmp_slot[PDMP_N_CORE];

static __always_inline void kdmp_capture_event(struct pt_regs *regs, u32 source,
					       u32 id, unsigned long data)
{
	kdmp_event_hook_t hook = READ_ONCE(kdmp_event_hook);

	if (unlikely(hook))
		hook(regs, source, id, data);
}
#else
static __always_inline void kdmp_capture_event(struct pt_regs *regs, u32 source,
					       u32 id, unsigned long data)
{
}
#endif

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
 * (kdmp_head_t::status)
 */
#define PDMP_OK_HEAD          0x00000001
#define PDMP_OK_STAT          0x00000002
#define PDMP_OK_PRINTK        0x00000004
#define PDMP_OK_PCI_REGS      0x00000008
#define PDMP_OK_IO_REGS       0x00000010
#define PDMP_OK_LOCAL_APIC    0x00000020

/**
 * Panic Dump Data Flags
 * (kdmp_head_t::flags_status[n])
 */
#define PDMP_OK_GPRS          0x01
#define PDMP_OK_GPRS_EXCP     0x02
#define PDMP_OK_CR_MM_DR      0x04
#define PDMP_OK_FPU           0x08
#define PDMP_OK_MMX           0x10
#define PDMP_OK_XMM           0x20
#define PDMP_OK_MSRS          0x40
#define PDMP_OK_KSTACK        0x80

struct kdmp_head_t {
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

struct kdmp_x86_gprs_t {
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

struct kdmp_x86_xmmrs_t {
	uint8_t xmm[8][16];
	uint32_t mxcsr;
};

struct kdmp_x86_memmrs_t {
	struct desc_ptr gdtr;
	struct desc_ptr idtr;
	uint32_t ldtr;
	uint32_t tr;
};

struct kdmp_cpuid_t {
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;
};

struct kdmp_x86_msr_t {
	int err;
	unsigned long long data;
};

struct kdmp_x86_regs_t {
	struct kdmp_x86_gprs_t gprs;
	uint64_t saved_sp;
	uint8_t rsv0[24];
	struct pt_regs excp_gprs;
	uint8_t rsv1[108];
	struct fregs_state fpurs;
	uint8_t rsv2[48];
	uint64_t mmxrs[8];
	uint8_t rsv3[192];
	struct kdmp_x86_xmmrs_t xmmrs;
	uint8_t rsv4[124];
	uint32_t crs[4];
	uint8_t rsv5[240];
	struct kdmp_x86_memmrs_t memrs;
	uint8_t rsv6[60];
	uint32_t dbgrs[6];
	uint8_t rsv7[152];
	struct kdmp_cpuid_t cpuids[32];
	uint8_t rsv8[448];
	struct kdmp_x86_msr_t mcmsrs[57];
	uint8_t rsv9[84];
	struct kdmp_x86_msr_t msrs[141];
	uint8_t rsv10[676];
	uint32_t local_apic_regs[47];
	uint8_t rsv11[2372];
};

struct kdmp_kstack_head_t {
	uint32_t sp;
	uint32_t stack_size;
};

#define PDMP_SZ_KSTACK		(8UL<<10)
#define PDMP_SZ_PRINTK_BUF	(8UL<<10)
#define PDMP_SZ_GLOBAL_TBL	(4UL<<10)
#define PDMP_SZ_IOREG_INFO	(32UL<<10)
#define PDMP_SZ_PCIREG_INFO	(128UL<<10)

/* PCI dump region offsets (pcidevregs) */
#define PDMP_OFS_PCI_HOST_CFG		0x0000
#define PDMP_OFS_PCI_VGA_CFG		0x0100
#define PDMP_OFS_PCI_E1000E_CFG		0x0200
#define PDMP_OFS_PCI_LPC_CFG		0x0300
#define PDMP_OFS_PCI_SATA_CFG		0x0400
#define PDMP_OFS_PCI_SMBUS_CFG		0x0500
#define PDMP_OFS_PCI_VIRTIO_CFG		0x0600
#define PDMP_OFS_PCI_PCIE_RP_CFG	0x0700
#define PDMP_OFS_PCI_TEST_CFG		0x0800
#define PDMP_OFS_PCI_NET_CFG		0x0900
#define PDMP_OFS_PCI_STOR_CFG		0x0a00
#define PDMP_OFS_PCI_PCIE_EP_CFG	0x0b00
#define PDMP_SZ_PCI_STD_CFG		0x0100
#define PDMP_SZ_PCI_PCIE_EP_CFG		0x0300

/* IO/MMIO dump region offsets (ioregs) */
#define PDMP_OFS_IO_LEGACY		0x0000
#define PDMP_SZ_IO_LEGACY		0x0080
#define PDMP_OFS_IO_TEST_MMIO		0x0080
#define PDMP_SZ_IO_TEST_MMIO		0x0400
#define PDMP_OFS_IO_TEST_PIO		0x0480
#define PDMP_SZ_IO_TEST_PIO		0x0080
#define PDMP_OFS_IO_NET_MMIO		0x0500
#define PDMP_SZ_IO_NET_MMIO		0x0200
#define PDMP_OFS_IO_NET_PIO		0x0700
#define PDMP_SZ_IO_NET_PIO		0x0040
#define PDMP_OFS_IO_STOR_MMIO		0x0740
#define PDMP_SZ_IO_STOR_MMIO		0x0400
#define PDMP_OFS_IO_PCIE_EP_MMIO	0x0b40
#define PDMP_SZ_IO_PCIE_EP_MMIO		0x0400
#define PDMP_OFS_IO_VGA_MMIO		0x1000
#define PDMP_SZ_IO_VGA_MMIO		0x1000
#define PDMP_OFS_IO_E1000E_MMIO		0x2000
#define PDMP_SZ_IO_E1000E_MMIO		0x1000
#define PDMP_OFS_IO_E1000E_PIO		0x3000
#define PDMP_SZ_IO_E1000E_PIO		0x0020
#define PDMP_OFS_IO_SATA_MMIO		0x3100
#define PDMP_SZ_IO_SATA_MMIO		0x1000
#define PDMP_OFS_IO_SATA_PIO		0x4100
#define PDMP_SZ_IO_SATA_PIO		0x0020
#define PDMP_OFS_IO_VIRTIO_MMIO		0x4200
#define PDMP_SZ_IO_VIRTIO_MMIO		0x1000
#define PDMP_OFS_IO_VIRTIO_PMMIO	0x5200
#define PDMP_SZ_IO_VIRTIO_PMMIO		0x1000
#define PDMP_OFS_IO_VIRTIO_PIO		0x6200
#define PDMP_SZ_IO_VIRTIO_PIO		0x0040
#define PDMP_OFS_IO_PCIE_RP_MMIO	0x6300
#define PDMP_SZ_IO_PCIE_RP_MMIO		0x1000

struct kdmp_pcidevregs_t {
	uint8_t host_cfg[PDMP_SZ_PCI_STD_CFG];
	uint8_t vga_cfg[PDMP_SZ_PCI_STD_CFG];
	uint8_t e1000e_cfg[PDMP_SZ_PCI_STD_CFG];
	uint8_t lpc_cfg[PDMP_SZ_PCI_STD_CFG];
	uint8_t sata_cfg[PDMP_SZ_PCI_STD_CFG];
	uint8_t smbus_cfg[PDMP_SZ_PCI_STD_CFG];
	uint8_t virtio_cfg[PDMP_SZ_PCI_STD_CFG];
	uint8_t pcie_rp_cfg[PDMP_SZ_PCI_STD_CFG];
	uint8_t test_cfg[PDMP_SZ_PCI_STD_CFG];
	uint8_t net_cfg[PDMP_SZ_PCI_STD_CFG];
	uint8_t stor_cfg[PDMP_SZ_PCI_STD_CFG];
	uint8_t pcie_ep_cfg[PDMP_SZ_PCI_PCIE_EP_CFG];
	uint8_t rsv[PDMP_SZ_PCIREG_INFO -
		(PDMP_OFS_PCI_PCIE_EP_CFG + PDMP_SZ_PCI_PCIE_EP_CFG)];
};

union kdmp_pcidevregs_u {
	uint8_t raw[PDMP_SZ_PCIREG_INFO];
	struct kdmp_pcidevregs_t view;
};

struct kdmp_ioregs_t {
	uint8_t legacy[PDMP_SZ_IO_LEGACY];
	uint8_t test_mmio[PDMP_SZ_IO_TEST_MMIO];
	uint8_t test_pio[PDMP_SZ_IO_TEST_PIO];
	uint8_t net_mmio[PDMP_SZ_IO_NET_MMIO];
	uint8_t net_pio[PDMP_SZ_IO_NET_PIO];
	uint8_t stor_mmio[PDMP_SZ_IO_STOR_MMIO];
	uint8_t pcie_ep_mmio[PDMP_SZ_IO_PCIE_EP_MMIO];
	uint8_t rsv0[PDMP_OFS_IO_VGA_MMIO -
		(PDMP_OFS_IO_PCIE_EP_MMIO + PDMP_SZ_IO_PCIE_EP_MMIO)];
	uint8_t vga_mmio[PDMP_SZ_IO_VGA_MMIO];
	uint8_t e1000e_mmio[PDMP_SZ_IO_E1000E_MMIO];
	uint8_t e1000e_pio[PDMP_SZ_IO_E1000E_PIO];
	uint8_t rsv1[PDMP_OFS_IO_SATA_MMIO -
		(PDMP_OFS_IO_E1000E_PIO + PDMP_SZ_IO_E1000E_PIO)];
	uint8_t sata_mmio[PDMP_SZ_IO_SATA_MMIO];
	uint8_t sata_pio[PDMP_SZ_IO_SATA_PIO];
	uint8_t rsv2[PDMP_OFS_IO_VIRTIO_MMIO -
		(PDMP_OFS_IO_SATA_PIO + PDMP_SZ_IO_SATA_PIO)];
	uint8_t virtio_mmio[PDMP_SZ_IO_VIRTIO_MMIO];
	uint8_t virtio_pmmio[PDMP_SZ_IO_VIRTIO_PMMIO];
	uint8_t virtio_pio[PDMP_SZ_IO_VIRTIO_PIO];
	uint8_t rsv3[PDMP_OFS_IO_PCIE_RP_MMIO -
		(PDMP_OFS_IO_VIRTIO_PIO + PDMP_SZ_IO_VIRTIO_PIO)];
	uint8_t pcie_rp_mmio[PDMP_SZ_IO_PCIE_RP_MMIO];
	uint8_t rsv4[PDMP_SZ_IOREG_INFO -
		(PDMP_OFS_IO_PCIE_RP_MMIO + PDMP_SZ_IO_PCIE_RP_MMIO)];
};

union kdmp_ioregs_u {
	uint8_t raw[PDMP_SZ_IOREG_INFO];
	struct kdmp_ioregs_t view;
};

#define PDMP_SZ_CORE_RSV	\
	((24UL<<10) - sizeof(struct kdmp_x86_regs_t) - PDMP_SZ_KSTACK)
#define PDMP_SZ_DATA_RSV0	\
	((1UL<<10) - sizeof(struct kdmp_head_t) - sizeof(struct sysinfo))
#define PDMP_SZ_DATA_RSV1	\
	((15UL<<10) - PDMP_SZ_PRINTK_BUF)

#define KDMP_LIVE_MAGIC			0x4556494c /* LIVE */
#define KDMP_LIVE_VERSION		1
/* 31 events × 224 bytes + 32 byte header = 6976 bytes ≤ PDMP_SZ_DATA_RSV1 (7168) */
#define KDMP_LIVE_NR_EVENTS		31

#ifdef __KERNEL__
struct kdmp_live_event_t {
	u32 committed;
	u32 cpu;
	u32 source;
	u32 id;
	u64 timestamp;
	u64 data;
	u64 ip;
	u64 sp;
	u64 flags;
	struct pt_regs regs;
};

struct kdmp_live_ring_t {
	u32 magic;
	u32 version;
	u32 entry_size;
	u32 entry_count;
	u64 write_seq;
	u64 dropped;
	struct kdmp_live_event_t events[KDMP_LIVE_NR_EVENTS];
};
#endif

struct kdmp_core_info_t {
	struct kdmp_x86_regs_t x86_regs;
	uint8_t kstack[PDMP_SZ_KSTACK];
	uint8_t rsv[PDMP_SZ_CORE_RSV];
};

struct kdmp_data_t {
	struct kdmp_head_t head;
	struct sysinfo sysinfo;
	uint8_t rsv0[PDMP_SZ_DATA_RSV0];
	struct kdmp_core_info_t status[PDMP_N_STATUS];
	uint8_t printk_buf[PDMP_SZ_PRINTK_BUF];
	uint8_t rsv1[PDMP_SZ_DATA_RSV1];
	uint8_t global_tbl[PDMP_SZ_GLOBAL_TBL];
	union kdmp_pcidevregs_u pcidevregs;
	union kdmp_ioregs_u ioregs;
};

#define PDMP_SZ_DATA		(1<<20)

#endif /* INC_PANIC_DUMP_HEADER_H_ */