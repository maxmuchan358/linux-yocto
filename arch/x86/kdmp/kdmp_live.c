// SPDX-License-Identifier: GPL-2.0
/*
 * kdmp_live.c - live pt_regs capture and procfs export for pre-crash checks
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/compiler.h>
#include <linux/jiffies.h>
#include <linux/string.h>

#include <asm/kdmp.h>
#include "kdmp_local.h"

#define KDMP_LIVE_PROC_NAME "kdmp_live_ptregs"

static struct proc_dir_entry *kdmp_live_proc;

static const char *kdmp_event_name(u32 source)
{
	switch (source) {
	case KDMP_EVENT_EXCEPTION:
		return "EXCEPTION";
	case KDMP_EVENT_PAGE_FAULT:
		return "PAGE_FAULT";
	case KDMP_EVENT_IRQ:
		return "IRQ";
	case KDMP_EVENT_NMI:
		return "NMI";
	case KDMP_EVENT_IPI:
		return "IPI";
	case KDMP_EVENT_PANIC:
		return "PANIC";
	case KDMP_EVENT_DIRECT_PANIC:
		return "DIRECT_PANIC";
	case KDMP_EVENT_SYSVEC:
		return "SYSVEC";
	default:
		return "UNKNOWN";
	}
}

static struct kdmp_live_ring_t *kdmp_live_ring(struct kdmp_data_t *dmpbuf)
{
	return (struct kdmp_live_ring_t *)dmpbuf->rsv1;
}

static void kdmp_live_ring_init(struct kdmp_live_ring_t *ring)
{
	if (READ_ONCE(ring->magic) == KDMP_LIVE_MAGIC &&
	    READ_ONCE(ring->version) == KDMP_LIVE_VERSION)
		return;

	memset(ring, 0, sizeof(*ring));
	ring->magic = KDMP_LIVE_MAGIC;
	ring->version = KDMP_LIVE_VERSION;
	ring->entry_size = sizeof(struct kdmp_live_event_t);
	ring->entry_count = KDMP_LIVE_NR_EVENTS;
}

void kdmp_live_capture(struct pt_regs *regs, u32 source, u32 id,
		       unsigned long data)
{
	struct kdmp_data_t *dmpbuf;
	struct kdmp_live_ring_t *ring;
	struct kdmp_live_event_t *ev;
	u64 seq;
	u32 idx;

	if (!regs)
		return;

	dmpbuf = kdmp_current_dump_region();
	if (!dmpbuf)
		return;

	ring = kdmp_live_ring(dmpbuf);
	kdmp_live_ring_init(ring);

	seq = READ_ONCE(ring->write_seq);
	idx = (u32)(seq % KDMP_LIVE_NR_EVENTS);
	ev = &ring->events[idx];

	WRITE_ONCE(ev->committed, 0);
	barrier();

	ev->cpu = raw_smp_processor_id();
	ev->source = source;
	ev->id = id;
	ev->timestamp = jiffies_64;
	ev->data = data;
	ev->ip = regs->ip;
	ev->sp = regs->sp;
	ev->flags = regs->flags;
	memcpy(&ev->regs, regs, sizeof(ev->regs));

	smp_wmb();
	WRITE_ONCE(ev->committed, (u32)(seq + 1));
	WRITE_ONCE(ring->write_seq, seq + 1);

	if ((seq + 1) > KDMP_LIVE_NR_EVENTS)
		ring->dropped++;
}

static int kdmp_live_proc_show(struct seq_file *m, void *v)
{
	int cpu;

	for (cpu = 0; cpu < PDMP_N_CORE; cpu++) {
		struct kdmp_data_t *dmpbuf;
		struct kdmp_live_ring_t *ring;
		u64 write_seq;
		u64 start;
		u64 i;

		dmpbuf = kdmp_kdmp_slot[cpu];
		if (!dmpbuf) {
			seq_printf(m, "cpu=%d: slot unavailable\n", cpu);
			continue;
		}

		ring = kdmp_live_ring(dmpbuf);
		if (READ_ONCE(ring->magic) != KDMP_LIVE_MAGIC) {
			seq_printf(m, "cpu=%d: live ring empty\n", cpu);
			continue;
		}

		write_seq = READ_ONCE(ring->write_seq);
		start = (write_seq > 8) ? (write_seq - 8) : 0;

		seq_printf(m,
			   "cpu=%d write_seq=%llu dropped=%llu entry_count=%u\n",
			   cpu, (unsigned long long)write_seq,
			   (unsigned long long)READ_ONCE(ring->dropped), ring->entry_count);

		for (i = start; i < write_seq; i++) {
			u32 idx = (u32)(i % KDMP_LIVE_NR_EVENTS);
			struct kdmp_live_event_t *ev = &ring->events[idx];
			u32 committed = READ_ONCE(ev->committed);

			if (committed != (u32)(i + 1))
				continue;

			seq_printf(m,
				   "  seq=%llu src=%s(%u) id=%u data=0x%lx ip=0x%llx sp=0x%llx flags=0x%llx ts=%llu\n",
				   (unsigned long long)(i + 1),
				   kdmp_event_name(ev->source), ev->source,
				   ev->id, (unsigned long)ev->data,
				   (unsigned long long)ev->ip,
				   (unsigned long long)ev->sp,
				   (unsigned long long)ev->flags,
				   (unsigned long long)ev->timestamp);
		}
	}

	return 0;
}

int kdmp_live_init(void)
{
#if IS_ENABLED(CONFIG_PROC_FS)
	if (!kdmp_live_proc)
		kdmp_live_proc = proc_create_single(KDMP_LIVE_PROC_NAME, 0444, NULL,
					    kdmp_live_proc_show);
	if (!kdmp_live_proc)
		pr_warn("kdmp: failed to create /proc/%s\n", KDMP_LIVE_PROC_NAME);
#endif
	return 0;
}
