// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2018 VMware Inc, Yordan Karadzhov <y.karadz@gmail.com>
 */

// C
#include <stdint.h>

// C++
#include <vector>
#include <iostream>

// OpenGL
#include <GL/freeglut.h>

// KernelShark
#include "libkshark.h"
#include "KsPlotTools.hpp"

#define GRAPH_HEIGHT	40   // width of the graph in pixels
#define GRAPH_H_MARGIN	50   // size of the white space surrounding the graph
#define WINDOW_WIDTH	1200  // width of the screen window in pixels
#define WINDOW_HEIGHT	720  // height of the scrren window in pixels

using namespace std;

kshark_trace_histo histo_m;
vector<KsPlot::Graph *> graphs;
int *stream_ids;

void calib(kshark_entry *e, int64_t *atgv) {
	e->ts += atgv[0];
}

void plot()
{
	KsPlot::Color::setRainbowFrequency(.9);
	KsPlot::ColorTable pidColors = KsPlot::getTaskColorTable();
	vector<KsPlot::Graph *>::iterator it;
	kshark_context *kshark_ctx(nullptr);
	struct kshark_data_stream *stream;
	KsPlot::Graph *graph;
	int base, nCPUs, sd;

	if (!kshark_instance(&kshark_ctx))
		return;

	for (int d = 0; d < kshark_ctx->n_streams; ++d) {
		sd = stream_ids[d];
		stream = kshark_get_data_stream(kshark_ctx, sd);
		nCPUs = tep_get_cpus(stream->pevent);
		for (int cpu = 0; cpu < nCPUs; ++cpu) {
			graph = new KsPlot::Graph(&histo_m, &pidColors,
							    &pidColors);

			/* Set the dimensions of the Graph. */
			graph->setHeight(GRAPH_HEIGHT);
			graph->setHMargin(GRAPH_H_MARGIN);

			/*
			 * Set the Y coordinate of the Graph's base.
			 * Remember that the "Y" coordinate is inverted.
			 */
			base = 1.7 * GRAPH_HEIGHT * (graphs.size() + 1);
			graph->setBase(base);

			graph->fillCPUGraph(sd, cpu);

			/* Add the Graph. */
			graphs.push_back(graph);
		}
	}

	/* Clear the screen. */
	glClear(GL_COLOR_BUFFER_BIT);

	/* Draw all graphs. */
	for (auto &g: graphs)
		g->draw();

	glFlush();
}

int main(int argc, char **argv)
{
	vector<kshark_entry **> data(argc - 1, nullptr);
	kshark_entry ** data_m;
	vector<size_t> nRows(argc - 1, 0);
	kshark_context *kshark_ctx(nullptr);
	int nBins;
	size_t r, tot;
	int64_t timeShift;

	/* Create a new kshark session. */
	if (!kshark_instance(&kshark_ctx))
		return 1;

	if (argc != 3)
		return 1;

	for (int i = 1; i < argc; ++i) {
		if (kshark_open(kshark_ctx, argv[i]) < 0) {
			cerr << "Unable to load file " << argv[i] << endl;
			return 1;
		}
	}

	stream_ids = kshark_all_streams(kshark_ctx);
	for (int d = 0; d < kshark_ctx->n_streams; ++d) {
		nRows[d] = kshark_load_data_entries(kshark_ctx,
						    stream_ids[d],
						    &data[d]);
	}

	timeShift = data[0][0]->ts - data[1][0]->ts;
	tot = nRows[0] + nRows[1];
	printf("%lu  %lu  t0 %lu\n",  data[0][0]->ts, data[1][0]->ts, timeShift);
	data_m = kshark_data_merge(data[0], nRows[0],
				   data[1], nRows[1],
				   calib, &timeShift);

	for (size_t i = 1; i < tot; ++i) {
		if (data_m[i]->ts < data_m[i-1]->ts)
			break;
	}

	nBins = WINDOW_WIDTH - 2 * GRAPH_H_MARGIN;

	ksmodel_init(&histo_m);
	ksmodel_set_bining(&histo_m, nBins, data_m[0]->ts,
					    data_m[tot - 1]->ts);

	/* Fill the model with data and calculate its state. */
	ksmodel_fill(&histo_m, data_m, tot);

	glutInit(&argc, argv);
	ksplot_make_scene(WINDOW_WIDTH, WINDOW_HEIGHT);
	ksplot_init_opengl(1);

	/* Display graphs. */
	glutDisplayFunc(plot);
	glutMainLoop();

	/* Free the memory. */
	free(stream_ids);

	for (size_t d = 0; d < data.size(); ++d) {
		for (r = 0; r < nRows[d]; ++r)
			free(data[d][r]);

		free(data[d]);
	}

	free(data_m);

	/* Close the session. */
	kshark_free(kshark_ctx);

	return 0;
}
