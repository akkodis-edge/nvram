#ifndef NVRAM_INTERFACE_H_
#define NVRAM_INTERFACE_H_

#include <stdint.h>
#include <stddef.h>

/* Private data for usage by interface */
struct nvram_priv;

struct nvram_interface {
	/*
	 * Initialize nvram interface
	 *
	 * @params
	 *   priv: private data
	 *   section: String (i.e. path) for section A. The pointer must remain valid until
	 *            nvram_interface_destroy is called.
	 *
	 * @returns
	 *   0 for success
	 *   negative errno for error
	 */
	int (*init)(struct nvram_priv** priv, const char* section);

	/*
	 * Free allocated resources
	 */
	void (*destroy)(struct nvram_priv** priv);

	/*
	 * Get size needed to be read
	 *
	 * @params
	 *   priv: private data
	 *   size: size returned
	 *
	 * @returns
	 *   0 for success
	 *   negative errno for error
	 */
	int (*size)(const struct nvram_priv* priv, size_t* size);

	/*
	 * Read from nvram device into buffer
	 *
	 * @params
	 *   priv: private data
	 *   buf: Read buffer
	 *   size: Size of read buffer
	 *
	 * @returns
	 *   0 for success (All "size" bytes read)
	 *   negative errno for error
	 */
	int (*read)(struct nvram_priv* priv, uint8_t* buf, size_t size);

	/*
	 * Write from buffer into nvram device
	 *
	 * @params
	 *   priv: private data
	 *   section: Section to operate on
	 *   buf: write buffer
	 *   size: Size of write buffer
	 *
	 * @returns
	 *   0 for success (All "size" bytes written)
	 *   negative errno for error
	 */
	int (*write)(struct nvram_priv* priv, const uint8_t* buf, size_t size);

	/*
	 * Get section string from interface
	 *
	 * @params
	 *   priv: private data
	 *
	 * @returns
	 *   pointer to path string
	 *   NULL if unavailable
	 */
	const char* (*section)(const struct nvram_priv* priv);
};

/* Returns NULL if not found */
struct nvram_interface* nvram_get_interface(const char* interface_name);

enum section {
	SYSTEM_A,
	SYSTEM_B,
	USER_A,
	USER_B,
};
/* Returns NULL if not found */
const char* nvram_get_interface_section(const char* interface_name, enum section section);

#endif // NVRAM_INTERFACE_H_
