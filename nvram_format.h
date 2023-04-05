#ifndef NVRAM_FORMAT_H_
#define NVRAM_FORMAT_H_

#include <stdint.h>
#include "libnvram/libnvram.h"
#include "nvram_interface.h"

struct nvram;

struct nvram_format {
	/*
	 * Initialize nvram and get list of variables
	 *
	 * @params
	 *   nvram: private data
	 *   list: returned list
	 *   section_a: String (i.e. path) for section A. The pointer must remain valid during program execution.
	 *   section_b: String (i.e. path) for section B. The pointer must remain valid during program execution.
	 *
	 * @returns
	 *   0 for success
	 *   negative errno for error
	 */
	int (*init)(struct nvram** nvram, struct nvram_interface* interface, struct libnvram_list** list, const char* section_a, const char* section_b);

	/*
	 * Commit list of variables to nvram
	 *
	 * @params
	 *   nvram: private data
	 *   list: list to commit
	 *
	 * @returns
	 *   0 for success
	 *   negative errno for error
	 */
	int (*commit)(struct nvram* nvram, const struct libnvram_list* list);

	/*
	 * Close nvram after usage
	 *
	 * @params
	 *   nvram: private data
	 */
	void (*close)(struct nvram** nvram);
};

/* Returns NULL if not found */
struct nvram_format* nvram_get_format(const char* format_name);

#endif // NVRAM_FORMAT_H_
