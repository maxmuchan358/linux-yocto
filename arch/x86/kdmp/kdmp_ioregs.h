/*
 * kdmp_ioregs.h - I/O and memory-mapped I/O register definitions for panic dump
 */
#ifndef _KDMP_IOREGS_H_
#define _KDMP_IOREGS_H_

#include "kdmp_regid.h"

/**
 * PCI device read/write map.
 *
 * register and dump area mapping table.
 *
 * struct pdmp_reg_map_t {
 *	int regid;			// register identify
 *	unsigned long offset;	// read register offset
 *	unsigned long size;		// read/write data size
 *	unsigned long idx_offset;	// index register offset
 *	unsigned long idx_val;	// index data
 *	unsigned long pos;		// write area offset
 * } ;
 */
/* q35 + ICH9 dump layout used by the current QEMU test harness. */
struct pdmp_reg_map_t kdmp_pcireg_map[] = {
	{REG_HOST_DEV_CFG,	0, 0x0100, IDX_NON, 0, 0x0000},
	{REG_LPC_PCI_CFG,	0, 0x0100, IDX_NON, 0, 0x0100},
	{REG_SMBUS_PCI_CFG,	0, 0x0100, IDX_NON, 0, 0x0200},
	{REG_TEST_DEV_PCI_CFG,	0, 0x0100, IDX_NON, 0, 0x0300},
	{REG_NET_DEV_PCI_CFG,	0, 0x0100, IDX_NON, 0, 0x0400},
	{REG_STOR_DEV_PCI_CFG,	0, 0x0100, IDX_NON, 0, 0x0500},
};

/**
 * I/O and memory-mapped I/O device read/write map.
 *
 * register and dump area mapping table.
 *
 * struct pdmp_reg_map_t {
 *	int regid;			// register identify
 *	unsigned long offset;	// read register offset
 *	unsigned long size;		// read/write data size
 *	unsigned long idx_offset;	// index register offset
 *	unsigned long idx_val;	// index data
 *	unsigned long pos;		// write area offset
 * } ;
 */
struct pdmp_reg_map_t kdmp_ioreg_map[] = {
	/* DMA I/O Registers */
	{REG_DMA_IO, 0x0000, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x0001, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x0002, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x0003, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x0004, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x0005, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x0006, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x0007, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x0008, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x000F, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x0081, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x0082, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x0083, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x0087, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x0089, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x008A, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x008B, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x00C0, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x00C2, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x00C4, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x00C6, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x00C8, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x00CA, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x00CC, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x00CE, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x00D0, 1, IDX_NON, 0, 0},
	{REG_DMA_IO, 0x00DE, 1, IDX_NON, 0, 0},
	/* Timer I/O Registers */
	{REG_TIMER_IO, 0x0040, 1, IDX_NON, 0, 0},
	{REG_TIMER_IO, 0x0041, 1, IDX_NON, 0, 0},
	{REG_TIMER_IO, 0x0042, 1, IDX_NON, 0, 0},
	/* PIC Registers */
	{REG_PIC, 0x0021, 1, IDX_NON, 0, 0},
	{REG_PIC, 0x00A1, 1, IDX_NON, 0, 0},
	{REG_PIC, 0x04D0, 1, IDX_NON, 0, 0},
	{REG_PIC, 0x04D1, 1, IDX_NON, 0, 0},
	/* RTC I/O Registers */
	{REG_RTC, 0x0071, 1, 0x0070, 0x00, 0},
	{REG_RTC, 0x0071, 1, 0x0070, 0x01, 0},
	{REG_RTC, 0x0071, 1, 0x0070, 0x02, 0},
	{REG_RTC, 0x0071, 1, 0x0070, 0x03, 0},
	{REG_RTC, 0x0071, 1, 0x0070, 0x04, 0},
	{REG_RTC, 0x0071, 1, 0x0070, 0x05, 0},
	{REG_RTC, 0x0071, 1, 0x0070, 0x06, 0},
	{REG_RTC, 0x0071, 1, 0x0070, 0x07, 0},
	{REG_RTC, 0x0071, 1, 0x0070, 0x08, 0},
	{REG_RTC, 0x0071, 1, 0x0070, 0x09, 0},
	{REG_RTC, 0x0071, 1, 0x0070, 0x0A, 0},
	{REG_RTC, 0x0071, 1, 0x0070, 0x0B, 0},
	{REG_RTC, 0x0071, 1, 0x0070, 0x0C, 0},
	{REG_RTC, 0x0071, 1, 0x0070, 0x0D, 0},
	/* Processor Interface Registers */
	{REG_PROC_IF, 0x0061, 1, IDX_NON, 0, 0},
	{REG_PROC_IF, 0x0070, 1, IDX_NON, 0, 0},
	{REG_PROC_IF, 0x0092, 1, IDX_NON, 0, 0},
	{REG_PROC_IF, 0x0CF9, 1, IDX_NON, 0, 0},
	/* APM Register */
	{REG_APM_IO, 0x00B2, 1, IDX_NON, 0, 0},
	{REG_APM_IO, 0x00B3, 1, IDX_NON, 0, 0},
	/* Power Management I/O Registers */
	{REG_PM_IO, 0x0000, 2, IDX_NON, 0, 0},
	{REG_PM_IO, 0x0002, 2, IDX_NON, 0, 0},
	{REG_PM_IO, 0x0004, 4, IDX_NON, 0, 0},
	{REG_PM_IO, 0x0008, 4, IDX_NON, 0, 0},
	{REG_PM_IO, 0x0020, 8, IDX_NON, 0, 0},
	{REG_PM_IO, 0x0028, 8, IDX_NON, 0, 0},
	{REG_PM_IO, 0x0030, 4, IDX_NON, 0, 0},
	{REG_PM_IO, 0x0034, 4, IDX_NON, 0, 0},
	{REG_PM_IO, 0x0038, 2, IDX_NON, 0, 0},
	{REG_PM_IO, 0x003A, 2, IDX_NON, 0, 0},
	{REG_PM_IO, 0x0042, 1, IDX_NON, 0, 0},
	{REG_PM_IO, 0x0044, 2, IDX_NON, 0, 0},
	{REG_PM_IO, 0x0050, 1, IDX_NON, 0, 0},
	{REG_PM_IO, 0x005C, 2, IDX_NON, 0, 0},
	{REG_PM_IO, 0x005E, 2, IDX_NON, 0, 0},
	/* ICH9 SMBus I/O Registers */
	{REG_SMBUS_IO, 0x0000, 0x0010, IDX_NON, 0, 0},
	/* custom-crashdump-test BAR0 MMIO */
	{REG_TEST_DEV_MMIO, 0x0000, 0x0400, IDX_NON, 0, 0x0080},
	/* custom-crashdump-test BAR1 PIO */
	{REG_TEST_DEV_IO, 0x0000, 0x0080, IDX_NON, 0, 0x0480},
	/* custom-crashdump-net BAR0 MMIO */
	{REG_NET_DEV_MMIO, 0x0000, 0x0200, IDX_NON, 0, 0x0500},
	/* custom-crashdump-net BAR1 PIO */
	{REG_NET_DEV_IO, 0x0000, 0x0040, IDX_NON, 0, 0x0700},
	/* custom-crashdump-stor BAR0 MMIO */
	{REG_STOR_DEV_MMIO, 0x0000, 0x0400, IDX_NON, 0, 0x0740},
};

#endif /* _KDMP_IOREGS_H_ */

/* end of kdmp_ioregs.h */