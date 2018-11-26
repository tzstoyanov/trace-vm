/* SPDX-License-Identifier: LGPL-2.1 */

/*
 * Copyright (C) 2017 VMware Inc, Yordan Karadzhov <y.karadz@gmail.com>
 */

/**
 *  @file    rename_sched_events.c
 *  @brief   A plugin to deal with renamed threads.
 */

#ifndef _KS_PLUGIN_SHED_RENAME_H
#define _KS_PLUGIN_SHED_RENAME_H

// KernelShark
#include "libkshark.h"
#include "libkshark-model.h"

/** Structure representing a plugin-specific context. */
struct plugin_rename_context {
	/** Stream identifier of the Monitor data. */
	uint8_t		monitor_stream_id;

	/** Pointer to the kvm_entry_event object. */
	struct tep_event_format	*kvm_entry_event;

	/** Pointer to the sched_switch_next_field format descriptor. */
	struct tep_format_field	*kvm_vcpu_id_field;

	
};

/** Plugin context instances. */
static struct plugin_rename_context *
plugin_context_handler[KS_MAX_NUM_STREAMS] = {NULL};

static void plugin_close(int sd)
{
	free(plugin_context_handler[sd]);
	plugin_context_handler[sd] = NULL;
}

// static void free_plugin_context()
// {
// 	int i;
// 
// 	for (i = 0; i < KS_MAX_NUM_STREAMS; ++i)
// 		plugin_close(i);
// }

static bool
plugin_update_stream_context(struct kshark_context *kshark_ctx, int sd)
{
	struct plugin_rename_context *plugin_ctx;
	struct tep_event_format *event;
	struct kshark_data_stream *stream;

// 	printf("#### plugin_update_stream_context %i %p: %p %p\n", sd, plugin_context_handler, plugin_context_handler[0], plugin_context_handler[1]);

	stream = kshark_get_data_stream(kshark_ctx, sd);
	if (!stream)
		return false;

	plugin_ctx = malloc(sizeof(*plugin_ctx));
	printf("plugin_update_stream_context %i  %p\n", sd, plugin_ctx);
	plugin_ctx->handle = stream->handle;
	plugin_ctx->pevent = stream->pevent;

	event = tep_find_event_by_name(plugin_ctx->pevent,
				       "sched", "sched_switch");
	if (!event)
		return false;

	plugin_ctx->sched_switch_event = event;
	plugin_ctx->sched_switch_next_field =
		tep_find_any_field(event, "next_pid");

	plugin_ctx->sched_switch_comm_field =
		tep_find_field(event, "next_comm");

	plugin_ctx->done = false;
	plugin_context_handler[sd] = plugin_ctx;
	printf("plugin_update_stream_context %i done %p: %p %p\n", sd, plugin_context_handler, plugin_context_handler[0], plugin_context_handler[1]);
	return true;
}

static bool plugin_update_context(struct kshark_context *kshark_ctx)
{
	int *stream_ids, i;

	stream_ids = kshark_all_streams(kshark_ctx);
	for (i = 0; i < kshark_ctx->n_streams; ++i) {
		if (!plugin_update_stream_context(kshark_ctx, stream_ids[i]))
			goto fail;
	}

	return true;

 fail:
	free_plugin_context();

	return false;
}

static void plugin_kvm_action(struct kshark_context *kshark_ctx,
			      struct tep_record *rec,
			      struct kshark_entry *entry)
{}

static int plugin_get_next_pid(struct tep_record *record, int sd)
{
	unsigned long long val;
	struct plugin_rename_context *plugin_ctx =
		plugin_context_handler[sd];

	tep_read_number_field(plugin_ctx->sched_switch_next_field,
			      record->data, &val);
	return val;
}

static bool plugin_sched_switch_match_pid(struct kshark_context *kshark_ctx,
					  struct kshark_entry *e,
					  int sd, int pid)
{
	struct plugin_rename_context *plugin_ctx =
		plugin_context_handler[e->stream_id];
	struct tep_record *record = NULL;
	int switch_pid;

	if (plugin_ctx->sched_switch_event &&
	    e->stream_id == sd &&
	    e->event_id == plugin_ctx->sched_switch_event->id) {
		if (e->event_id == KS_EVENT_OVERFLOW)
			return false;

		record = kshark_read_at(kshark_ctx, sd, e->offset);
		if (!record) {
			printf("%i %i %lu\n", sd, e->event_id, e->offset);
			return false;
		}

		switch_pid = plugin_get_next_pid(record, sd);
		free(record);

		if (switch_pid == pid)
			return true;
	}

	return false;
}

static void kvm_draw_nop(struct kshark_cpp_argv *argv,
			 int sd, int pid, int draw_action)
{}

static int plugin_rename_sched_init(struct kshark_context *kshark_ctx, int sd)
{
	struct plugin_rename_context *plugin_ctx;

	if (!plugin_update_stream_context(kshark_ctx, sd))
		return 0;

	plugin_ctx = plugin_context_handler[sd];
	kshark_register_event_handler(&kshark_ctx->event_handlers,
				      plugin_ctx->sched_switch_event->id,
				      sd,
				      plugin_nop,
				      plugin_rename);

	return kshark_ctx->n_streams;
}

static int plugin_rename_sched_close(struct kshark_context *kshark_ctx, int sd)
{
	struct plugin_rename_context *plugin_ctx;
	plugin_ctx = plugin_context_handler[sd];
	if (!plugin_ctx)
		return 0;

	kshark_unregister_event_handler(&kshark_ctx->event_handlers,
					plugin_ctx->sched_switch_event->id,
					sd,
					plugin_nop,
					plugin_rename);

	plugin_close(sd);

	return kshark_ctx->n_streams;
}

/** Load this plugin. */
int KSHARK_PLUGIN_INITIALIZER(struct kshark_context *kshark_ctx, int sd)
{
	printf("--> rename init %i\n", sd);
	return plugin_rename_sched_init(kshark_ctx, sd);
}

/** Unload this plugin. */
int KSHARK_PLUGIN_DEINITIALIZER(struct kshark_context *kshark_ctx, int sd)
{
	printf("<-- rename close %i\n", sd);
	return plugin_rename_sched_close(kshark_ctx, sd);
}

#endif
