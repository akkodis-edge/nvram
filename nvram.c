#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/types.h>
#include "log.h"
#include "nvram.h"
#include "nvram_interface.h"
#include "libnvram/libnvram.h"

struct nvram {
	struct nvram_interface* interface;
	struct libnvram_transaction trans;
	struct nvram_priv* priv_a;
	struct nvram_priv* priv_b;
};

static const char* nvram_active_str(enum libnvram_active active)
{
	switch (active) {
	case LIBNVRAM_ACTIVE_A:
		return "A";
	case LIBNVRAM_ACTIVE_B:
		return "B";
	default:
		return "NONE";
	}
}

static int read_section(struct nvram_interface* interface, struct nvram_priv* priv, uint8_t** data, size_t* len)
{
	size_t size = 0;
	uint8_t *buf = NULL;

	int r = interface->size(priv, &size);
	if (r) {
		pr_err("%s: failed checking size [%d]: %s\n", interface->section(priv), -r, strerror(-r));
		goto error_exit;
	}

	if (size > UINT32_MAX) { // libnvram limitation
		r = -EINVAL;
		pr_err("%s: size %zu larger than limit %u\n", interface->section(priv), size, UINT32_MAX);
		goto error_exit;
	}

	if (size > 0) {
		buf = (uint8_t*) malloc(size);
		if (!buf) {
			r = -ENOMEM;
			pr_err("%s: failed allocating %zu byte read buffer\n", interface->section(priv), size);
			goto error_exit;
		}

		r = interface->read(priv, buf, size);
		if (r) {
			pr_err("%s: failed reading %zu bytes [%d]: %s\n", interface->section(priv), size, -r, strerror(-r));
			goto error_exit;
		}
	}

	*data = buf;
	*len = size;

	return 0;

error_exit:
	if (buf) {
		free(buf);
	}
	return r;
}

static int init_and_read(struct nvram_interface* interface, struct nvram_priv** priv, const char* section, enum libnvram_active name, uint8_t** buf, size_t* size)
{
	pr_dbg("%s: initializing: %s\n", nvram_active_str(name), section);
	int r = interface->init(priv, section);
	if (r) {
		pr_err("%s: failed init [%d]: %s\n", section, -r, strerror(-r));
		return r;
	}
	r = read_section(interface, *priv, buf, size);
	if (r) {
		return r;
	}
	pr_dbg("%s: size: %zu b\n", nvram_active_str(name), *size);

	return 0;
}

int nvram_init(struct nvram** nvram, struct nvram_interface* interface, struct libnvram_list** list, const char* section_a, const char* section_b)
{
	if (interface == NULL || interface->init == NULL || interface->destroy == NULL
		|| interface->size == NULL || interface->read == NULL
		|| interface->write == NULL || interface->section == NULL)
		return EINVAL;

	uint8_t *buf_a = NULL;
	size_t size_a = 0;
	uint8_t *buf_b = NULL;
	size_t size_b = 0;
	struct nvram *pnvram = (struct nvram*) malloc(sizeof(struct nvram));
	if (!pnvram) {
		return -ENOMEM;
	}
	memset(pnvram, 0, sizeof(struct nvram));
	pnvram->interface = interface;

	int r = 0;
	if (section_a && strlen(section_a) > 0) {
		r = init_and_read(pnvram->interface, &pnvram->priv_a, section_a, LIBNVRAM_ACTIVE_A, &buf_a, &size_a);
		if (r) {
			goto exit;
		}
	}
	if (section_b && strlen(section_b) > 0) {
		r = init_and_read(pnvram->interface, &pnvram->priv_b, section_b, LIBNVRAM_ACTIVE_B, &buf_b, &size_b);
		if (r) {
			goto exit;
		}
	}

	libnvram_init_transaction(&pnvram->trans, buf_a, size_a, buf_b, size_b);
	pr_dbg("%s: active\n", nvram_active_str(pnvram->trans.active));
	r = 0;
	if ((pnvram->trans.active & LIBNVRAM_ACTIVE_A) == LIBNVRAM_ACTIVE_A) {
		r = libnvram_deserialize(list, buf_a + libnvram_header_len(), size_a - libnvram_header_len(), &pnvram->trans.section_a.hdr);
	}
	else
	if ((pnvram->trans.active & LIBNVRAM_ACTIVE_B) == LIBNVRAM_ACTIVE_B) {
		r = libnvram_deserialize(list, buf_b + libnvram_header_len(), size_b - libnvram_header_len(), &pnvram->trans.section_b.hdr);
	}

	if (r) {
		pr_err("failed deserializing data [%d]: %s\n", -r, strerror(-r));
		goto exit;
	}

	*nvram = pnvram;

exit:
	if (r) {
		if (pnvram) {
			if (pnvram->priv_a) {
				interface->destroy(&pnvram->priv_a);
			}
			if (pnvram->priv_b) {
				interface->destroy(&pnvram->priv_b);
			}
			free(pnvram);
		}
	}
	if (buf_a) {
		free(buf_a);
	}
	if (buf_b) {
		free(buf_b);
	}

	return r;
}

static int _write(struct nvram_interface* interface, struct nvram_priv* priv, const uint8_t* buf, uint32_t size)
{
	pr_dbg("%s: write: %" PRIu32 " b\n", interface->section(priv), size);
	int r = interface->write(priv, buf, size);
	if (r) {
		pr_err("%s: failed writing %" PRIu32 " b [%d]: %s\n", interface->section(priv), size, -r, strerror(-r));
	}
	return r;
}

int nvram_commit(struct nvram* nvram, const struct libnvram_list* list)
{
	uint8_t *buf = NULL;
	int r = 0;
	uint32_t size = libnvram_serialize_size(list, LIBNVRAM_TYPE_LIST);

	buf = (uint8_t*) malloc(size);
	if (!buf) {
		pr_err("failed allocating %" PRIu32 " byte write buffer\n", size);
		r = -ENOMEM;
		goto exit;
	}

	struct libnvram_header hdr;
	hdr.type = LIBNVRAM_TYPE_LIST;
	enum libnvram_operation op = libnvram_next_transaction(&nvram->trans, &hdr);
	uint32_t bytes = libnvram_serialize(list, buf, size, &hdr);
	if (!bytes) {
		pr_err("failed serializing nvram data\n");
		goto exit;
	}

	if (!nvram->priv_a || !nvram->priv_b) {
		// Transactional write disabled
		r = _write(nvram->interface, nvram->priv_a ? nvram->priv_a : nvram->priv_b, buf, size);
	}
	else {
		const int is_write_a = (op & LIBNVRAM_OPERATION_WRITE_A) == LIBNVRAM_OPERATION_WRITE_A;
		const int is_counter_reset = (op & LIBNVRAM_OPERATION_COUNTER_RESET) == LIBNVRAM_OPERATION_COUNTER_RESET;
		// first write
		r = _write(nvram->interface, is_write_a ? nvram->priv_a : nvram->priv_b, buf, size);
		if (!r && is_counter_reset) {
			// second write, if requested
			r = _write(nvram->interface, is_write_a ? nvram->priv_b : nvram->priv_a, buf, size);
		}
	}
	if (r) {
		goto exit;
	}

	libnvram_update_transaction(&nvram->trans, op, &hdr);

	pr_dbg("%s: active\n", nvram_active_str(nvram->trans.active));

	r = 0;
exit:
	if (buf) {
		free(buf);
	}
	return r;
}

void nvram_close(struct nvram** nvram)
{
	if (nvram && *nvram) {
		struct nvram *pnvram = *nvram;
		if (pnvram->priv_a) {
			pnvram->interface->destroy(&pnvram->priv_a);
		}
		if (pnvram->priv_b) {
			pnvram->interface->destroy(&pnvram->priv_b);
		}
		free(*nvram);
		*nvram = NULL;
	}
}
