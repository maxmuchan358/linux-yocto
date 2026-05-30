/*
 * kdmp_msrs.h - model-specific register definitions for panic dump
 */
#ifndef _KDMP_MSRS_H_
#define _KDMP_MSRS_H_

#define CPUID_SUBLEAF_NA	0xFFFFFFFF

struct kdmp_cpuid_def_t kdmp_cpuid_defs[] = {
	{0x00000000, CPUID_SUBLEAF_NA},
	{0x00000001, CPUID_SUBLEAF_NA},
	{0x00000007, 0x00000000},
	{0x0000000B, 0x00000000},
	{0x0000000B, 0x00000001},
	{0x0000000D, 0x00000000},
	{0x0000000D, 0x00000001},
	/* Extended Function CPUID Information */
	{0x80000000, CPUID_SUBLEAF_NA},
	{0x80000001, CPUID_SUBLEAF_NA},
	{0x80000008, CPUID_SUBLEAF_NA},
};

/* model-specific register definitions */
struct kdmp_msr_def_t kdmp_msr_defs[] = {
	{ 0x00000010 }, /* IA32_TIME_STAMP_COUNTER */
	{ 0x0000001B }, /* IA32_APIC_BASE */
	{ 0x00000174 }, /* IA32_SYSENTER_CS */
	{ 0x00000175 }, /* IA32_SYSENTER_ESP */
	{ 0x00000176 }, /* IA32_SYSENTER_EIP */
	{ 0x00000277 }, /* IA32_PAT */
	{ 0x0000038D }, /* IA32_FIXED_CTR_CTRL */
	{ 0x0000038E }, /* IA32_PERF_GLOBAL_STATUS */
	{ 0x0000038F }, /* IA32_PERF_GLOBAL_CTRL */
	{ 0xC0000080 }, /* IA32_EFER */
	{ 0xC0000081 }, /* IA32_STAR */
	{ 0xC0000082 }, /* IA32_LSTAR */
	{ 0xC0000084 }, /* IA32_FMASK */
	{ 0xC0000100 }, /* IA32_FS_BASE */
	{ 0xC0000101 }, /* IA32_GS_BASE */
	{ 0xC0000102 }, /* IA32_KERNEL_GS_BASE */
	{ 0xC0000103 }, /* IA32_TSC_AUX */
};

struct kdmp_msr_def_t kdmp_mcmsr_defs[] = {
	{ 0x00000179 }, /* IA32_MCG_CAP */
	{ 0x0000017A }, /* IA32_MCG_STATUS */
	{ 0x0000017B }, /* IA32_MCG_CTL */
};

#endif /* _KDMP_MSRS_H_ */

/* end of kdmp_msrs.h */