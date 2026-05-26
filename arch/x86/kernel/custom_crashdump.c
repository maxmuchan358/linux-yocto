// SPDX-License-Identifier: GPL-2.0-only

#include <linux/elf.h>
#include <linux/crash_dump.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/mm.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <linux/ptrace.h>
#include <linux/types.h>
#include <linux/vmcore_info.h>

#include <asm/asm.h>
#include <asm/crash.h>

#define CUSTOM_CRASH_NOTE_NAME "X86CUSTOM"
#define CUSTOM_CRASH_NOTE_TYPE 0x58434e4d
#define CUSTOM_CRASH_NOTE_MAGIC 0x4344564d
#define CUSTOM_CRASH_NOTE_VERSION 1
#define CUSTOM_CRASH_NOTE_BYTES (1024 * 1024)
#define CUSTOM_CRASH_NOTE_NAME_BYTES ALIGN(sizeof(CUSTOM_CRASH_NOTE_NAME), 4)
#define CUSTOM_MAX_CAPTURE_CPUS 4
#define CUSTOM_CONTEXT_SLOT_COUNT 4

enum custom_crashdump_slot_index {
	CUSTOM_SLOT_EXCEPTION,
	CUSTOM_SLOT_NMI,
	CUSTOM_SLOT_DIRECT_PANIC,
	CUSTOM_SLOT_IPI,
};

struct custom_crashdump_header {
	u32 magic;
	u32 version;
	u32 total_size;
	u32 valid_cpu_count;
	u32 valid_context_count;
	u32 context_entry_size;
	u32 context_entry_count;
	u32 gprs_entry_size;
	u32 excp_entry_size;
	u32 text_offset;
	u32 text_len;
};

struct custom_crashdump_gprs {
	unsigned long bx;
	unsigned long cx;
	unsigned long dx;
	unsigned long si;
	unsigned long di;
	unsigned long bp;
	unsigned long ax;
	unsigned long sp;
	unsigned long ip;
	unsigned long flags;
	unsigned short cs;
	unsigned short ss;
#ifdef CONFIG_X86_32
	unsigned short ds;
	unsigned short es;
	unsigned short fs;
	unsigned short gs;
#else
	unsigned short reserved0;
	unsigned short reserved1;
	unsigned short reserved2;
	unsigned short reserved3;
	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;
	unsigned long r12;
	unsigned long r13;
	unsigned long r14;
	unsigned long r15;
#endif
};

struct custom_crashdump_context_slot {
	u32 cpu_id;
	u32 source;
	u32 valid;
	u32 gprs_valid;
	u32 excp_valid;
	struct custom_crashdump_gprs gprs;
	struct pt_regs excp_gprs;
};

struct custom_crashdump_note_prefix {
	struct elf_note note;
	char name[CUSTOM_CRASH_NOTE_NAME_BYTES];
	struct custom_crashdump_header header;
	struct custom_crashdump_context_slot
		context[CUSTOM_MAX_CAPTURE_CPUS][CUSTOM_CONTEXT_SLOT_COUNT];
};

struct custom_crashdump_note_layout {
	struct custom_crashdump_note_prefix prefix;
	u8 text[CUSTOM_CRASH_NOTE_BYTES - sizeof(struct custom_crashdump_note_prefix)];
};

static void *custom_crash_note_storage;
static phys_addr_t custom_crash_note_storage_phys;
static struct notifier_block custom_crashdump_panic_nb;

static_assert(sizeof(struct custom_crashdump_note_layout) == CUSTOM_CRASH_NOTE_BYTES);

static struct custom_crashdump_note_layout *custom_crash_layout(void)
{
	return custom_crash_note_storage;
}

static struct custom_crashdump_header *custom_crash_header(void)
{
	struct custom_crashdump_note_layout *layout = custom_crash_layout();

	if (!layout)
		return NULL;

	return &layout->prefix.header;
}

static unsigned int custom_crashdump_slot_index(u32 source)
{
	switch (source) {
	case CUSTOM_CONTEXT_SOURCE_PANIC:
		return CUSTOM_SLOT_EXCEPTION;
	case CUSTOM_CONTEXT_SOURCE_DIRECT_PANIC:
		return CUSTOM_SLOT_DIRECT_PANIC;
	case CUSTOM_CONTEXT_SOURCE_NMI:
		return CUSTOM_SLOT_NMI;
	case CUSTOM_CONTEXT_SOURCE_IPI:
		return CUSTOM_SLOT_IPI;
	case CUSTOM_CONTEXT_SOURCE_EXCEPTION:
	default:
		return CUSTOM_SLOT_EXCEPTION;
	}
}

static void custom_crashdump_capture_gprs(struct custom_crashdump_gprs *gprs)
{
	memset(gprs, 0, sizeof(*gprs));

	asm volatile("mov %%" _ASM_BX ",%0" : "=m"(gprs->bx));
	asm volatile("mov %%" _ASM_CX ",%0" : "=m"(gprs->cx));
	asm volatile("mov %%" _ASM_DX ",%0" : "=m"(gprs->dx));
	asm volatile("mov %%" _ASM_SI ",%0" : "=m"(gprs->si));
	asm volatile("mov %%" _ASM_DI ",%0" : "=m"(gprs->di));
	asm volatile("mov %%" _ASM_BP ",%0" : "=m"(gprs->bp));
	asm volatile("mov %%" _ASM_AX ",%0" : "=m"(gprs->ax));
	asm volatile("mov %%" _ASM_SP ",%0" : "=m"(gprs->sp));
#ifdef CONFIG_X86_64
	asm volatile("mov %%r8,%0" : "=m"(gprs->r8));
	asm volatile("mov %%r9,%0" : "=m"(gprs->r9));
	asm volatile("mov %%r10,%0" : "=m"(gprs->r10));
	asm volatile("mov %%r11,%0" : "=m"(gprs->r11));
	asm volatile("mov %%r12,%0" : "=m"(gprs->r12));
	asm volatile("mov %%r13,%0" : "=m"(gprs->r13));
	asm volatile("mov %%r14,%0" : "=m"(gprs->r14));
	asm volatile("mov %%r15,%0" : "=m"(gprs->r15));
#endif
	asm volatile("mov %%ss,%0" : "=r"(gprs->ss));
	asm volatile("mov %%cs,%0" : "=r"(gprs->cs));
#ifdef CONFIG_X86_32
	asm volatile("mov %%ds,%0" : "=r"(gprs->ds));
	asm volatile("mov %%es,%0" : "=r"(gprs->es));
	asm volatile("mov %%fs,%0" : "=r"(gprs->fs));
	asm volatile("mov %%gs,%0" : "=r"(gprs->gs));
#endif
	asm volatile("pushf\n\tpop %0" : "=m"(gprs->flags));
	gprs->ip = _THIS_IP_;
}

static struct custom_crashdump_context_slot *custom_crash_context_slot(unsigned int cpu,
							     u32 source)
{
	struct custom_crashdump_note_layout *layout = custom_crash_layout();

	if (!layout || cpu >= CUSTOM_MAX_CAPTURE_CPUS)
		return NULL;

	return &layout->prefix.context[cpu][custom_crashdump_slot_index(source)];
}

static bool custom_crashdump_slot_valid(unsigned int cpu, u32 source)
{
	struct custom_crashdump_context_slot *slot;

	slot = custom_crash_context_slot(cpu, source);
	return slot && slot->valid;
}

static bool custom_crashdump_exception_valid(unsigned int cpu)
{
	struct custom_crashdump_context_slot *slot;

	slot = custom_crash_context_slot(cpu, CUSTOM_CONTEXT_SOURCE_EXCEPTION);
	return slot && slot->excp_valid;
}

static bool custom_crashdump_resolve_source(unsigned int cpu, u32 *source)
{
	if (!source)
		return false;

	switch (*source) {
	case CUSTOM_CONTEXT_SOURCE_PANIC:
		if (custom_crashdump_slot_valid(cpu, CUSTOM_CONTEXT_SOURCE_NMI))
			return false;
		if (!custom_crashdump_exception_valid(cpu))
			*source = CUSTOM_CONTEXT_SOURCE_DIRECT_PANIC;
		break;
	case CUSTOM_CONTEXT_SOURCE_IPI:
		if (custom_crashdump_slot_valid(cpu, CUSTOM_CONTEXT_SOURCE_NMI))
			return false;
		break;
	default:
		break;
	}

	return true;
}

static void custom_crashdump_prepare_slot(struct custom_crashdump_context_slot *slot,
						  unsigned int cpu, u32 source)
{
	if (!slot)
		return;

	if (!slot->valid ||
	    custom_crashdump_slot_index(slot->source) != custom_crashdump_slot_index(source))
		memset(slot, 0, sizeof(*slot));

	slot->cpu_id = cpu;
	slot->source = source;
	slot->valid = 1;
}

static unsigned int custom_crashdump_valid_cpu_count(void)
{
	struct custom_crashdump_note_layout *layout = custom_crash_layout();
	unsigned int count = 0;
	unsigned int cpu;
	unsigned int slot_index;

	if (!layout)
		return 0;

	for (cpu = 0; cpu < CUSTOM_MAX_CAPTURE_CPUS; cpu++) {
		for (slot_index = 0; slot_index < CUSTOM_CONTEXT_SLOT_COUNT; slot_index++) {
			if (layout->prefix.context[cpu][slot_index].valid) {
				count++;
				break;
			}
		}
	}

	return count;
}

static unsigned int custom_crashdump_valid_context_count(void)
{
	struct custom_crashdump_note_layout *layout = custom_crash_layout();
	unsigned int count = 0;
	unsigned int cpu;
	unsigned int slot_index;

	if (!layout)
		return 0;

	for (cpu = 0; cpu < CUSTOM_MAX_CAPTURE_CPUS; cpu++) {
		for (slot_index = 0; slot_index < CUSTOM_CONTEXT_SLOT_COUNT; slot_index++) {
			if (layout->prefix.context[cpu][slot_index].valid)
				count++;
		}
	}

	return count;
}

static void custom_crashdump_fill_text(void)
{
	struct custom_crashdump_note_layout *layout = custom_crash_layout();
	struct custom_crashdump_header *hdr = custom_crash_header();
	static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	size_t text_size;
	size_t index;

	if (!layout || !hdr)
		return;

	text_size = sizeof(layout->text);
	memset(layout->text, 0, sizeof(layout->text));

	for (index = 0; index < text_size; index++)
		layout->text[index] = alphabet[index % (sizeof(alphabet) - 1)];

	hdr->text_len = text_size;
}

static void custom_crashdump_prepare_note(void)
{
	struct custom_crashdump_note_layout *layout = custom_crash_layout();
	struct elf_note *note;
	struct custom_crashdump_header *hdr;

	if (!layout)
		return;

	note = &layout->prefix.note;
	hdr = &layout->prefix.header;

	memset(note, 0, sizeof(*note));
	memset(layout->prefix.name, 0, sizeof(layout->prefix.name));
	memset(hdr, 0, sizeof(*hdr));

	note->n_namesz = sizeof(CUSTOM_CRASH_NOTE_NAME);
	note->n_descsz = sizeof(*layout) -
		offsetof(struct custom_crashdump_note_layout, prefix.header);
	note->n_type = CUSTOM_CRASH_NOTE_TYPE;
	memcpy(layout->prefix.name, CUSTOM_CRASH_NOTE_NAME,
	       sizeof(CUSTOM_CRASH_NOTE_NAME));

	hdr->magic = CUSTOM_CRASH_NOTE_MAGIC;
	hdr->version = CUSTOM_CRASH_NOTE_VERSION;
	hdr->total_size = note->n_descsz;
	hdr->valid_cpu_count = custom_crashdump_valid_cpu_count();
	hdr->valid_context_count = custom_crashdump_valid_context_count();
	hdr->context_entry_size = sizeof(struct custom_crashdump_context_slot);
	hdr->context_entry_count =
		CUSTOM_MAX_CAPTURE_CPUS * CUSTOM_CONTEXT_SLOT_COUNT;
	hdr->gprs_entry_size = sizeof(struct custom_crashdump_gprs);
	hdr->excp_entry_size = sizeof(struct pt_regs);
	hdr->text_offset = offsetof(struct custom_crashdump_note_layout, text) -
		offsetof(struct custom_crashdump_note_layout, prefix.header);

	custom_crashdump_fill_text();
}

static int custom_crashdump_panic_notify(struct notifier_block *nb,
					 unsigned long event, void *ptr)
{
	custom_crashdump_prepare_note();
	return NOTIFY_DONE;
}

void custom_crashdump_save_cpu(struct pt_regs *regs, int cpu, u32 source)
{
	struct custom_crashdump_context_slot *slot;

	if (!regs || cpu < 0 || cpu >= CUSTOM_MAX_CAPTURE_CPUS)
		return;
	if (!custom_crashdump_resolve_source(cpu, &source))
		return;

	slot = custom_crash_context_slot(cpu, source);
	if (!slot)
		return;

	custom_crashdump_prepare_slot(slot, cpu, source);
	slot->excp_valid = 1;
	memcpy(&slot->excp_gprs, regs, sizeof(*regs));
}

void custom_crashdump_save_gprs(int cpu, u32 source)
{
	struct custom_crashdump_context_slot *slot;

	if (cpu < 0 || cpu >= CUSTOM_MAX_CAPTURE_CPUS)
		return;
	if (!custom_crashdump_resolve_source(cpu, &source))
		return;

	slot = custom_crash_context_slot(cpu, source);
	if (!slot)
		return;

	custom_crashdump_prepare_slot(slot, cpu, source);
	slot->gprs_valid = 1;
	custom_crashdump_capture_gprs(&slot->gprs);
}

void custom_crash_save_vmcoreinfo_late(void)
{
	struct custom_crashdump_header *hdr = custom_crash_header();

	if (!hdr || !custom_crash_note_storage_phys)
		return;

	vmcoreinfo_append_str("CUSTOM_NOTE_NAME=%s\n", CUSTOM_CRASH_NOTE_NAME);
	vmcoreinfo_append_str("CUSTOM_NOTE_TYPE=0x%x\n", CUSTOM_CRASH_NOTE_TYPE);
	vmcoreinfo_append_str("CUSTOM_NOTE_PADDR=0x%llx\n",
			     (unsigned long long)custom_crash_note_paddr());
	vmcoreinfo_append_str("CUSTOM_NOTE_RESERVED_SIZE=%u\n",
			     CUSTOM_CRASH_NOTE_BYTES);
	vmcoreinfo_append_str("CUSTOM_NOTE_DESC_SIZE=%u\n", hdr->total_size);
	vmcoreinfo_append_str("CUSTOM_CPU_COUNT=%u\n", hdr->valid_cpu_count);
	vmcoreinfo_append_str("CUSTOM_CONTEXT_COUNT=%u\n",
			     hdr->valid_context_count);
	vmcoreinfo_append_str("CUSTOM_GPRS_SIZE=%u\n", hdr->gprs_entry_size);
	vmcoreinfo_append_str("CUSTOM_EXCP_GPRS_SIZE=%u\n",
			     hdr->excp_entry_size);
	vmcoreinfo_append_str("CUSTOM_TEXT_LEN=%u\n", hdr->text_len);
}

phys_addr_t custom_crash_note_paddr(void)
{
	return custom_crash_note_storage_phys;
}

size_t custom_crash_note_reserved_size(void)
{
	if (!custom_crash_note_storage_phys)
		return 0;

	return CUSTOM_CRASH_NOTE_BYTES;
}

static int __init custom_crashdump_alloc_buffer(void)
{
	unsigned long count;
	unsigned long index;
	struct page *page;
	void *vaddr;

	vaddr = alloc_pages_exact(CUSTOM_CRASH_NOTE_BYTES,
				  GFP_KERNEL | __GFP_ZERO);
	if (!vaddr)
		return -ENOMEM;

	count = PAGE_ALIGN(CUSTOM_CRASH_NOTE_BYTES) / PAGE_SIZE;
	page = virt_to_page(vaddr);
	for (index = 0; index < count; index++)
		mark_page_reserved(page + index);

	custom_crash_note_storage = vaddr;
	custom_crash_note_storage_phys = virt_to_phys(vaddr);
	return 0;
}

static int __init custom_crashdump_init(void)
{
	int ret;

	if (is_kdump_kernel())
		return 0;

	ret = custom_crashdump_alloc_buffer();
	if (ret) {
		pr_warn("custom crashdump: failed to allocate reserved buffer\n");
		return ret;
	}

	custom_crashdump_panic_nb.notifier_call = custom_crashdump_panic_notify;
	crash_kexec_post_notifiers = true;
	custom_crashdump_prepare_note();

	pr_info("custom crashdump: reserved buffer at 0x%llx size 0x%x\n",
		(unsigned long long)custom_crash_note_storage_phys,
		CUSTOM_CRASH_NOTE_BYTES);

	return atomic_notifier_chain_register(&panic_notifier_list,
					      &custom_crashdump_panic_nb);
}
early_initcall(custom_crashdump_init);