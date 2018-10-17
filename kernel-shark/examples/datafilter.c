// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2018 VMware Inc, Yordan Karadzhov <y.karadz@gmail.com>
 */

// C
#include <stdio.h>
#include <stdlib.h>

// KernelShark
#include "libkshark.h"

const char *default_file = "trace.dat";

int main(int argc, char **argv)
{
	size_t i, sd, n_rows, n_tasks, n_evts, count;
	struct kshark_context *kshark_ctx;
	struct kshark_data_stream *stream;
	struct kshark_entry **data = NULL;
	struct tep_event_filter *adv_filter;
	struct tep_event *event;
	char *entry_str;
	int *pids;

	/* Create a new kshark session. */
	kshark_ctx = NULL;
	if (!kshark_instance(&kshark_ctx))
		return 1;

	/* Open a trace data file produced by trace-cmd. */
	if (argc > 1)
		sd = kshark_open(kshark_ctx, argv[1]);
	else
		sd = kshark_open(kshark_ctx, default_file);

	if (sd < 0) {
		kshark_free(kshark_ctx);
		return 1;
	}

	/* Load the content of the file into an array of entries. */
	n_rows = kshark_load_data_entries(kshark_ctx, sd, &data);

	/* Filter the trace data coming from trace-cmd. */
	n_tasks = kshark_get_task_pids(kshark_ctx, sd, &pids);
	stream = kshark_get_data_stream(kshark_ctx, sd);
	for (i = 0; i < n_tasks; ++i) {
		const char *task_str =
			tep_data_comm_from_pid(stream->pevent,
					       pids[i]);

		if (strcmp(task_str, "trace-cmd") == 0)
			kshark_filter_add_id(kshark_ctx, sd,
					     KS_HIDE_TASK_FILTER,
					     pids[i]);
	}

	free(pids);

	/*
	 * Set the Filter Mask. In this case we want to avoid showing the
	 * filterd entris in text format.
	 */
	kshark_ctx->filter_mask = KS_TEXT_VIEW_FILTER_MASK;
	kshark_filter_entries(kshark_ctx, sd, data, n_rows);

	/* Print to the screen the first 10 visible entries. */
	count = 0;
	i = 0;
	for (i = 0; i < n_rows; ++i) {
		if (data[i]->visible & KS_TEXT_VIEW_FILTER_MASK) {
			entry_str = kshark_dump_entry(data[i]);
			puts(entry_str);
			free(entry_str);

			if (++count > 10)
				break;
		}

		++i;
	}

	puts("\n\n");

	/* Show only "sched" events. */
	n_evts = tep_get_events_count(stream->pevent);
	for (i = 0; i < n_evts; ++i) {
		event = tep_get_event(stream->pevent, i);
		if (strcmp(event->system, "sched") == 0)
			kshark_filter_add_id(kshark_ctx, sd,
					     KS_SHOW_EVENT_FILTER,
					     event->id);
	}

	kshark_filter_entries(kshark_ctx, sd, data, n_rows);

	/* Print to the screen the first 10 visible entries. */
	count = 0;
	i = 0;
	for (i = 0; i < n_rows; ++i) {
		if (data[i]->visible & KS_TEXT_VIEW_FILTER_MASK) {
			entry_str = kshark_dump_entry(data[i]);
			puts(entry_str);
			free(entry_str);

			if (++count > 10)
				break;
		}

		++i;
	}

	puts("\n\n");

	/* Clear all filters. */
	kshark_filter_clear(kshark_ctx, sd, KS_HIDE_TASK_FILTER);
	kshark_filter_clear(kshark_ctx, sd, KS_SHOW_EVENT_FILTER);

	/* Use the Advanced filter to do event content based filtering. */
	adv_filter = stream->advanced_event_filter;
	tep_filter_add_filter_str(adv_filter,
				  "sched/sched_wakeup:target_cpu==1");

	/* The Advanced filter requires reloading the data. */
	for (i = 0; i < n_rows; ++i)
		free(data[i]);

	n_rows = kshark_load_data_entries(kshark_ctx, sd, &data);

	count = 0;
	for (i = 0; i < n_rows; ++i) {
		if (data[i]->visible & KS_EVENT_VIEW_FILTER_MASK) {
			entry_str = kshark_dump_entry(data[i]);
			puts(entry_str);
			free(entry_str);

			if (++count > 10)
				break;
		}
	}

	/* Free the memory. */
	for (i = 0; i < n_rows; ++i)
		free(data[i]);

	free(data);

	/* Close the file. */
	kshark_close(kshark_ctx, sd);

	/* Close the session. */
	kshark_free(kshark_ctx);

	return 0;
}
