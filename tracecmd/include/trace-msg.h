#ifndef _TRACE_MSG_H_
#define _TRACE_MSG_H_

#include <stdbool.h>

#define UDP_MAX_PACKET	(65536 - 20)
#define V3_MAGIC	"766679\0"
#define V3_CPU		"-1V3"

#define V1_PROTOCOL	1
#define V3_PROTOCOL	3

extern unsigned int page_size;

void plog(const char *fmt, ...);
void pdie(const char *fmt, ...);

#ifndef htonll
# if __BYTE_ORDER == __LITTLE_ENDIAN
#define htonll(x) __bswap_64(x)
#define ntohll(x) __bswap_64(x)
#else
#define htonll(x) (x)
#define ntohll(x) (x)
#endif
#endif

#endif /* _TRACE_MSG_H_ */
