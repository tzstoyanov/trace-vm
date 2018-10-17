#include <stdio.h>
#include <stdlib.h>

#include "libkshark.h"

int main(int argc, char **argv)
{
	struct kshark_config_doc *conf, *filter, *hello;
	struct kshark_context *kshark_ctx;
	int *ids = NULL, i, sd, n;

	/* Create a new kshark session. */
	kshark_ctx = NULL;
	if (!kshark_instance(&kshark_ctx))
		return 1;

	/** Add new data stream. */
	sd = kshark_add_stream(kshark_ctx);
	if(sd < 0)
		return 1;

	if (argc == 1) {
		kshark_filter_add_id(kshark_ctx, sd, KS_SHOW_TASK_FILTER, 314);
		kshark_filter_add_id(kshark_ctx, sd, KS_SHOW_TASK_FILTER, 42);

		/* Create a new Confog. doc. */
		conf = kshark_config_new("foo.bar.config", KS_CONFIG_JSON);

		/* Add filter's info. */
		filter = kshark_export_all_filters(kshark_ctx, sd, KS_CONFIG_JSON);
		kshark_config_doc_add(conf, "Filters" ,filter);

		/* Add "Hello Kernel" message. */
		hello = kshark_string_config_alloc();
		hello->conf_doc = "Hello Kernel";
		kshark_config_doc_add(conf, "Message" ,hello);

		/* Save to file. */
		kshark_save_config_file("conf.json", conf);
	} else {
		/* Open a Config. file. */
		conf = kshark_open_config_file(argv[1], "foo.bar.config");

		/* Retrieve the filter's info. */
		filter = kshark_config_alloc(KS_CONFIG_JSON);
		if (kshark_config_doc_get(conf, "Filters" ,filter)) {
			kshark_import_all_filters(kshark_ctx, sd, filter);

			/* Get the array of Ids to be fitered. */
			ids = kshark_get_filter_ids(kshark_ctx, sd,
						    KS_SHOW_TASK_FILTER, &n);
			for (i = 0; i < n; ++i)
				printf("pid: %i\n", ids[i]);
		}

		/* Retrieve the message. */
		hello = kshark_string_config_alloc();
		if (kshark_config_doc_get(conf, "Message" ,hello))
			puts((char *) hello->conf_doc);

		free(filter);
		free(hello);
		free(ids);
	}

	kshark_free_config_doc(conf);

	kshark_free(kshark_ctx);

	return 0;
}
