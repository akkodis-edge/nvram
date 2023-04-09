#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "nvram_interface.h"

#define xstr(a) str(a)
#define str(a) #a

/* nvram_interface_file.c*/
extern struct nvram_interface nvram_file_interface;
/* nvram_interface_mtd.c*/
extern struct nvram_interface nvram_mtd_interface;
/* nvram_interface_efi.c*/
extern struct nvram_interface nvram_efi_interface;

struct interface_desc {
	char* name;
	struct nvram_interface* interface;
	char* system_a_default;
	char* system_a_env;
	char* system_b_default;
	char* system_b_env;
	char* user_a_default;
	char* user_a_env;
	char* user_b_default;
	char* user_b_env;
};

struct interface_desc available_interfaces[] = {
#if NVRAM_INTERFACE_FILE > 0
		{.name = "file", .interface = &nvram_file_interface,
			.system_a_default = xstr(NVRAM_FILE_SYSTEM_A), .system_a_env = "NVRAM_FILE_SYSTEM_A",
			.system_b_default = xstr(NVRAM_FILE_SYSTEM_B), .system_b_env = "NVRAM_FILE_SYSTEM_B",
			.user_a_default = xstr(NVRAM_FILE_USER_A), .user_a_env = "NVRAM_FILE_USER_A",
			.user_b_default = xstr(NVRAM_FILE_USER_B), .user_b_env = "NVRAM_FILE_USER_B",
		},
#endif
#if NVRAM_INTERFACE_MTD > 0
		{.name = "mtd", .interface = &nvram_mtd_interface,
			.system_a_default = xstr(NVRAM_MTD_SYSTEM_A), .system_a_env = "NVRAM_MTD_SYSTEM_A",
			.system_b_default = xstr(NVRAM_MTD_SYSTEM_B), .system_b_env = "NVRAM_MTD_SYSTEM_B",
			.user_a_default = xstr(NVRAM_MTD_USER_A), .user_a_env = "NVRAM_MTD_USER_A",
			.user_b_default = xstr(NVRAM_MTD_USER_B), .user_b_env = "NVRAM_MTD_USER_B",
		},
#endif
#if NVRAM_INTERFACE_EFI > 0
		{.name = "efi", .interface = &nvram_efi_interface,
			.system_a_default = xstr(NVRAM_EFI_SYSTEM_A), .system_a_env = "NVRAM_EFI_SYSTEM_A",
			.system_b_default = xstr(NVRAM_EFI_SYSTEM_B), .system_b_env = "NVRAM_EFI_SYSTEM_B",
			.user_a_default = xstr(NVRAM_EFI_USER_A), .user_a_env = "NVRAM_EFI_USER_A",
			.user_b_default = xstr(NVRAM_EFI_USER_B), .user_b_env = "NVRAM_EFI_USER_B",
		},
#endif
		{.name = NULL},
};

static const char* get_env_str(const char* env, const char* def)
{
	const char *str = getenv(env);
	if (str)
		return str;
	return def;
}

struct nvram_interface* nvram_get_interface(const char* interface_name)
{
	struct interface_desc* desc = &available_interfaces[0];
	while (desc->name != NULL) {
		if (strcmp(desc->name, interface_name) == 0)
			return desc->interface;
		desc++;
	}
	return NULL;
}

const char* nvram_get_interface_section(const char* interface_name, enum section section)
{
	struct interface_desc* desc = &available_interfaces[0];
	while (desc->name != NULL) {
		if (strcmp(desc->name, interface_name) == 0) {
			switch (section) {
			case SYSTEM_A:
				return get_env_str(desc->system_a_env, desc->system_a_default);
			case SYSTEM_B:
				return get_env_str(desc->system_b_env, desc->system_b_default);
			case USER_A:
				return get_env_str(desc->user_a_env, desc->user_a_default);
			case USER_B:
				return get_env_str(desc->user_b_env, desc->user_b_default);
			}
			break;
		}
		desc++;
	}
	return NULL;
}

