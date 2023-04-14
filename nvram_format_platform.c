#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <assert.h>
#include "log.h"
#include "nvram_format.h"
#include "nvram_interface.h"
#include "libnvram/libnvram.h"

#ifdef NVRAM_PLATFORM_VERSION
static const uint32_t HEADER_VERSION = NVRAM_PLATFORM_VERSION;
#else
static const uint32_t HEADER_VERSION = 0;
#endif
#if NVRAM_PLATFORM_WRITE > 0
static const int ALLOW_WRITE = 1;
#else
static const int ALLOW_WRITE = 0;
#endif

/*
 * All fields LITTLE ENDIAN
 *
 * Re-ordering of fields not allowed due to backwards compatibility.
 *
 * Increment version when adding fields.
 */
struct platform_header {
	/*
	 * Shall contain value HEADER_MAGIC.
	 */
#define HEADER_MAGIC 0x54414c50
	uint32_t magic;
	/*
	 * Header version -- starting from 0.
	 */
	uint32_t version;
	/*
	 * Name of platform, null terminated
	 */
	char name[64]; //NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
	/*
	 * Reserved fields for future versions.
	 * All shall be set to 0.
	 */
	uint32_t rsvd[237]; //NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
	/*
	 * crc32 of header, not including field hdr_crc32.
	 */
	uint32_t hdr_crc32;
};
#define PLATFORM_HEADER_SIZE 1024
static_assert(sizeof(struct platform_header) == PLATFORM_HEADER_SIZE, "Platform header invalid size");

struct nvram {
	struct nvram_interface* interface;
	struct nvram_priv* interface_priv;
};

static void platform_close(struct nvram** nvram)
{
	if (nvram && *nvram) {
		struct nvram *pnvram = *nvram;
		if (pnvram->interface_priv)
			pnvram->interface->destroy(&pnvram->interface_priv);
		free(*nvram);
		*nvram = NULL;
	}
}

// le length must be 4
static uint32_t letou32(const uint8_t* le)
{
	return (uint32_t) le[3] << 24 | le[2] << 16 | le[1] << 8 | le[0];
}

#define MEMBER_SIZE(type, member) sizeof(((type *)0)->member)

/* Returns 0 if valid */
static int parse_header(struct platform_header* header, const uint8_t* buf, size_t size)
{
	if (size != PLATFORM_HEADER_SIZE)
		return -EINVAL;

	header->hdr_crc32 = letou32(buf + offsetof(struct platform_header, hdr_crc32));
	const uint32_t crc32 = libnvram_crc32(buf, offsetof(struct platform_header, hdr_crc32));
	if (header->hdr_crc32 != crc32)
		return -EINVAL;

	header->magic = letou32(buf + offsetof(struct platform_header, magic));
	if (header->magic != HEADER_MAGIC)
		return -EINVAL;

	header->version = letou32(buf + offsetof(struct platform_header, version));

	const size_t name_len = MEMBER_SIZE(struct platform_header, name);
	memcpy(header->name, buf + offsetof(struct platform_header, name), name_len);
	/* Verify null-terminator present */
	if (strnlen(header->name, name_len) == name_len)
		return -EINVAL;

	const size_t rsvd_len = MEMBER_SIZE(struct platform_header, rsvd);
	memcpy(header->rsvd, buf + offsetof(struct platform_header, rsvd), rsvd_len);

	return 0;
}

static int header_to_list(struct libnvram_list** list, const struct platform_header* header)
{
	struct libnvram_entry entry;
	int r = 0;

	switch (header->version) {
	case 0:
		entry.key = (uint8_t*) "name";
		entry.key_len = strlen((char*) entry.key) + 1;
		entry.value = (uint8_t*) header->name;
		entry.value_len = strlen((char*) entry.value) + 1;
		r = libnvram_list_set(list, &entry);
		if (r != 0)
			return -ENOMEM;
		break;
	default:
		pr_err("Unknown header version: %" PRIu32 "\n", header->version);
		return -EINVAL;
	}

	return 0;
}

static int list_to_header(const struct libnvram_list* list, struct platform_header* header)
{
	header->magic = HEADER_MAGIC;
	header->version = HEADER_VERSION;

	for (struct libnvram_list* it = libnvram_list_begin(list); it != libnvram_list_end(list); it = libnvram_list_next(it)) {
		const struct libnvram_entry* entry = libnvram_list_deref(it);
		switch (header->version) {
		case 0:
			if (strcmp((char*) entry->key, "name") == 0) {
				if (entry->value_len > MEMBER_SIZE(struct platform_header, name)) {
					pr_err("key \"name\" too long value: max %zu\n", MEMBER_SIZE(struct platform_header, name));
					return -EINVAL;
				}
				memcpy(header->name, entry->value, entry->value_len);
			}
			else {
				pr_err("Unknown field: %s\n", (char*) entry->key);
				return -EINVAL;
			}
			break;
		default:
			pr_err("Unknown header version: %" PRIu32 "\n", header->version);
			return -EINVAL;
		}
	}

	return 0;
}

static void u32tole(uint32_t host, uint8_t* le)
{
	le[0] = host & 0xff;
	le[1] = (host >> 8) & 0xff;
	le[2] = (host >> 16) & 0xff;
	le[3] = (host >> 24) & 0xff;
}

static int serialize_header(const struct platform_header* header, uint8_t* buf, size_t size)
{
	if (size != PLATFORM_HEADER_SIZE)
		return -EINVAL;

	memset(buf, 0, size);

	u32tole(header->magic, buf + offsetof(struct platform_header, magic));
	u32tole(header->version, buf + offsetof(struct platform_header, version));
	memcpy(buf + offsetof(struct platform_header, name), header->name, MEMBER_SIZE(struct platform_header, name));

	const uint32_t crc32 = libnvram_crc32(buf, offsetof(struct platform_header, hdr_crc32));
	u32tole(crc32, buf + offsetof(struct platform_header, hdr_crc32));

	pr_dbg("header content:\n");
	pr_dbg("  magic:      0x%" PRIx32 "\n", header->magic);
	pr_dbg("  version:    %u\n", header->version);
	pr_dbg("  name:       %s\n", header->name);
	pr_dbg("  hdr_crc32:  0x%" PRIx32 "\n", crc32);

	return 0;
}

static int platform_init(struct nvram** nvram, struct nvram_interface* interface, struct libnvram_list** list, const char* section_a, const char* section_b)
{
	if (!section_a || strlen(section_a) < 1)
		return -EINVAL;
	if (section_b && strlen(section_b) > 0) {
		pr_err("platform interface supports single (A) section only\n");
		return -EINVAL;
	}
	struct nvram *pnvram = malloc(sizeof(struct nvram));
	if (!pnvram)
		return -ENOMEM;
	memset(pnvram, 0, sizeof(struct nvram));
	pnvram->interface = interface;

	int r = 0;
	size_t size = 0;
	uint8_t* buf = NULL;
	struct platform_header header;

	r = pnvram->interface->init(&pnvram->interface_priv, section_a);
	if (r != 0) {
		pr_err("%s: failed initializing [%d]: %s\n", section_a, -r, strerror(-r));
		goto exit;
	}
	r = pnvram->interface->size(pnvram->interface_priv, &size);
	if (r != 0) {
		pr_err("%s: failed checking size [%d]: %s\n", section_a, -r, strerror(-r));
		goto exit;
	}

	/* Can't be valid if too small */
	if (size >= PLATFORM_HEADER_SIZE) {
		buf = malloc(PLATFORM_HEADER_SIZE);
		if (buf == NULL) {
			r = -ENOMEM;
			goto exit;
		}
		r = pnvram->interface->read(pnvram->interface_priv, buf, PLATFORM_HEADER_SIZE);
		if (r != 0) {
			pr_err("%s: failed reading [%d]: %s\n", section_a, -r, strerror(-r));
			goto exit;
		}
		if (parse_header(&header, buf, PLATFORM_HEADER_SIZE) == 0) {
			if (header.version > HEADER_VERSION) {
				pr_err("%s: found header version [%u] greater than supported version [%u]\n",
						section_a, header.version, HEADER_VERSION)
				r = -EINVAL;
				goto exit;
			}
			pr_dbg("header valid\n");
			r = header_to_list(list, &header);
			if (r) {
				pr_err("%s: Failed populating list from header [%d]: %s\n", section_a, -r, strerror(-r))
				goto exit;
			}
		}
		else {
			pr_dbg("header invalid\n");
		}
	}
	else {
		pr_dbg("header not found\n");
	}

	*nvram = pnvram;
	r = 0;

exit:
	if (buf != NULL)
		free(buf);
	if (r != 0)
		platform_close(&pnvram);
	return r;
}

static int platform_commit(struct nvram* nvram, const struct libnvram_list* list)
{
	if (ALLOW_WRITE != 1)
		return -EACCES;

	struct platform_header header;
	int r = list_to_header(list, &header);
	if (r != 0)
		return r;

	uint8_t* buf = malloc(PLATFORM_HEADER_SIZE);
	if (buf == NULL)
		return -ENOMEM;

	r = serialize_header(&header, buf, PLATFORM_HEADER_SIZE);
	if (r != 0) {
		pr_err("Failed serializing header [%d]: %s\n", -r, strerror(-r));
		goto exit;
	}

	r = nvram->interface->write(nvram->interface_priv, buf, PLATFORM_HEADER_SIZE);
	if (r != 0) {
		pr_err("%s: Failed writing header [%d]: %s\n",
				nvram->interface->section(nvram->interface_priv), -r, strerror(-r));
		goto exit;
	}

	r = 0;
exit:
	if (buf != NULL)
		free(buf);
	return r;
}

/* Exposed by nvram_format.c */
struct nvram_format nvram_platform_format =
{
	.init = platform_init,
	.commit = platform_commit,
	.close = platform_close,
};
