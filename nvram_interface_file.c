#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "nvram_interface.h"

struct nvram_priv {
	char *path;
};

static int file_init(struct nvram_priv** priv, const char* section)
{
	if (!section || *priv) {
		return -EINVAL;
	}

	struct nvram_priv *pbuf = malloc(sizeof(struct nvram_priv));
	if (!pbuf) {
		return -ENOMEM;
	}
	pbuf->path = (char*) section;

	*priv = pbuf;

	return 0;
}

static void file_destroy(struct nvram_priv** priv)
{
	if (*priv) {
		free(*priv);
		*priv = NULL;
	}
}

static int file_size(const struct nvram_priv* priv, size_t* size)
{
	struct stat sb;
	if (stat(priv->path, &sb)) {
		switch (errno) {
		case ENOENT:
			*size = 0;
			return 0;
		default:
			return -errno;
		}
	}

	*size = sb.st_size;
	return 0;
}

static int file_read(struct nvram_priv* priv, uint8_t* buf, size_t size)
{
	if (!buf) {
		return -EINVAL;
	}

	int fd = open(priv->path, O_RDONLY);
	if (fd < 0) {
		return -errno;
	}

	int r = 0;
	ssize_t bytes = read(fd, buf, size);
	if (bytes < 0) {
		r = -errno;
		goto exit;
	}
	else
	if ((size_t) bytes != size) {
		r = -EIO;
		goto exit;
	}

exit:
	close(fd);
	return r;
}

static int file_write(struct nvram_priv* priv, const uint8_t* buf, size_t size)
{
	if (!buf) {
		return -EINVAL;
	}

	int fd = open(priv->path, O_WRONLY | O_CREAT, S_IWUSR | S_IRUSR);
	if (fd < 0) {
		return -errno;
	}

	int r = 0;
	ssize_t bytes = write(fd, buf, size);
	if (bytes < 0) {
		r = -errno;
		goto exit;
	}
	else
	if ((size_t) bytes != size) {
		r = -EIO;
		goto exit;
	}

exit:
	close(fd);
	return r;
}

static const char* file_section(const struct nvram_priv* priv)
{
	return priv->path;
}

/* Exposed by nvram_interface.c */
struct nvram_interface nvram_file_interface =
{
	.init = file_init,
	.destroy = file_destroy,
	.size = file_size,
	.read = file_read,
	.write = file_write,
	.section = file_section,
};
