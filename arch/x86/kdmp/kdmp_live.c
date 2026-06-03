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

/* Ensure the ring fits inside rsv1 at compile time. */
static_assert(sizeof(struct kdmp_live_ring_t) <= PDMP_SZ_DATA_RSV1);

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
	smp_wmb();
	WRITE_ONCE(ring->write_seq, seq + 1);

	if (seq >= KDMP_LIVE_NR_EVENTS)
		WRITE_ONCE(ring->dropped, READ_ONCE(ring->dropped) + 1);
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
		start = (write_seq > KDMP_LIVE_NR_EVENTS) ?
			(write_seq - KDMP_LIVE_NR_EVENTS) : 0;

		seq_printf(m,
			   "cpu=%d write_seq=%llu dropped=%llu entry_count=%u\n",
			   cpu, (unsigned long long)write_seq,
			   (unsigned long long)READ_ONCE(ring->dropped),
			   KDMP_LIVE_NR_EVENTS);

		for (i = start; i < write_seq; i++) {
			u32 idx = (u32)(i % KDMP_LIVE_NR_EVENTS);
			struct kdmp_live_event_t *ev = &ring->events[idx];
			u32 committed;
			u32 src, id_val;
			unsigned long data_val;
			unsigned long long ip_val, sp_val, flags_val, ts_val;

			committed = READ_ONCE(ev->committed);
			if (committed != (u32)(i + 1))
				continue;

			smp_rmb();

			src      = ev->source;
			id_val   = ev->id;
			data_val = ev->data;
			ip_val   = ev->ip;
			sp_val   = ev->sp;
			flags_val = ev->flags;
			ts_val   = ev->timestamp;

			smp_rmb();

			/* discard if the slot was overwritten while we read */
			if (READ_ONCE(ev->committed) != committed)
				continue;

			seq_printf(m,
				   "  seq=%llu src=%s(%u) id=%u data=0x%lx ip=0x%llx sp=0x%llx flags=0x%llx ts=%llu\n",
				   (unsigned long long)(i + 1),
				   kdmp_event_name(src), src,
				   id_val, data_val,
				   ip_val, sp_val, flags_val, ts_val);
		}
	}

	return 0;
}

int kdmp_live_init(void)
{
	int cpu;

	/* Initialise each per-CPU ring once here so kdmp_live_capture
	 * never calls memset on the hot path. */
	for (cpu = 0; cpu < PDMP_N_CORE; cpu++) {
		struct kdmp_data_t *dmpbuf = kdmp_kdmp_slot[cpu];

		if (dmpbuf)
			kdmp_live_ring_init(kdmp_live_ring(dmpbuf));
	}

#if IS_ENABLED(CONFIG_PROC_FS)
	if (!kdmp_live_proc)
		kdmp_live_proc = proc_create_single(KDMP_LIVE_PROC_NAME, 0444, NULL,
					    kdmp_live_proc_show);
	if (!kdmp_live_proc)
		pr_warn("kdmp: failed to create /proc/%s\n", KDMP_LIVE_PROC_NAME);
#endif
	return 0;
}

void kdmp_live_fini(void)
{
#if IS_ENABLED(CONFIG_PROC_FS)
	if (kdmp_live_proc) {
		proc_remove(kdmp_live_proc);
		kdmp_live_proc = NULL;
	}
#endif
}
