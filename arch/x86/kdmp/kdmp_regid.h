/*
 * kdmp_regid.h - register region id for panic dump
 */
#ifndef _KDMP_REGID_H_
#define _KDMP_REGID_H_

/* register regions id */
enum {
	REG_HOST_DEV_CFG,
	REG_LPC_PCI_CFG,
	REG_SMBUS_PCI_CFG,
	REG_TEST_DEV_PCI_CFG,
	REG_NET_DEV_PCI_CFG,
	REG_STOR_DEV_PCI_CFG,
	REG_TEST_DEV_MMIO,
	REG_TEST_DEV_IO,
	REG_NET_DEV_MMIO,
	REG_NET_DEV_IO,
	REG_STOR_DEV_MMIO,

	REG_DMA_IO,
	REG_TIMER_IO,
	REG_PIC,
	REG_RTC,
	REG_PROC_IF,
	REG_APM_IO,
	REG_PM_IO,
	REG_SMBUS_IO,
};

#endif /* _KDMP_REGID_H_ */

/* end of kdmp_regid.h */
