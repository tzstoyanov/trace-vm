#ifndef _TRACE_MSG_H_
#define _TRACE_MSG_H_

#include <stdbool.h>
#define VIRTIO_PORTS	"/dev/virtio-ports/"
#define AGENT_CTL_PATH	VIRTIO_PORTS "agent-ctl-path"
#define TRACE_PATH_CPU	VIRTIO_PORTS "trace-path-cpu%d"

#define UDP_MAX_PACKET	(65536 - 20)
#define V2_MAGIC	"677768\0"
#define V2_CPU		"-1V2"

#define V1_PROTOCOL	1
#define V2_PROTOCOL	2

extern unsigned int page_size;

void plog(const char *fmt, ...);
void pdie(const char *fmt, ...);

int tracecmd_open_virt_ports(int *ports, int cpus);

#endif /* _TRACE_MSG_H_ */
