// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009, 2010 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>

#include "trace-local.h"

void trace_check_events(int argc, char **argv)
{
	const char *tracing;
	int ret, c;
	int parsing_failures = 0;
	struct tep_handle *pevent = NULL;
	struct tep_plugin_list *list = NULL;

	while ((c = getopt(argc-1, argv+1, "+hN")) >= 0) {
		switch (c) {
		case 'h':
		default:
			usage(argv);
			break;
		case 'N':
			tracecmd_disable_plugins = 1;
			break;
		}
	}
	tracing = tracecmd_get_tracing_dir();

	if (!tracing) {
		printf("Can not find or mount tracing directory!\n"
		       "Either tracing is not configured for this "
		       "kernel\n"
		       "or you do not have the proper permissions to "
		       "mount the directory");
		exit(EINVAL);
	}

	pevent = tep_alloc();
	if (!pevent)
		exit(EINVAL);

	if (tracecmd_disable_plugins)
		tep_set_flag(pevent, TEP_DISABLE_PLUGINS);
	if (tracecmd_disable_sys_plugins)
		tep_set_flag(pevent, TEP_DISABLE_SYS_PLUGINS);

	list = tep_load_plugins(pevent);
	ret = tracecmd_fill_local_events(tracing, pevent, &parsing_failures);
	if (ret || parsing_failures)
		ret = EINVAL;
	tep_unload_plugins(list, pevent);
	tep_free(pevent);

	return;
}
