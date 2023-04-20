#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <zlib.h>
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
 *
 * Care should be taken when adding new fields as
 * previous versions of header will have it written as 0
 */
struct platform_header {
	/*
	 * Shall contain value HEADER_MAGIC.
	 */
#define HEADER_MAGIC 0x54414c50
	uint32_t hdr_magic;
	/*
	 * Header version -- starting from 0.
	 */
	uint32_t hdr_version;
	/*
	 * Name of platform, null terminated
	 */
	char name[64]; //NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
	/*
	 * Dram configuration blob.
	 * - blob offset from start of header.
	 * - size of blob
	 * - type of blob
	 * - crc32 of blob
	 */
	uint32_t ddrc_blob_offset;
	uint32_t ddrc_blob_size;
	uint32_t ddrc_blob_type;
	uint32_t ddrc_blob_crc32;
	/*
	 * Dram size in bytes
	 */
	uint64_t ddrc_size;
	/*
	 * Configuration fields for defining hardware capabilities.
	 */
	uint32_t config1;
	uint32_t config2;
	uint32_t config3;
	uint32_t config4;
	/*
	 * Reserved fields for future versions.
	 * All shall be set to 0.
	 */
	uint32_t rsvd[227]; //NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
	/*
	 * crc32 of header, not including field hdr_crc32.
	 * zlib crc32 format.
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

/* Used for array indexing */
enum field_name {
	FIELD_NAME_NAME = 0,
	FIELD_NAME_DDRC_BLOB_OFFSET,
	FIELD_NAME_DDRC_BLOB_SIZE,
	FIELD_NAME_DDRC_BLOB_TYPE,
	FIELD_NAME_DDRC_BLOB_CRC32,
	FIELD_NAME_DDRC_SIZE,
	FIELD_NAME_CONFIG1,
	FIELD_NAME_CONFIG2,
	FIELD_NAME_CONFIG3,
	FIELD_NAME_CONFIG4,
	FIELD_NAME_NUM_FIELDS, /* Used for array size */
};

enum field_type {
	FIELD_TYPE_U32,
	FIELD_TYPE_U64,
	FIELD_TYPE_STRING,
};

struct field {
	char* key;
	enum field_type type;
};

static const struct field fields[] = {
	[FIELD_NAME_NAME]				= {.key = "name", .type = FIELD_TYPE_STRING},
	[FIELD_NAME_DDRC_BLOB_OFFSET]	= {.key = "ddrc_blob_offset", .type = FIELD_TYPE_U32},
	[FIELD_NAME_DDRC_BLOB_SIZE]		= {.key = "ddrc_blob_size", .type = FIELD_TYPE_U32},
	[FIELD_NAME_DDRC_BLOB_TYPE]		= {.key = "ddrc_blob_type", .type = FIELD_TYPE_U32},
	[FIELD_NAME_DDRC_BLOB_CRC32]	= {.key = "ddrc_blob_crc32", .type = FIELD_TYPE_U32},
	[FIELD_NAME_DDRC_SIZE]			= {.key = "ddrc_size", .type = FIELD_TYPE_U64},
	[FIELD_NAME_CONFIG1]			= {.key = "config1", .type = FIELD_TYPE_U32},
	[FIELD_NAME_CONFIG2]			= {.key = "config2", .type = FIELD_TYPE_U32},
	[FIELD_NAME_CONFIG3]			= {.key = "config3", .type = FIELD_TYPE_U32},
	[FIELD_NAME_CONFIG4]			= {.key = "config4", .type = FIELD_TYPE_U32},
};
#define ARRAY_SIZE(a) ((sizeof(a) / sizeof(*(a))))
static_assert(ARRAY_SIZE(fields) == FIELD_NAME_NUM_FIELDS, "Not all fields defined");

static const enum field_name version_0_fields[] = {
	FIELD_NAME_NAME,
	FIELD_NAME_DDRC_BLOB_OFFSET,
	FIELD_NAME_DDRC_BLOB_SIZE,
	FIELD_NAME_DDRC_BLOB_TYPE,
	FIELD_NAME_DDRC_BLOB_CRC32,
	FIELD_NAME_DDRC_SIZE,
	FIELD_NAME_CONFIG1,
	FIELD_NAME_CONFIG2,
	FIELD_NAME_CONFIG3,
	FIELD_NAME_CONFIG4,
};

union data {
	uint32_t u32;
	uint64_t u64;
	char* str;
};

static uint32_t letou32(const uint8_t* le)
{
	return (uint32_t) le[3] << 24 | le[2] << 16 | le[1] << 8 | le[0];
}

static uint64_t letou64(const uint8_t* le)
{
	return (uint64_t) le[7] << 56 | (uint64_t) le[6] << 48 | (uint64_t) le[5] << 40 | (uint64_t) le[4] << 32
					| (uint64_t) le[3] << 24 | (uint64_t) le[2] << 16 | (uint64_t) le[1] << 8 | (uint64_t) le[0];
}

#define MEMBER_SIZE(type, member) sizeof(((type *)0)->member)

/* Returns 0 if valid */
static int parse_header(struct platform_header* header, const uint8_t* buf, size_t size)
{
	if (size != PLATFORM_HEADER_SIZE)
		return -EINVAL;

	/* header */
	header->hdr_crc32 = letou32(buf + offsetof(struct platform_header, hdr_crc32));
	const uint32_t crc32_init = crc32(0L, Z_NULL, 0);
	const uint32_t crc32_calc = crc32(crc32_init, buf, offsetof(struct platform_header, hdr_crc32));
	if (header->hdr_crc32 != crc32_calc)
		return -EINVAL;
	header->hdr_magic = letou32(buf + offsetof(struct platform_header, hdr_magic));
	if (header->hdr_magic != HEADER_MAGIC)
		return -EINVAL;
	header->hdr_version = letou32(buf + offsetof(struct platform_header, hdr_version));

	/* data */
	const size_t name_len = MEMBER_SIZE(struct platform_header, name);
	memcpy(header->name, buf + offsetof(struct platform_header, name), name_len);
	/* Verify null-terminator present */
	if (strnlen(header->name, name_len) == name_len)
		return -EINVAL;
	header->ddrc_blob_offset = letou32(buf + offsetof(struct platform_header, ddrc_blob_offset));
	header->ddrc_blob_size = letou32(buf + offsetof(struct platform_header, ddrc_blob_size));
	header->ddrc_blob_type = letou32(buf + offsetof(struct platform_header, ddrc_blob_type));
	header->ddrc_blob_crc32 = letou32(buf + offsetof(struct platform_header, ddrc_blob_crc32));
	header->ddrc_size = letou64(buf + offsetof(struct platform_header, ddrc_size));
	header->config1 = letou32(buf + offsetof(struct platform_header, config1));
	header->config2 = letou32(buf + offsetof(struct platform_header, config2));
	header->config3 = letou32(buf + offsetof(struct platform_header, config3));
	header->config4 = letou32(buf + offsetof(struct platform_header, config4));

	/* reserved */
	const size_t rsvd_len = MEMBER_SIZE(struct platform_header, rsvd);
	memcpy(header->rsvd, buf + offsetof(struct platform_header, rsvd), rsvd_len);

	return 0;
}

static int value_to_list(const struct platform_header* header, enum field_name name, const struct field* field, struct libnvram_list** list)
{
	/* Well enough to hold both x32 and x64 string representations */
	const int buf_size = 64;
	uint8_t buf[buf_size];
	struct libnvram_entry entry;
	int r = 0;
	union data data;

	switch (name) {
	case FIELD_NAME_NAME:
		if (field->type != FIELD_TYPE_STRING)
			return -EBADF;
		data.str = (char*) header->name;
		break;
	case FIELD_NAME_DDRC_BLOB_OFFSET:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		data.u32 = header->ddrc_blob_offset;
		break;
	case FIELD_NAME_DDRC_BLOB_SIZE:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		data.u32 = header->ddrc_blob_size;
		break;
	case FIELD_NAME_DDRC_BLOB_TYPE:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		data.u32 = header->ddrc_blob_type;
		break;
	case FIELD_NAME_DDRC_BLOB_CRC32:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		data.u32 = header->ddrc_blob_crc32;
		break;
	case FIELD_NAME_DDRC_SIZE:
		if (field->type != FIELD_TYPE_U64)
			return -EBADF;
		data.u64 = header->ddrc_size;
		break;
	case FIELD_NAME_CONFIG1:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		data.u32 = header->config1;
		break;
	case FIELD_NAME_CONFIG2:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		data.u32 = header->config2;
		break;
	case FIELD_NAME_CONFIG3:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		data.u32 = header->config3;
		break;
	case FIELD_NAME_CONFIG4:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		data.u32 = header->config4;
		break;
	default:
		pr_err("Unknown field id [%d] with key \"%s\"\n", name, field->key != NULL ? field->key : "");
		return -EINVAL;
	}

	entry.key = (uint8_t*) field->key;
	entry.key_len = strlen(field->key) + 1;

	switch (field->type) {
	case FIELD_TYPE_U32:
		r = snprintf((char*) buf, buf_size, "0x%" PRIx32"", data.u32);
		if (r >= buf_size) {
			pr_err("failed converting u32 to string\n");
			return -EINVAL;
		}
		entry.value = buf;
		entry.value_len = r + 1;
		break;
	case FIELD_TYPE_U64:
		r = snprintf((char*) buf, buf_size, "0x%" PRIx64"", data.u64);
		if (r >= buf_size) {
			pr_err("failed converting u64 to string\n");
			return -EINVAL;
		}
		entry.value = buf;
		entry.value_len = r + 1;
		break;
	case FIELD_TYPE_STRING:
		entry.value = (uint8_t*) data.str;
		entry.value_len = strlen(data.str) + 1;
		break;
	}

	if (libnvram_list_set(list, &entry) != 0) {
		pr_err("Failed adding entry to list\n");
		return -ENOMEM;
	}

	return 0;
}

static int header_to_list_version_iterator(const struct platform_header* header, const enum field_name* version_fields, size_t len, struct libnvram_list** list)
{
	for (size_t i = 0; i < len; ++i) {
		const enum field_name field_index = version_fields[i];
		const struct field* field = &fields[field_index];
		int r = value_to_list(header, field_index, field, list);
		if (r != 0)
			return r;
	}
	return 0;
}

static int header_to_list(struct libnvram_list** list, const struct platform_header* header)
{
	int r = 0;

	switch (header->hdr_version) {
	/* Example of adding header version 1:
	 * case 1:
	 *     r = header_to_list_version_iterator(header, version_1_fields, ARRAY_SIZE(version_1_fields), list);
	 *     if (r != 0)
	 *         return r;
	 *     [[FALLTHROUGH]
	 * */
	case 0:
		r = header_to_list_version_iterator(header, version_0_fields, ARRAY_SIZE(version_0_fields), list);
		if (r != 0)
			return r;
		break;
	default:
		pr_err("Unknown header version: %" PRIu32 "\n", header->hdr_version);
		return -EINVAL;
	}

	return 0;
}

static_assert(sizeof(unsigned long int) >= sizeof(uint32_t), "unsigned long smaller than uint32_t, format conversion invalid");
static_assert(sizeof(unsigned long long int) >= sizeof(uint64_t), "unsigned long long smaller than uint64_t, format conversion invalid");
static int value_to_header(struct platform_header* header, enum field_name name, const struct field* field, const struct libnvram_entry* entry)
{
	/* Ensure value to parse is null terminated */
	if (entry->value[entry->value_len - 1] != '\0') {
		pr_err("field id [%d] with key \"%s\" not of type string\n", name, field->key);
		return -EINVAL;
	}
	union data data;
	switch (field->type) {
	case FIELD_TYPE_U32:
		{
			char* endptr = NULL;
			const unsigned long int val = strtoul((char*) entry->value, &endptr, 0);
			if ((val == 0 && endptr == NULL)
				|| (val == ULONG_MAX && errno == ERANGE)
				|| (val > UINT32_MAX)) {
				pr_err("field id [%d] with key \"%s\" not of type u32\n", name, field->key);
				return -EINVAL;
			}
			data.u32 = val;
		}
		break;
	case FIELD_TYPE_U64:
	{
		char* endptr = NULL;
		const unsigned long long int val = strtoull((char*) entry->value, &endptr, 0);
		if ((val == 0 && endptr == NULL)
			|| (val == ULLONG_MAX && errno == ERANGE)
			|| (val > UINT64_MAX)) {
			pr_err("field id [%d] with key \"%s\" not of type u64\n", name, field->key);
			return -EINVAL;
		}
		data.u64 = val;
	}
		break;
	case FIELD_TYPE_STRING:
		data.str = (char*) entry->value;
		break;
	}

	switch (name) {
	case FIELD_NAME_NAME:
		if (field->type != FIELD_TYPE_STRING)
			return -EBADF;
		if (entry->value_len > MEMBER_SIZE(struct platform_header, name)) {
			pr_err("field id [%d] with key \"%s\" too long value\n", name, field->key);
			return -EINVAL;
		}
		memcpy(header->name, (char*) entry->value, entry->value_len);
		break;
	case FIELD_NAME_DDRC_BLOB_OFFSET:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		header->ddrc_blob_offset = data.u32;
		break;
	case FIELD_NAME_DDRC_BLOB_SIZE:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		header->ddrc_blob_size = data.u32;
		break;
	case FIELD_NAME_DDRC_BLOB_TYPE:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		header->ddrc_blob_type = data.u32;
		break;
	case FIELD_NAME_DDRC_BLOB_CRC32:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		header->ddrc_blob_crc32 = data.u32;
		break;
	case FIELD_NAME_DDRC_SIZE:
		if (field->type != FIELD_TYPE_U64)
			return -EBADF;
		header->ddrc_size = data.u64;
		break;
	case FIELD_NAME_CONFIG1:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		header->config1 = data.u32;
		break;
	case FIELD_NAME_CONFIG2:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		header->config2 = data.u32;
		break;
	case FIELD_NAME_CONFIG3:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		header->config3 = data.u32;
		break;
	case FIELD_NAME_CONFIG4:
		if (field->type != FIELD_TYPE_U32)
			return -EBADF;
		header->config4 = data.u32;
		break;
	default:
		pr_err("Unknown field id [%d] with key \"%s\"\n", name, field->key);
		return -EINVAL;
	}

	return 0;
}

/* return 1 if found, 0 if not found, < 0 for error */
static int list_to_header_version_iterator(struct platform_header* header, const enum field_name* version_fields, size_t len, const struct libnvram_entry* entry)
{
	for (size_t i = 0; i < len; ++i) {
		const enum field_name field_index = version_fields[i];
		const struct field* field = &fields[field_index];
		if (strncmp(field->key, (char*) entry->key, entry->key_len) == 0) {
			int r = value_to_header(header, field_index, field, entry);
			if (r != 0)
				return r;
			return 1;
		}
	}
	return 0;
}

static int list_to_header(const struct libnvram_list* list, struct platform_header* header)
{
	header->hdr_magic = HEADER_MAGIC;
	header->hdr_version = HEADER_VERSION;

	int r = 0;

	for (struct libnvram_list* it = libnvram_list_begin(list); it != libnvram_list_end(list); it = libnvram_list_next(it)) {
		const struct libnvram_entry* entry = libnvram_list_deref(it);
		switch (header->hdr_version) {
			/* Example of adding header version 1:
			 * case 1:
			 *     r = list_to_header_version_iterator(header, version_1_fields, ARRAY_SIZE(version_1_fields), entry);
			 *     if (r < 0)
			 *         return r;
			 *     [[FALLTHROUGH]]
			 *      --- Allow r == 0 if key not found, version0 will return with error if key still not resolved.
			 * */
		case 0:
			r = list_to_header_version_iterator(header, version_0_fields, ARRAY_SIZE(version_0_fields), entry);
			if (r < 0)
				return r;
			if (r == 0) {
				pr_err("field with key \"%s\" unresolved\n", (char*) entry->key);
				return -EINVAL;
			}
			break;
		default:
			pr_err("Unknown header version: %" PRIu32 "\n", header->hdr_version);
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

static void u64tole(uint64_t host, uint8_t* le)
{
	le[0] = host & 0xff;
	le[1] = (host >> 8) & 0xff;
	le[2] = (host >> 16) & 0xff;
	le[3] = (host >> 24) & 0xff;
	le[4] = (host >> 32) & 0xff;
	le[5] = (host >> 40) & 0xff;
	le[6] = (host >> 48) & 0xff;
	le[7] = (host >> 56) & 0xff;
}

static int serialize_header(const struct platform_header* header, uint8_t* buf, size_t size)
{
	if (size != PLATFORM_HEADER_SIZE)
		return -EINVAL;

	memset(buf, 0, size);

	u32tole(header->hdr_magic, buf + offsetof(struct platform_header, hdr_magic));
	u32tole(header->hdr_version, buf + offsetof(struct platform_header, hdr_version));

	memcpy(buf + offsetof(struct platform_header, name), header->name, MEMBER_SIZE(struct platform_header, name));
	u32tole(header->ddrc_blob_offset, buf + offsetof(struct platform_header, ddrc_blob_offset));
	u32tole(header->ddrc_blob_size, buf + offsetof(struct platform_header, ddrc_blob_size));
	u32tole(header->ddrc_blob_type, buf + offsetof(struct platform_header, ddrc_blob_type));
	u32tole(header->ddrc_blob_crc32, buf + offsetof(struct platform_header, ddrc_blob_crc32));
	u64tole(header->ddrc_size, buf + offsetof(struct platform_header, ddrc_size));
	u32tole(header->config1, buf + offsetof(struct platform_header, config1));
	u32tole(header->config2, buf + offsetof(struct platform_header, config2));
	u32tole(header->config3, buf + offsetof(struct platform_header, config3));
	u32tole(header->config4, buf + offsetof(struct platform_header, config4));

	const uint32_t crc32_init = crc32(0L, Z_NULL, 0);
	const uint32_t crc32_calc = crc32(crc32_init, buf, offsetof(struct platform_header, hdr_crc32));
	u32tole(crc32_calc, buf + offsetof(struct platform_header, hdr_crc32));

	pr_dbg("header content:\n");
	pr_dbg("  hdr_magic:             0x%" PRIx32 "\n", header->hdr_magic);
	pr_dbg("  hdr_version:           %u\n", header->hdr_version);
	pr_dbg("  name:              %s\n", header->name);
	pr_dbg("  ddrc_blob_offset:  0x%" PRIx32 "\n", header->ddrc_blob_offset);
	pr_dbg("  ddrc_blob_size:    0x%" PRIx32 "\n", header->ddrc_blob_size);
	pr_dbg("  ddrc_blob_type:    0x%" PRIx32 "\n", header->ddrc_blob_type);
	pr_dbg("  ddrc_blob_crc32:   0x%" PRIx32 "\n", header->ddrc_blob_crc32);
	pr_dbg("  ddrc_size:         0x%" PRIx64 "\n", header->ddrc_size);
	pr_dbg("  config1:           0x%" PRIx32 "\n", header->config1);
	pr_dbg("  config2:           0x%" PRIx32 "\n", header->config2);
	pr_dbg("  config3:           0x%" PRIx32 "\n", header->config3);
	pr_dbg("  config4:           0x%" PRIx32 "\n", header->config4);
	pr_dbg("  hdr_crc32:         0x%" PRIx32 "\n", crc32_calc);

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
			if (header.hdr_version > HEADER_VERSION) {
				pr_err("%s: found header version [%u] greater than supported version [%u]\n",
						section_a, header.hdr_version, HEADER_VERSION)
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
		return -ENOTSUP;

	struct platform_header header;
	memset(&header, 0, sizeof(header));
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
