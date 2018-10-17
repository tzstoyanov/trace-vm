// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2018 VMware Inc, Yordan Karadzhov <y.karadz@gmail.com>
 */

// C
#include <stdio.h>
#include <stdlib.h>

// KernelShark
#include "libkshark.h"

int main(int argc, char **argv)
{
	struct kshark_context *kshark_ctx;
	struct kshark_data_stream *stream;
	struct kshark_entry **data_f1 = NULL;
	struct kshark_entry **data_f2 = NULL;
	int *pids_f1 = NULL, *pids_f2 = NULL;
	size_t n_rows_f1 = 0, n_tasks_f1;
	size_t n_rows_f2 = 0, n_tasks_f2;
	char *entry_str;
	int sd1, sd2;
	size_t r;

	if (argc != 3)
		return 1;

	/* Create a new kshark session. */
	kshark_ctx = NULL;
	if (!kshark_instance(&kshark_ctx))
		return 1;

	/* Open a trace data file produced by trace-cmd. */
	sd1 = kshark_open(kshark_ctx, argv[1]);
	if (sd1 >= 0) {
		/* Load the content of the file into an array of entries. */
		n_rows_f1 = kshark_load_data_entries(kshark_ctx, sd1, &data_f1);
		printf("file %s -> %li entries\n", argv[1], n_rows_f1);

		/* Print to the screen the list of all tasks. */
		n_tasks_f1 = kshark_get_task_pids(kshark_ctx, sd1, &pids_f1);
		stream = kshark_get_data_stream(kshark_ctx, sd1);
		for (r = 0; r < n_tasks_f1; ++r) {
			const char *task_str =
				tep_data_comm_from_pid(stream->pevent, pids_f1[r]);

			printf("task: %s-%i\n", task_str, pids_f1[r]);
		}

		/* Print to the screen the first 5 entries. */
		for (r = 0; r < 5; ++r) {
			entry_str = kshark_dump_entry(data_f1[r]);
			puts(entry_str);
			free(entry_str);
		}
	}

	/* Open another data file. */
	sd2 = kshark_open(kshark_ctx, argv[2]);
	if (sd2 >= 0) {
		/* Load the content of the second file into an array of entries. */
		n_rows_f2 = kshark_load_data_entries(kshark_ctx, sd2, &data_f2);
		printf("\n\nfile %s -> %i %li entries\n", argv[2], sd2, n_rows_f2);

		/* Print to the screen the list of all tasks. */
		n_tasks_f2 = kshark_get_task_pids(kshark_ctx, sd2, &pids_f2);
		stream = kshark_get_data_stream(kshark_ctx, sd2);
		for (r = 0; r < n_tasks_f2; ++r) {
			const char *task_str =
				tep_data_comm_from_pid(stream->pevent, pids_f2[r]);

			printf("task: %s-%i\n", task_str, pids_f2[r]);
		}

		/* Print to the screen the first 5 entries. */
		for (r = 0; r < 5; ++r) {
			entry_str = kshark_dump_entry(data_f2[r]);
			puts(entry_str);
			free(entry_str);
		}
	}

	/* Free the memory. */
	free(pids_f1);
	free(pids_f2);

	for (r = 0; r < n_rows_f1; ++r)
		free(data_f1[r]);

	free(data_f1);

	for (r = 0; r < n_rows_f2; ++r)
		free(data_f2[r]);

	free(data_f2);

	/* Close the session. */
	kshark_free(kshark_ctx);

	return 0;
}
