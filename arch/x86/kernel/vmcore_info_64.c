// SPDX-License-Identifier: GPL-2.0-only

#include <linux/vmcore_info.h>
#include <linux/pgtable.h>

#include <asm/setup.h>
#include <asm/kdmp.h>

void arch_crash_save_vmcoreinfo(void)
{
	u64 sme_mask = sme_me_mask;

	VMCOREINFO_NUMBER(phys_base);
	VMCOREINFO_SYMBOL(init_top_pgt);
	vmcoreinfo_append_str("NUMBER(pgtable_l5_enabled)=%d\n",
			      pgtable_l5_enabled());

#ifdef CONFIG_NUMA
	VMCOREINFO_SYMBOL(node_data);
	VMCOREINFO_LENGTH(node_data, MAX_NUMNODES);
#endif
	vmcoreinfo_append_str("KERNELOFFSET=%lx\n", kaslr_offset());
	VMCOREINFO_NUMBER(KERNEL_IMAGE_SIZE);
	VMCOREINFO_NUMBER(sme_mask);

#if IS_ENABLED(CONFIG_CUSTOM_CRASHCUMP)
	VMCOREINFO_SYMBOL(kdmp_res);
	VMCOREINFO_SYMBOL(kdmp_active);
	VMCOREINFO_SYMBOL(kdmp_phys_base);
	VMCOREINFO_SYMBOL(kdmp_phys_size);
	VMCOREINFO_SYMBOL(kdmp_pdmp_primary);
	VMCOREINFO_SYMBOL(kdmp_pdmp_slot);
	VMCOREINFO_STRUCT_SIZE(pdmp_data_t);
	VMCOREINFO_OFFSET(pdmp_data_t, head);
	VMCOREINFO_OFFSET(pdmp_data_t, status);
	VMCOREINFO_OFFSET(pdmp_data_t, printk_buf);
	vmcoreinfo_append_str("NUMBER(KDMP_PHYS_BASE)=0x%llx\n",
			      (unsigned long long)kdmp_phys_base);
	vmcoreinfo_append_str("NUMBER(KDMP_PHYS_SIZE)=0x%llx\n",
			      (unsigned long long)kdmp_phys_size);
	vmcoreinfo_append_str("NUMBER(KDMP_SLOT_SIZE)=0x%llx\n",
			      (unsigned long long)PDMP_SZ_DATA);
	vmcoreinfo_append_str("NUMBER(KDMP_SLOT_COUNT)=%u\n", PDMP_N_CORE);
#endif
}
