#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "nvram_interface.h"

#define xstr(a) str(a)
#define str(a) #a

#if NVRAM_FORMAT_V2 > 0
/* nvram_format_v2.c*/
extern struct nvram_format nvram_v2_format;
#endif

struct nvram_format* nvram_get_format(const char* format_name)
{
#if NVRAM_FORMAT_V2 > 0
	if (!strcmp("v2", format_name))
		return &nvram_v2_format;
#endif
	return NULL;
}
