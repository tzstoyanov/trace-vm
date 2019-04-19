// SPDX-License-Identifier: LGPL-2.1

/*
 * Copyright (C) 2018 VMware Inc, Yordan Karadzhov <y.karadz@gmail.com>
 */

/**
 *  @file    sched_events.c
 *  @brief   Defines a callback function for Sched events used to registers the
 *	     "next" task (if not registered already) and to changes the value
 *	     of the "pid" field of the "sched_switch" entries such that, it
 *	     will be ploted as part of the "next" task.
 */

// C
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

// KernelShark
#include "plugins/sched_events.h"

/** Plugin context instance. */
// struct plugin_sched_context *plugin_sched_context_handler = NULL;
static struct plugin_sched_context *
plugin_sched_context_handler[KS_MAX_NUM_STREAMS] = {NULL};

static bool define_wakeup_event(struct tep_handle *tep, const char *wakeup_name,
				struct tep_event **wakeup_event,
				struct tep_format_field **pid_field)
{
	struct tep_event *event;

	event = tep_find_event_by_name(tep, "sched", wakeup_name);
	if (!event)
		return false;

	*wakeup_event = event;
	*pid_field = tep_find_any_field(event, "pid");

	return true;
}

static void plugin_free_context(struct plugin_sched_context *plugin_ctx)
{
	if (!plugin_ctx)
		return;

	tracecmd_filter_id_hash_free(plugin_ctx->second_pass_hash);
	kshark_free_collection_list(plugin_ctx->collections);

	free(plugin_ctx);
}

struct plugin_sched_context *get_sched_context(int sd)
{
	return plugin_sched_context_handler[sd];
}

static bool plugin_sched_init_context(struct kshark_context *kshark_ctx,
				      int sd)
{
	struct plugin_sched_context *plugin_ctx;
	struct kshark_data_stream *stream;
	struct tep_event *event;
	bool wakeup_found;

	stream = kshark_get_data_stream(kshark_ctx, sd);
	if (!stream)
		return false;

	/* No context should exist when we initialize the plugin. */
	assert(plugin_sched_context_handler[sd] == NULL);

	plugin_ctx = calloc(1, sizeof(*plugin_ctx));
	if (!plugin_ctx) {
		fprintf(stderr,
			"Failed to allocate memory for plugin_sched_context.\n");
		return false;
	}

	plugin_ctx->handle = stream->handle;
	plugin_ctx->pevent = stream->pevent;
	plugin_ctx->collections = NULL;

	event = tep_find_event_by_name(plugin_ctx->pevent,
				       "sched", "sched_switch");
	if (!event) {
		fprintf(stderr, "No sched_switch events in stream %i.\n", sd);
		plugin_free_context(plugin_ctx);
		plugin_sched_context_handler = NULL;
		return false;
	}

	plugin_ctx->sched_switch_event = event;
	plugin_ctx->sched_switch_next_field =
		tep_find_any_field(event, "next_pid");

	plugin_ctx->sched_switch_comm_field =
		tep_find_field(event, "next_comm");

	plugin_ctx->sched_switch_prev_state_field =
		tep_find_field(event, "prev_state");

	wakeup_found = define_wakeup_event(stream->pevent, "sched_wakeup",
					   &plugin_ctx->sched_wakeup_event,
					   &plugin_ctx->sched_wakeup_pid_field);

	wakeup_found |= define_wakeup_event(stream->pevent, "sched_wakeup_new",
					    &plugin_ctx->sched_wakeup_new_event,
					    &plugin_ctx->sched_wakeup_new_pid_field);

	wakeup_found |= define_wakeup_event(stream->pevent, "sched_waking",
					    &plugin_ctx->sched_waking_event,
					    &plugin_ctx->sched_waking_pid_field);

	plugin_ctx->second_pass_hash = tracecmd_filter_id_hash_alloc();
	plugin_sched_context_handler[sd] = plugin_ctx;

	return true;
}

/**
 * @brief Get the Process Id of the next scheduled task.
 *
 * @param record: Input location for a sched_switch record.
 */
int plugin_get_next_pid(struct tep_record *record, int sd)
{
	struct plugin_sched_context *plugin_ctx =
		plugin_sched_context_handler[sd];
	unsigned long long val;
	int ret;

	ret = tep_read_number_field(plugin_ctx->sched_switch_next_field,
				    record->data, &val);

	return ret ? : val;
}

static void plugin_register_command(struct kshark_context *kshark_ctx,
				    struct tep_record *record,
				    int sd, int pid)
{
	struct plugin_sched_context *plugin_ctx =
		plugin_sched_context_handler[sd];
	struct kshark_data_stream *stream;
	const char *comm;

	stream = kshark_get_data_stream(kshark_ctx, sd);
	if (!stream || !plugin_ctx->sched_switch_comm_field)
		return;

	comm = record->data + plugin_ctx->sched_switch_comm_field->offset;
	/*
	 * TODO: The retrieve of the name of the command above needs to be
	 * implemented as a wrapper function in libtracevent.
	 */

	if (!tep_is_pid_registered(stream->pevent, pid))
			tep_register_comm(stream->pevent, comm, pid);
}

int find_wakeup_pid(struct kshark_context *kshark_ctx, struct kshark_entry *e, int sd,
		    struct tep_event *wakeup_event, struct tep_format_field *pid_field)
{
	struct kshark_data_stream *stream;
	struct tep_record *record;
	unsigned long long val;
	int ret;

	stream = kshark_get_data_stream(kshark_ctx, sd);
	if (!stream || !wakeup_event || e->event_id != wakeup_event->id)
		return -1;

	record = tracecmd_read_at(stream->handle, e->offset, NULL);
	ret = tep_read_number_field(pid_field, record->data, &val);
	free_record(record);

	if (ret)
		return -1;

	return val;
}

static bool wakeup_match_rec_pid(struct plugin_sched_context *plugin_ctx,
				 struct kshark_context *kshark_ctx,
				 struct kshark_entry *e,
				 int sd, int pid)
{
	struct tep_event *wakeup_events[] = {
		plugin_ctx->sched_waking_event,
		plugin_ctx->sched_wakeup_event,
		plugin_ctx->sched_wakeup_new_event,
	};
	struct tep_format_field *wakeup_fields[] = {
		plugin_ctx->sched_waking_pid_field,
		plugin_ctx->sched_wakeup_pid_field,
		plugin_ctx->sched_wakeup_new_pid_field,
	};
	int i, wakeup_pid = -1;

	for (i = 0; i < sizeof(wakeup_events) / sizeof(wakeup_events[0]); i++) {
		wakeup_pid = find_wakeup_pid(kshark_ctx, e, sd,
					     wakeup_events[i], wakeup_fields[i]);
		if (wakeup_pid >= 0)
			break;
	}

	if (wakeup_pid >= 0 && wakeup_pid == pid)
		return true;

	return false;
}

/**
 * @brief Process Id matching function adapted for sched_wakeup and
 *	  sched_wakeup_new events.
 *
 * @param kshark_ctx: Input location for the session context pointer.
 * @param e: kshark_entry to be checked.
 * @param pid: Matching condition value.
 *
 * @returns True if the Pid of the record matches the value of "pid".
 *	    Otherwise false.
 */
bool plugin_wakeup_match_rec_pid(struct kshark_context *kshark_ctx,
				 struct kshark_entry *e,
				 int sd, int *pid)
{
	struct plugin_sched_context *plugin_ctx;

	plugin_ctx = plugin_sched_context_handler[sd];

	if (e->stream_id != sd)
		return false;

	return wakeup_match_rec_pid(plugin_ctx, kshark_ctx, e, sd, *pid);
}

/**
 * @brief Process Id matching function adapted for sched_switch events.
 *
 * @param kshark_ctx: Input location for the session context pointer.
 * @param e: kshark_entry to be checked.
 * @param pid: Matching condition value.
 *
 * @returns True if the Pid of the record matches the value of "pid".
 *	    Otherwise false.
 */
bool plugin_switch_match_rec_pid(struct kshark_context *kshark_ctx,
				 struct kshark_entry *e,
				 int sd, int *pid)
{
	struct plugin_sched_context *plugin_ctx;
	unsigned long long val;
	int ret, switch_pid = -1;

	plugin_ctx = plugin_sched_context_handler[sd];

	if (plugin_ctx->sched_switch_event &&
	    e->stream_id == sd &&
	    e->event_id == plugin_ctx->sched_switch_event->id) {
		struct tep_record *record;

		record = kshark_read_at(kshark_ctx, sd, e->offset);
		ret = tep_read_number_field(plugin_ctx->sched_switch_prev_state_field,
					    record->data, &val);

		if (ret == 0 && !(val & 0x7f))
			switch_pid = tep_data_pid(plugin_ctx->pevent, record);

		free_record(record);
	}

	if (switch_pid >= 0 && switch_pid == *pid)
		return true;

	return false;
}

/**
 * @brief Process Id matching function adapted for sched_switch events.
 *
 * @param kshark_ctx: Input location for the session context pointer.
 * @param e: kshark_entry to be checked.
 * @param pid: Matching condition value.
 *
 * @returns True if the Pid of the entry matches the value of "pid".
 *	    Otherwise false.
 */
bool plugin_switch_match_entry_pid(struct kshark_context *kshark_ctx,
				   struct kshark_entry *e,
				   int sd, int *pid)
{
	struct plugin_sched_context *plugin_ctx;

	plugin_ctx = plugin_sched_context_handler[sd];

	if (plugin_ctx->sched_switch_event &&
	    e->event_id == plugin_ctx->sched_switch_event->id &&
	    e->stream_id == sd &&
	    e->pid == *pid)
		return true;

	return false;
}

/**
 * @brief A match function to be used to process a data collections for
 *	  the Sched events plugin.
 *
 * @param kshark_ctx: Input location for the session context pointer.
 * @param e: kshark_entry to be checked.
 * @param pid: Matching condition value.
 *
 * @returns True if the entry is relevant for the Sched events plugin.
 *	    Otherwise false.
 */
bool plugin_match_pid(struct kshark_context *kshark_ctx,
		      struct kshark_entry *e, int sd, int *pid)
{
	return plugin_switch_match_entry_pid(kshark_ctx, e, sd, pid) ||
	       plugin_switch_match_rec_pid(kshark_ctx, e, sd, pid) ||
	       plugin_wakeup_match_rec_pid(kshark_ctx, e, sd, pid);
}

static void plugin_sched_action(struct kshark_context *kshark_ctx,
				struct tep_record *rec,
				struct kshark_entry *entry)
{
	int pid = plugin_get_next_pid(rec, entry->stream_id);
	if (pid >= 0) {
		entry->pid = pid;
		plugin_register_command(kshark_ctx, rec,
					entry->stream_id,
					entry->pid);
	}
}

static int plugin_sched_init(struct kshark_context *kshark_ctx, int sd)
{
	struct plugin_sched_context *plugin_ctx;

	if (!plugin_sched_init_context(kshark_ctx, sd))
		return 0;

	plugin_ctx = plugin_sched_context_handler[sd];

	kshark_register_event_handler(&kshark_ctx->event_handlers,
				      plugin_ctx->sched_switch_event->id,
				      sd,
				      plugin_sched_action,
				      plugin_draw);

	return 1;
}

static int plugin_sched_close(struct kshark_context *kshark_ctx, int sd)
{
	struct plugin_sched_context *plugin_ctx;

	plugin_ctx = plugin_sched_context_handler[sd];
	if (!plugin_ctx)
		return 0;

	kshark_unregister_event_handler(&kshark_ctx->event_handlers,
					plugin_ctx->sched_switch_event->id,
					sd,
					plugin_sched_action,
					plugin_draw);

	plugin_free_context(plugin_sched_context_handler[sd]);
	plugin_sched_context_handler[sd] = NULL;

	return 1;
}

/** Load this plugin. */
int KSHARK_PLUGIN_INITIALIZER(struct kshark_context *kshark_ctx, int sd)
{
	printf("--> sched init %i\n", sd);
	return plugin_sched_init(kshark_ctx, sd);
}

/** Unload this plugin. */
int KSHARK_PLUGIN_DEINITIALIZER(struct kshark_context *kshark_ctx, int sd)
{
	printf("<-- sched close %i\n", sd);
	return plugin_sched_close(kshark_ctx, sd);
}
