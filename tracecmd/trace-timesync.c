// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019, VMware, Tzvetomir Stoyanov <tstoyanov@vmware.com>
 *
 */

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <linux/vm_sockets.h>
#include "trace-local.h"

static int clock_sync_x86_host_init(struct tracecmd_clock_sync *clock_context);
static int clock_sync_x86_host_free(struct tracecmd_clock_sync *clock_context);
static int clock_sync_x86_host_find_events(struct tracecmd_clock_sync *clock,
				    int cpu, struct tracecmd_time_sync_event *event);
static int clock_sync_x86_guest_init(struct tracecmd_clock_sync *clock_context);
static int clock_sync_x86_guest_free(struct tracecmd_clock_sync *clock_context);
static int clock_sync_x86_guest_find_events(struct tracecmd_clock_sync *clock,
					    int pid,
					    struct tracecmd_time_sync_event *event);

struct tracecmd_event_descr {
	char			*system;
	char			*name;
};

struct tracecmd_ftrace_param {
	char	*file;
	char	*set;
	char	*reset;
};

enum clock_sync_context {
	CLOCK_KVM_X86_VSOCK_HOST,
	CLOCK_KVM_X86_VSOCK_GUEST,
	CLOCK_CONTEXT_MAX,
};

struct tracecmd_clock_sync {
	enum clock_sync_context		clock_context_id;
	struct tracecmd_ftrace_param	*ftrace_params;
	struct tracecmd_time_sync_event	*events;
	int				events_count;
	struct tep_handle		*tep;
	struct buffer_instance		*vinst;

	int				probes_count;
	int				bad_probes;
	int				probes_size;
	long long			*times;
	long long			*offsets;
	long long			offset_av;
	long long			offset_min;
	long long			offset_max;
	int				debug_fd;

	unsigned int			local_cid;
	unsigned int			local_port;
	unsigned int			remote_cid;
	unsigned int			remote_port;
};

struct {
	const char *systems[3];
	struct tracecmd_ftrace_param ftrace_params[5];
	struct tracecmd_event_descr events[3];
	int (*clock_sync_init)(struct tracecmd_clock_sync *clock_context);
	int (*clock_sync_free)(struct tracecmd_clock_sync *clock_context);
	int (*clock_sync_find_events)(struct tracecmd_clock_sync *clock_context,
				      int pid,
				      struct tracecmd_time_sync_event *event);
	int (*clock_sync_load)(struct tracecmd_clock_sync *clock_context);
	int (*clock_sync_unload)(struct tracecmd_clock_sync *clock_context);
} static clock_sync[CLOCK_CONTEXT_MAX] = {
	{ /* CLOCK_KVM_X86_VSOCK_HOST */
	  .systems = {"vsock", "ftrace", NULL},
	  .ftrace_params = {
	  {"set_ftrace_filter", "vmx_read_l1_tsc_offset\nsvm_read_l1_tsc_offset", "\0"},
	  {"current_tracer", "function", "nop"},
	  {"events/vsock/virtio_transport_recv_pkt/enable", "1", "0"},
	  {"events/vsock/virtio_transport_recv_pkt/filter", NULL, "\0"},
	  {NULL, NULL, NULL} },
	  .events = {
	  {.system = "ftrace", .name = "function"},
	  {.system = "vsock", .name = "virtio_transport_recv_pkt"},
	  {.system = NULL, .name = NULL} },
	 clock_sync_x86_host_init,
	 clock_sync_x86_host_free,
	 clock_sync_x86_host_find_events,
	},

	{ /* CLOCK_KVM_X86_VSOCK_GUEST */
	 .systems = { "vsock", "ftrace", NULL},
	 .ftrace_params = {
	  {"set_ftrace_filter", "vp_notify", "\0"},
	  {"current_tracer", "function", "nop"},
  {"events/vsock/virtio_transport_alloc_pkt/enable", "1", "0"},
	  {"events/vsock/virtio_transport_alloc_pkt/filter", NULL, "\0"},
	  {NULL, NULL, NULL},
	  },
	  .events = {
	  {.system = "vsock", .name = "virtio_transport_alloc_pkt"},
	  {.system = "ftrace", .name = "function"},
	  {.system = NULL, .name = NULL}
	 },
	  clock_sync_x86_guest_init,
	  clock_sync_x86_guest_free,
	  clock_sync_x86_guest_find_events,
	}
};

static int clock_sync_x86_host_init(struct tracecmd_clock_sync *clock_context)
{
	char vsock_filter[255];

	snprintf(vsock_filter, 255,
		"src_cid==%u && src_port==%u && dst_cid==%u && dst_port==%u && len!=0",
		clock_context->remote_cid, clock_context->remote_port,
		clock_context->local_cid, clock_context->local_port);

	clock_context->ftrace_params[3].set = strdup(vsock_filter);
	return 1;
}

static int clock_sync_x86_host_free(struct tracecmd_clock_sync *clock_context)
{
	free(clock_context->ftrace_params[3].set);
	clock_context->ftrace_params[3].set = NULL;
	return 1;
}

static int clock_sync_x86_guest_init(struct tracecmd_clock_sync *clock_context)
{
	char vsock_filter[255];

	snprintf(vsock_filter, 255,
		"src_cid==%u && src_port==%u && dst_cid==%u && dst_port==%u && len!=0",
		clock_context->local_cid, clock_context->local_port,
		clock_context->remote_cid, clock_context->remote_port);

	clock_context->ftrace_params[3].set = strdup(vsock_filter);
	return 1;
}

static int clock_sync_x86_guest_free(struct tracecmd_clock_sync *clock_context)
{
	free(clock_context->ftrace_params[3].set);
	clock_context->ftrace_params[3].set = NULL;
	return 1;
}

static int
get_events_in_page(struct tep_handle *tep, void *page,
		    int size, int cpu, struct tracecmd_time_sync_event **events,
		    int *events_count, int *events_size)
{
	struct tracecmd_time_sync_event *events_array = NULL;
	struct tep_record *last_record = NULL;
	struct tep_event *event = NULL;
	struct tep_record *record;
	int id, cnt = 0;

	if (size <= 0)
		return 0;

	if (*events == NULL) {
		*events = malloc(10*sizeof(struct tracecmd_time_sync_event));
		*events_size = 10;
		*events_count = 0;
	}

	while (true) {
		event = NULL;
		record = tracecmd_read_page_record(tep, page, size,
						   last_record);
		if (!record)
			break;
		free_record(last_record);
		id = tep_data_type(tep, record);
		event = tep_find_event(tep, id);
		if (event) {
			if (*events_count >= *events_size) {
				events_array = realloc(*events,
					((3*(*events_size))/2)*
					sizeof(struct tracecmd_time_sync_event));
				if (events_array) {
					*events = events_array;
					(*events_size) = (((*events_size)*3)/2);
				}
			}

			if (*events_count < *events_size) {
				(*events)[*events_count].ts = record->ts;
				(*events)[*events_count].cpu = cpu;
				(*events)[*events_count].id = id;
				(*events)[*events_count].pid = tep_data_pid(tep, record);
				(*events_count)++;
				cnt++;
			}
		}
		last_record = record;
	}
	free_record(last_record);

	return cnt;
}

static int sync_events_cmp(const void *a, const void *b)
{
	const struct tracecmd_time_sync_event *ea = (const struct tracecmd_time_sync_event *)a;
	const struct tracecmd_time_sync_event *eb = (const struct tracecmd_time_sync_event *)b;

	if (ea->ts > eb->ts)
		return 1;
	if (ea->ts < eb->ts)
		return -1;
	return 0;
}

static int find_sync_events(struct tep_handle *pevent,
			    struct tracecmd_time_sync_event *recorded,
			    int rsize, struct tracecmd_time_sync_event *events)
{
	int i = 0, j = 0;

	while (i < rsize) {
		if (!events[j].ts && events[j].id == recorded[i].id &&
		    (events[j].pid < 0 || events[j].pid == recorded[i].pid)) {
			events[j].cpu = recorded[i].cpu;
			events[j].ts = recorded[i].ts;
			j++;
		} else if (j > 0 && events[j-1].id == recorded[i].id &&
			  (events[j-1].pid < 0 || events[j-1].pid == recorded[i].pid)) {
			events[j-1].cpu = recorded[i].cpu;
			events[j-1].ts = recorded[i].ts;
		}
		i++;
	}
	return j;
}

//#define TSYNC_RBUFFER_DEBUG
static int find_raw_events(struct tep_handle *tep,
		    struct buffer_instance *instance,
		    struct tracecmd_time_sync_event *events)
{
	struct tracecmd_time_sync_event *events_array = NULL;
	int events_count = 0;
	int events_size = 0;
	unsigned int p_size;
	char file[PATH_MAX];
	int ts = 0;
	void *page;
	char *path;
	int fd;
	int i;
	int r;

	p_size = getpagesize();
#ifdef TSYNC_RBUFFER_DEBUG
	file = get_instance_file(instance, "trace");
	if (!file)
		return ts;
	{
		char *buf = NULL;
		FILE *fp;
		size_t n;
		int r;

		printf("Events:\n\r");
		fp = fopen(file, "r");
		while ((r = getline(&buf, &n, fp)) >= 0) {

			if (buf[0] != '#')
				printf("%s", buf);
			free(buf);
			buf = NULL;
		}
		fclose(fp);
	}
	tracecmd_put_tracing_file(file);
#endif /* TSYNC_RBUFFER_DEBUG */
	path = get_instance_file(instance, "per_cpu");
	if (!path)
		return ts;

	page = malloc(p_size);
	if (!page)
		die("Failed to allocate time_stamp info");
	for (i = 0; i < instance->cpu_count; i++) {
		sprintf(file, "%s/cpu%d/trace_pipe_raw", path, i);
		fd = open(file, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		do {
			r = read(fd, page, p_size);
			if (r > 0) {
				get_events_in_page(tep, page, r, i,
						   &events_array, &events_count,
						   &events_size);
			}
		} while (r > 0);
		close(fd);
	}
	qsort(events_array, events_count, sizeof(*events_array), sync_events_cmp);
	r = find_sync_events(tep, events_array, events_count, events);
#ifdef TSYNC_RBUFFER_DEBUG
	len = 0;
	while (events[len].id) {
		printf("Found %d @ cpu %d: %lld pid %d\n\r",
			events[len].id,  events[len].cpu,
			events[len].ts, events[len].pid);
		len++;
	}
#endif

	free(events_array);
	free(page);

	tracecmd_put_tracing_file(path);
	return r;
}

static int clock_sync_x86_host_find_events(struct tracecmd_clock_sync *clock,
					   int pid,
					   struct tracecmd_time_sync_event *event)
{
	int ret;

	clock->events[0].pid = pid;
	ret = find_raw_events(clock->tep, clock->vinst, clock->events);
	event->ts = clock->events[0].ts;
	event->cpu = clock->events[0].cpu;
	return ret;

}

static int clock_sync_x86_guest_find_events(struct tracecmd_clock_sync *clock,
					    int pid,
					    struct tracecmd_time_sync_event *event)
{
	int ret;

	ret = find_raw_events(clock->tep, clock->vinst, clock->events);
	if (ret != clock->events_count)
		return 0;
	event->ts = clock->events[1].ts;
	event->cpu = clock->events[0].cpu;
	return 1;

}

static void tracecmd_clock_sync_reset(struct tracecmd_clock_sync *clock_context)
{
	int i = 0;

	while (clock_context->events[i].id) {
		clock_context->events[i].cpu = 0;
		clock_context->events[i].ts = 0;
		clock_context->events[i].pid = -1;
		i++;
	}
}

int tracecmd_clock_find_event(struct tracecmd_clock_sync *clock, int pid,
			      struct tracecmd_time_sync_event *event)
{
	int ret = 0;
	int id;

	if (clock == NULL ||
	    clock->clock_context_id >= CLOCK_CONTEXT_MAX)
		return 0;

	id = clock->clock_context_id;
	if (clock_sync[id].clock_sync_find_events)
		ret = clock_sync[id].clock_sync_find_events(clock, pid, event);

	tracecmd_clock_sync_reset(clock);
	return ret;
}

static void clock_context_copy(struct tracecmd_clock_sync *clock_context,
			       struct tracecmd_ftrace_param *params,
			       struct tracecmd_event_descr *events)
{
	int i;

	i = 0;
	while (params[i].file)
		i++;
	clock_context->ftrace_params = calloc(i+1, sizeof(struct tracecmd_ftrace_param));
	i = 0;
	while (params[i].file) {
		clock_context->ftrace_params[i].file = strdup(params[i].file);
		if (params[i].set)
			clock_context->ftrace_params[i].set = strdup(params[i].set);
		if (params[i].reset)
			clock_context->ftrace_params[i].reset = strdup(params[i].reset);
		i++;
	}

	i = 0;
	while (events[i].name)
		i++;
	clock_context->events = calloc(i+1, sizeof(struct tracecmd_time_sync_event));
	clock_context->events_count = i;
}

void trace_instance_reset(struct buffer_instance *vinst)
{
	write_instance_file(vinst, "trace", "\0", NULL);
}

static struct buffer_instance *
clock_synch_create_instance(const char *clock, unsigned int cid)
{
	struct buffer_instance *vinst;
	char inst_name[256];

	snprintf(inst_name, 256, "clock_synch-%d", cid);

	vinst = create_instance(strdup(inst_name));
	tracecmd_init_instance(vinst);
	vinst->cpu_count = tracecmd_local_cpu_count();
	tracecmd_make_instance(vinst);
	trace_instance_reset(vinst);
	if (clock)
		vinst->clock = strdup(clock);
	tracecmd_set_clock(vinst);
	return vinst;
}

static struct tep_handle *clock_synch_get_tep(struct buffer_instance *instance,
					      const char * const *systems)
{
	struct tep_handle *tep = NULL;
	char *path;

	path = get_instance_dir(instance);
	tep = tracecmd_local_events_system(path, systems);
	tracecmd_put_tracing_file(path);

	tep_set_file_bigendian(tep, tracecmd_host_bigendian());
	tep_set_local_bigendian(tep, tracecmd_host_bigendian());

	return tep;
}

static int get_vsocket_params(int fd, unsigned int *lcid, unsigned int *lport,
			       unsigned int *rcid, unsigned int *rport)
{
	struct sockaddr_vm addr;
	socklen_t addr_len = sizeof(addr);

	memset(&addr, 0, sizeof(addr));
	if (getsockname(fd, (struct sockaddr *)&addr, &addr_len))
		return -1;
	if (addr.svm_family != AF_VSOCK)
		return -1;
	*lport = addr.svm_port;
	*lcid = addr.svm_cid;

	memset(&addr, 0, sizeof(addr));
	addr_len = sizeof(addr);
	if (getpeername(fd, (struct sockaddr *)&addr, &addr_len))
		return -1;
	if (addr.svm_family != AF_VSOCK)
		return -1;
	*rport = addr.svm_port;
	*rcid = addr.svm_cid;

	return 0;
}

#define TSYNC_DEBUG

struct tracecmd_clock_sync*
tracecmd_clock_context_new(struct tracecmd_msg_handle *msg_handle,
			    const char *clock_str,
			    enum clock_sync_context id)
{
	struct tracecmd_clock_sync *clock_context;
	struct tep_event *event;
	unsigned int i = 0;

	switch (id) {
#ifdef VSOCK
	case CLOCK_KVM_X86_VSOCK_HOST:
	case CLOCK_KVM_X86_VSOCK_GUEST:
		break;
#endif
	default: /* not supported clock sync context */
		return NULL;
	}

	if (id >= CLOCK_CONTEXT_MAX || NULL == msg_handle)
		return NULL;
	clock_context = calloc(1, sizeof(struct tracecmd_clock_sync));
	if (!clock_context)
		return NULL;
	if (get_vsocket_params(msg_handle->fd,
			       &clock_context->local_cid,
			       &clock_context->local_port,
			       &clock_context->remote_cid,
			       &clock_context->remote_port)) {
		free (clock_context);
		return NULL;
	}

	clock_context->clock_context_id = id;
	clock_context_copy(clock_context,
			   clock_sync[id].ftrace_params, clock_sync[id].events);

	if (clock_sync[id].clock_sync_init)
		clock_sync[id].clock_sync_init(clock_context);

	clock_context->vinst = clock_synch_create_instance(clock_str, clock_context->remote_cid);
	clock_context->tep = clock_synch_get_tep(clock_context->vinst,
						 clock_sync[id].systems);
	i = 0;
	while (clock_sync[id].events[i].name) {
		event = tep_find_event_by_name(clock_context->tep,
					       clock_sync[id].events[i].system,
					       clock_sync[id].events[i].name);
		if (!event)
			break;
		clock_context->events[i].id = event->id;
		i++;
	}
#ifdef TSYNC_DEBUG
	clock_context->debug_fd = -1;
#endif

	return clock_context;

}

void tracecmd_clock_context_free(struct buffer_instance *instance)
{
	int i;

	if (instance->clock_sync == NULL ||
	    instance->clock_sync->clock_context_id >= CLOCK_CONTEXT_MAX)
		return;
	if (clock_sync[instance->clock_sync->clock_context_id].clock_sync_free)
		clock_sync[instance->clock_sync->clock_context_id].clock_sync_free(instance->clock_sync);

	i = 0;
	while (instance->clock_sync->ftrace_params[i].file) {
		free(instance->clock_sync->ftrace_params[i].file);
		free(instance->clock_sync->ftrace_params[i].set);
		free(instance->clock_sync->ftrace_params[i].reset);
		i++;
	}
	free(instance->clock_sync->ftrace_params);
	free(instance->clock_sync->events);
	tracecmd_remove_instance(instance->clock_sync->vinst);
	/* todo: clean up the instance */
	tep_free(instance->clock_sync->tep);

	free(instance->clock_sync->offsets);
	free(instance->clock_sync->times);
#ifdef TSYNC_DEBUG
	if (instance->clock_sync->debug_fd >= 0) {
		close(instance->clock_sync->debug_fd);
		instance->clock_sync->debug_fd = -1;
	}
#endif
	free(instance->clock_sync);
	instance->clock_sync = NULL;
}

bool tracecmd_time_sync_check(void)
{
#ifdef VSOCK
	return true;
#endif
	return false;
}

void sync_time_with_host_v3(struct buffer_instance *instance)
{
	long long timestamp = 0;
	long long offset = 0;

	if (!instance->do_tsync)
		return;

	if (instance->clock_sync == NULL)
		instance->clock_sync = tracecmd_clock_context_new(instance->msg_handle,
					instance->clock, CLOCK_KVM_X86_VSOCK_GUEST);

	tracecmd_msg_snd_time_sync(instance->msg_handle, instance->clock_sync,
				   &offset, &timestamp);
	if (!offset && !timestamp)
		warning("Failed to synchronize timestamps with the host");
}

void sync_time_with_guest_v3(struct buffer_instance *instance)
{
	long long timestamp = 0;
	long long offset = 0;
	long long *sync_array_ts;
	long long *sync_array_offs;

	if (!instance->do_tsync)
		return;

	if (instance->clock_sync == NULL)
		instance->clock_sync = tracecmd_clock_context_new(instance->msg_handle,
						top_instance.clock, CLOCK_KVM_X86_VSOCK_HOST);

	tracecmd_msg_rcv_time_sync(instance->msg_handle,
				   instance->clock_sync, &offset, &timestamp);

	if (!offset && !timestamp) {
		warning("Failed to synchronize timestamps with guest %s",
			instance->name);
		return;
	}

	sync_array_ts = realloc(instance->time_sync_ts,
			    (instance->time_sync_count+1)*sizeof(long long));
	sync_array_offs = realloc(instance->time_sync_offsets,
			    (instance->time_sync_count+1)*sizeof(long long));

	if (sync_array_ts && sync_array_offs) {
		sync_array_ts[instance->time_sync_count] = timestamp;
		sync_array_offs[instance->time_sync_count] = offset;
		instance->time_sync_count++;
		instance->time_sync_ts = sync_array_ts;
		instance->time_sync_offsets = sync_array_offs;

	} else {
		free(sync_array_ts);
		free(sync_array_offs);
	}

}

static void set_clock_synch_events(struct buffer_instance *instance,
				   struct tracecmd_ftrace_param *params,
				   bool enable)
{
	int i = 0;

	if (!enable)
		write_tracing_on(instance, 0);

	while (params[i].file) {
		if (enable && params[i].set) {
			write_instance_file(instance, params[i].file,
					    params[i].set, NULL);
		}
		if (!enable && params[i].reset)
			write_instance_file(instance, params[i].file,
					    params[i].reset, NULL);
		i++;
	}

	if (enable)
		write_tracing_on(instance, 1);
}

int tracecmd_clock_get_peer(struct tracecmd_clock_sync *clock_context,
			    unsigned int *remote_cid, unsigned int *remote_port)
{
	if (!clock_context)
		return 0;
	if (remote_cid)
		*remote_cid = clock_context->remote_cid;
	if (remote_port)
		*remote_cid = clock_context->remote_port;
	return 1;
}

void tracecmd_clock_synch_enable(struct tracecmd_clock_sync *clock_context)
{
	set_clock_synch_events(clock_context->vinst,
			       clock_context->ftrace_params, true);
}

void tracecmd_clock_synch_disable(struct tracecmd_clock_sync *clock_context)
{
	set_clock_synch_events(clock_context->vinst,
			       clock_context->ftrace_params, false);
}

int tracecmd_clock_synch_calc(struct tracecmd_clock_sync *clock_context,
			       long long *offset_ret, long long *time_ret)
{
	int i, j = 0;
	long long av, tresch, offset = 0, time = 0;

	if (!clock_context || !clock_context->probes_count)
		return 0;
	av = clock_context->offset_av / clock_context->probes_count;
	tresch = (long long)((clock_context->offset_max - clock_context->offset_min)/10);

	for (i = 0; i < clock_context->probes_count; i++) {
		/* filter the offsets with deviation up to 10% */
		if (llabs(clock_context->offsets[i] - av) < tresch) {
			offset += clock_context->offsets[i];
			j++;
		}
	}
	if (j)
		offset /= (long long)j;

	tresch = 0;
	for (i = 0; i < clock_context->probes_count; i++) {
		if ((!tresch || tresch > llabs(offset-clock_context->offsets[i]))) {
			tresch = llabs(offset-clock_context->offsets[i]);
			time = clock_context->times[i];
		}
	}
	if (offset_ret)
		*offset_ret = offset;
	if (time_ret)
		*time_ret = time;
#ifdef TSYNC_DEBUG
	printf("\n calculated offset: %lld, %d/%d probes\n\r",
		*offset_ret, clock_context->probes_count,
		clock_context->probes_count + clock_context->bad_probes);
#endif
	return 1;
}

void tracecmd_clock_synch_calc_reset(struct tracecmd_clock_sync *clock_context)
{
	if (!clock_context)
		return;

	clock_context->probes_count = 0;
	clock_context->bad_probes = 0;
	clock_context->offset_av = 0;
	clock_context->offset_min = 0;
	clock_context->offset_max = 0;
#ifdef TSYNC_DEBUG
	if (clock_context->debug_fd >= 0) {
		close(clock_context->debug_fd);
		clock_context->debug_fd = -1;
	}
#endif

}

void tracecmd_clock_synch_calc_probe(struct tracecmd_clock_sync *clock_context,
				     long long ts_local, long long ts_remote)
{
	int count;
#ifdef TSYNC_DEBUG
	char buff[256];
#endif

	if (!clock_context || !ts_local || !ts_remote)
		return;
	if (!ts_local || !ts_remote) {
		clock_context->bad_probes++;
		return;
	}

	if (!clock_context->offsets && !clock_context->times) {
		clock_context->offsets = calloc(10, sizeof(long long));
		clock_context->times = calloc(10, sizeof(long long));
		clock_context->probes_size = 10;
	}

	if (clock_context->probes_size == clock_context->probes_count) {
		clock_context->probes_size = (3*clock_context->probes_size)/2;
		clock_context->offsets = realloc(clock_context->offsets,
						 clock_context->probes_size *
						 sizeof(long long));
		clock_context->times = realloc(clock_context->times,
					       clock_context->probes_size*
					       sizeof(long long));
	}

	if (!clock_context->offsets || !clock_context->times) {
		clock_context->probes_size = 0;
		tracecmd_clock_synch_calc_reset(clock_context);
		return;
	}
#ifdef TSYNC_DEBUG
	if (clock_context->debug_fd < 0) {
		sprintf(buff, "s-cid%d.txt", clock_context->remote_cid);
		clock_context->debug_fd = open(buff, O_CREAT|O_WRONLY|O_TRUNC, 0644);
	}
#endif
	count = clock_context->probes_count;
	clock_context->probes_count++;
	clock_context->offsets[count] = ts_remote - ts_local;
	clock_context->times[count] = ts_local;
	clock_context->offset_av += clock_context->offsets[count];

	if (!clock_context->offset_min ||
	    clock_context->offset_min > llabs(clock_context->offsets[count]))
		clock_context->offset_min = llabs(clock_context->offsets[count]);
	if (!clock_context->offset_max ||
	    clock_context->offset_max < llabs(clock_context->offsets[count]))
		clock_context->offset_max = llabs(clock_context->offsets[count]);
#ifdef TSYNC_DEBUG
	sprintf(buff, "%lld %lld\n", ts_local, ts_remote);
	write(clock_context->debug_fd, buff, strlen(buff));
#endif

}
