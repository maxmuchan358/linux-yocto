/*
 * kdmp_regs.c - access to chipset registers for panic dump
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <asm/io.h>
#include "kdmp_local.h"
#include "kdmp_regid.h"

/* local function prototypes */
static void *kdmp_get_mmap_addr(int regid);
static u32 kdmp_get_base_address(int baid);

/**
 * base address id.
 */
enum {
	BA_BEGIN,
	FIXED = BA_BEGIN,
	PMBASE,
	SMB_BASE,
	TEST_MMIO_BASE,
	TEST_IO_BASE,
	BA_END,
	BA_COUNT = BA_END,	/* count of base address id. */
};
/* invalid base address value definition.*/
#define BASE_ADDR_NA		0xFFFFFFFF

/**
 * base address list.
 * read from PCI and Memory, and setup.
 */
struct pdmp_base_addr_t {
	int id;
	u32 ba;
};

static struct pdmp_base_addr_t kdmp_base_addr[] = {
	{FIXED,	BASE_ADDR_NA},
	{PMBASE,	BASE_ADDR_NA},
	{SMB_BASE,	BASE_ADDR_NA},
	{TEST_MMIO_BASE,	BASE_ADDR_NA},
	{TEST_IO_BASE,	BASE_ADDR_NA},
};
int kdmp_base_addr_count = ARRAY_SIZE(kdmp_base_addr);

#define BA_MEMORY_MSK	0xFFFFFFF0
#define BA_IOPORT_MSK	0xFFFC


static u32 kdmp_get_base_address(int baid)
{
	int ret;
	u32 v32;
	u32 ba = BASE_ADDR_NA;

	if (kdmp_base_addr[baid].ba != BASE_ADDR_NA)
		return kdmp_base_addr[baid].ba;

	switch (baid) {
	case FIXED: /* Fixed */
		ba = 0x0000000;
		break;
	case PMBASE: /* Power Management */
		/* LPC Interface Bridge:ACPI Base Address: +40h */
		ret = kdmp_read_reg(REG_LPC_PCI_CFG, 0x40, &v32, sizeof(v32));
		if (ret == 0) {
			/* bit:7-15 Base Address. 128byte */
			if ((v32 != BASE_ADDR_NA) && (v32 != 0))
				ba = ((v32 & 0x0000FFFF) >> 7) * 128;
		}
		DBG("pdmp: PMBASE:%08x v:%08x\n", ba, v32);
		break;
	case SMB_BASE:/* SMBus */
		/* SMBus Controller Registers:SMBus Base Address: +20h */
		ret = kdmp_read_reg(REG_SMBUS_PCI_CFG, 0x20, &v32, sizeof(v32));
		if (ret == 0) {
			/* bit:5-15 Base Address. 32byte */
			if ((v32 != BASE_ADDR_NA) && (v32 != 0))
				ba = ((v32 & 0x0000FFFF) >> 5) * 32;
		}
		DBG("pdmp: SMB_BASE:%08x v:%08x\n", ba, v32);
		break;
	case TEST_MMIO_BASE:
		/* custom-crashdump-test BAR0 */
		ret = kdmp_read_reg(REG_TEST_DEV_PCI_CFG, 0x10, &v32, sizeof(v32));
		if (ret == 0 && (v32 != BASE_ADDR_NA) && (v32 != 0))
			ba = v32 & BA_MEMORY_MSK;
		DBG("pdmp: TEST_MMIO_BASE:%08x v:%08x\n", ba, v32);
		break;
	case TEST_IO_BASE:
		/* custom-crashdump-test BAR1 */
		ret = kdmp_read_reg(REG_TEST_DEV_PCI_CFG, 0x14, &v32, sizeof(v32));
		if (ret == 0 && (v32 != BASE_ADDR_NA) && (v32 != 0))
			ba = v32 & BA_IOPORT_MSK;
		DBG("pdmp: TEST_IO_BASE:%08x v:%08x\n", ba, v32);
		break;
	default:
		return ba;
	}

	kdmp_base_addr[baid].ba = ba;
	return ba;
}

struct pdmp_reg_def_t {
	int id;
	int type;
	int base;	/* type depended. PCI:bdf, other:baid*/
	u32 base_offset;
	u32 size;
	unsigned long value;	/* type depended. PCI:domain,MM:virt-ptr */
};

/*
 * register types.
 * pdmp_reg_def_t::type
 */
enum {
	REG_T_PCI,
	REG_T_MM,
	REG_T_IO,
};

/**
 * register definitions.
 */
#define BDF(b , d, f)	PCI_CONF_BDF(b, d, f)

/* q35 + ICH9 default topology */
static struct pdmp_reg_def_t kdmp_reg_def[] = {
{REG_HOST_DEV_CFG,	REG_T_PCI, BDF(0,  0, 0),	0, 0x00EC, 0},
{REG_LPC_PCI_CFG,	REG_T_PCI, BDF(0, 31, 0),	0, 0x00F4, 0},
{REG_SMBUS_PCI_CFG,	REG_T_PCI, BDF(0, 31, 3),	0, 0x0040, 0},
{REG_TEST_DEV_PCI_CFG,	REG_T_PCI, BDF(0,  5, 0),	0, 0x0100, 0},
{REG_TEST_DEV_MMIO,	REG_T_MM,  TEST_MMIO_BASE,	0, 0x0400, 0},
{REG_TEST_DEV_IO,	REG_T_IO,  TEST_IO_BASE,	0, 0x0080, 0},

{REG_DMA_IO,		REG_T_IO, FIXED,	0, 0xFFFF, 0},
{REG_TIMER_IO,	REG_T_IO, FIXED,	0, 0xFFFF, 0},
{REG_PIC,		REG_T_IO, FIXED,	0, 0xFFFF, 0},
{REG_RTC,		REG_T_IO, FIXED,	0, 0xFFFF, 0},
{REG_PROC_IF,		REG_T_IO, FIXED,	0, 0xFFFF, 0},
{REG_APM_IO,		REG_T_IO, FIXED,	0, 0xFFFF, 0},
{REG_PM_IO,		REG_T_IO, PMBASE,	0, 0xFFFF, 0},
{REG_SMBUS_IO,	REG_T_IO, SMB_BASE,	0, 0xFFFF, 0},
};

/**
 * read pci configuration register.
 */
static int pdmp_pci_iocfg_read(unsigned int seg, unsigned int bdf,
		int reg, int len, u8 *value)
{
	outl(PCI_CONF_ADDR_BDF(bdf, reg), PCI_CONFIG_ADDRESS);
	switch (len) {
	case 1:
		*((u8 *)value) = inb(PCI_CONFIG_DATA + (reg & 3));
		break;
	case 2:
		*((u16 *)value) = inw(PCI_CONFIG_DATA + (reg & 2));
		break;
	case 4:
		*((u32 *)value) = inl(PCI_CONFIG_DATA);
		break;
	}
	return 0;
}

static int kdmp_pci_cfg_read(unsigned int seg, unsigned int bdf,
		int reg, int len, u8 *value)
{
	if (reg < 256)
		return pdmp_pci_iocfg_read(seg, bdf, reg, len, value);

	return -EINVAL;
}

static int pdmp_read_pcicfg(int regid, int offset, void *buf, int size)
{
	u8 *dst;
	unsigned long bdf;
	unsigned int seg = 0;
	int remain;
	int i, n;
	int reg;
	int pos = 0;
	struct pdmp_reg_def_t *def = &kdmp_reg_def[regid];

	dst = (u8 *)buf;
	bdf = def->base;
	seg = def->value;

	remain = size;
	n = remain >> 2;
	for (i = 0; i < n; ++i) {
		reg = offset + pos;
		kdmp_pci_cfg_read(seg, bdf, reg, 4, &dst[pos]);
		remain -= 4;
		pos += 4;
	}
	n = remain >> 1;
	for (i = 0; i < n; ++i) {
		reg = offset + pos;
		kdmp_pci_cfg_read(seg, bdf, reg, 2, &dst[pos]);
		remain -= 2;
		pos += 2;
	}
	n = remain >> 0;
	for (i = 0; i < n; ++i) {
		reg = offset + pos;
		kdmp_pci_cfg_read(seg, bdf, reg, 1, &dst[pos]);
		remain -= 1;
		pos += 1;
	}
	return 0;
}

/**
 * write pci configuration register is not supported.
 */

/**
 * read mamory mapped register.
 */
static int pdmp_read_memory(int regid, int offset, void *buf, int size)
{
	u8 *src;
	u8 *dst;
	int i = 0;
	int remain;
	struct pdmp_reg_def_t *reg = &kdmp_reg_def[regid];

	src = (u8 *)kdmp_get_mmap_addr(regid);
	if (src == NULL)
		return -1;

	src += offset - reg->base_offset;
	dst = (u8 *)buf;

	remain = size;
	while (remain >= 4) {
		*((u32 *)&dst[i]) = readl(src + i);
		remain -= 4;
		i += 4;
	}
	while (remain >= 2) {
		*((u16 *)&dst[i]) = readw(src + i);
		remain -= 2;
		i += 2;
	}
	while (remain > 0) {
		dst[i] = readb(src + i);
		remain--;
		i++;
	}

	return 0;
}

/**
 * write mamory mapped register.
 */
static int pdmp_write_memory(int regid, int offset, u32 data, int size)
{
	u8 *dst;
	struct pdmp_reg_def_t *reg = &kdmp_reg_def[regid];

	dst = (u8 *)kdmp_get_mmap_addr(regid);
	if (dst == NULL)
		return -1;

	dst += offset - reg->base_offset;

	/* write simple, not use 'memcpy'. */
	switch (size) {
	case 1:
		*(u8 *)dst = (u8)data;
		break;
	case 2:
		*(u16 *)dst = (u16)data;
		break;
	case 4:
		*(u32 *)dst = (u32)data;
		break;
	default:
		return -1;
	}
	return 0;
}

/**
 * read io register.
 */
static int pdmp_read_ioport(int regid, int offset, void *buf, int size)
{
	u8 *dst;
	int baid;
	unsigned long ba;
	struct pdmp_reg_def_t *reg = &kdmp_reg_def[regid];
	int i = 0;
	int remain;

	baid = reg->base;
	ba = kdmp_get_base_address(baid);
	if (ba == BASE_ADDR_NA)
		return -1;

	dst = (u8 *)buf;

	remain = size;
	while (remain >= 4) {
		*((u32 *)&dst[i]) = inl(ba + offset + i);
		remain -= 4;
		i += 4;
	}
	while (remain >= 2) {
		*((u16 *)&dst[i]) = inw(ba + offset + i);
		remain -= 2;
		i += 2;
	}
	while (remain > 0) {
		dst[i] = inb(ba + offset + i);
		remain--;
		i++;
	}

	return 0;
}
/**
 * write io register.
 */
static int pdmp_write_ioport(int regid, int offset, u32 data, int size)
{
	int baid;
	unsigned long ba;
	struct pdmp_reg_def_t *reg = &kdmp_reg_def[regid];

	baid = reg->base;
	ba = kdmp_get_base_address(baid);
	if (ba == BASE_ADDR_NA)
		return -1;


	switch (size) {
	case 1:
		outb((u8)data, ba + offset);
		break;
	case 2:
		outw((u16)data, ba + offset);
		break;
	case 4:
		outl((u32)data, ba + offset);
		break;
	default:
		return -1;
	}
	return 0;
}

/**
 * read register.
 */
int kdmp_read_reg(int regid, int offset, void *buf, int size)
{
	int result = -1;
	struct pdmp_reg_def_t *reg = &kdmp_reg_def[regid];
	switch (reg->type) {
	case REG_T_PCI:
		result = pdmp_read_pcicfg(regid, offset, buf, size);
		break;
	case REG_T_MM:
		result = pdmp_read_memory(regid, offset, buf, size);
		break;
	case REG_T_IO:
		result = pdmp_read_ioport(regid, offset, buf, size);
		break;
	}
	return result;
}

/**
 * write register. for index register.
 */
int kdmp_write_reg(int regid, int offset, u32 data, int size)
{
	int result = -1;
	struct pdmp_reg_def_t *reg = &kdmp_reg_def[regid];
	switch (reg->type) {
	case REG_T_MM:
		result = pdmp_write_memory(regid, offset, data, size);
		break;
	case REG_T_IO:
		result = pdmp_write_ioport(regid, offset, data, size);
		break;
	}
	return result;
}

/**
 * get memory mapped register address.
 */
static void *kdmp_get_mmap_addr(int regid)
{
	struct pdmp_reg_def_t *reg;
	void *virt = NULL;
	int baid;
	u32 ba;
	u32 phys;

	reg = &kdmp_reg_def[regid];
	virt = (void *)reg->value;
	if (virt != NULL)
		return virt;


	baid = reg->base;
	ba = kdmp_get_base_address(baid);
	if (ba == BASE_ADDR_NA)
		return NULL;

	phys = ba + reg->base_offset;
	virt = ioremap(phys, reg->size);
	if (virt == NULL) {
		printk(KERN_EMERG
			"kdmp: %s: regid=%d addr=%08x: ioremap failure\n",
			__func__, regid, phys);
	}
	reg->value = (unsigned long)virt;
	return virt;
}

/**
 * initialize registry information.
 */
int kdmp_init_reg_info(void)
{
	int n;
	int i;

	/* initialize base address. */
	n = ARRAY_SIZE(kdmp_base_addr);
	for (i = 0; i < n; ++i)
		kdmp_base_addr[i].ba = BASE_ADDR_NA;

	/* initialize virtual memory pointers. */
	n = ARRAY_SIZE(kdmp_reg_def);
	for (i = 0; i < n; ++i)
		kdmp_reg_def[i].value = 0;

	return 0;
}

/**
 * configure registry information.
 */
int kdmp_conf_reg_info(void)
{
	struct pdmp_reg_def_t *reg;
	int i;
	int n;
	u32 ba;
	void *mm;

	/* setup base address */
	for (i = BA_BEGIN; i < BA_END; ++i) {
		ba = kdmp_get_base_address(i);
		DBG("pdmp: config panicdump baid[%02d]=%08x\n", i, ba);
	}

	/* setup virtual memory for MMIO register sets. */
	n = ARRAY_SIZE(kdmp_reg_def);
	for (i = 0; i < n; ++i) {
		reg = &kdmp_reg_def[i];
		if (reg->type == REG_T_MM) {
			mm = kdmp_get_mmap_addr(reg->id);
			DBG("pdmp: config panicdump regid[%02d]=%p\n", i, mm);
		}
	}
	return 0;
}

/* end of kdmp_regs.c */
