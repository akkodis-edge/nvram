#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "log.h"
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
	/* Find out what type of file we're dealing with */
	struct stat sb;
	if (stat(priv->path, &sb) != 0) {
		if (errno == ENOENT) {
			*size = 0;
			return 0;
		}
		return -errno;
	}

	if (S_ISREG(sb.st_mode)) {
		pr_dbg("%s: regular file\n", priv->path);
		*size = sb.st_size;
	}
	else if (S_ISBLK(sb.st_mode)) {
		pr_dbg("%s: blockdev\n", priv->path);
		const int fd = open(priv->path, O_RDONLY);
		if (fd < 0)
			return -errno;
		__u64 bytes = 0;
		const int io_r = ioctl(fd, BLKGETSIZE64, &bytes);
		const int io_errno = errno;
		close(fd);
		if (io_r != 0)
			return -io_errno;
		if (bytes > SIZE_MAX)
			return -ENOMEM;
		*size = bytes;
	}
	else {
		pr_dbg("unsupported file format\n");
		return -EOPNOTSUPP;
	}

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
