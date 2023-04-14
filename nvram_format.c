#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "nvram_interface.h"

/* nvram_format_v2.c*/
extern struct nvram_format nvram_v2_format;
/* nvram_format_legacy.c*/
extern struct nvram_format nvram_legacy_format;
/* nvram_format_platform.c */
extern struct nvram_format nvram_platform_format;

struct format_desc {
	char* name;
	struct nvram_format* format;
};

struct format_desc available_formats[] = {
#if NVRAM_FORMAT_V2 > 0
		{.name = "v2", .format = &nvram_v2_format},
#endif
#if NVRAM_FORMAT_LEGACY > 0
		{.name = "legacy", .format = &nvram_legacy_format},
#endif
#if NVRAM_FORMAT_PLATFORM > 0
		{.name = "platform", .format = &nvram_platform_format},
#endif
		{.name = NULL},
};

struct nvram_format* nvram_get_format(const char* format_name)
{
	struct format_desc* desc = &available_formats[0];
	while (desc->name != NULL) {
		if (strcmp(desc->name, format_name) == 0)
			return desc->format;
		desc++;
	}
	return NULL;
}
