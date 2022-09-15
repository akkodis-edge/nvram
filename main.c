#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include "log.h"
#include "nvram.h"
#include "libnvram/libnvram.h"

#define xstr(a) str(a)
#define str(a) #a

#define NVRAM_PROGRAM_NAME "nvram"
#define NVRAM_LOCKFILE "/run/lock/nvram.lock"
#define NVRAM_ENV_DEBUG "NVRAM_DEBUG"
#define NVRAM_ENV_USER_A "NVRAM_USER_A"
#define NVRAM_ENV_USER_B "NVRAM_USER_B"
#define NVRAM_ENV_SYSTEM_A "NVRAM_SYSTEM_A"
#define NVRAM_ENV_SYSTEM_B "NVRAM_SYSTEM_B"
#define NVRAM_ENV_SYSTEM_UNLOCK "NVRAM_SYSTEM_UNLOCK"
#define NVRAM_SYSTEM_UNLOCK_MAGIC "16440"
#define NVRAM_SYSTEM_PREFIX "SYS_"

static int FDLOCK = 0;

static const char* get_env_str(const char* env, const char* def)
{
	const char *str = getenv(env);
	if (str) {
		return str;
	}

	return def;
}

static long get_env_long(const char* env)
{
	const char *val = getenv(env);
	if (val) {
		char *endptr = NULL;
		return strtol(val, &endptr, 10);
	}
	return 0;
}

static int system_unlocked(void)
{
	const char* unlock_str = getenv(NVRAM_ENV_SYSTEM_UNLOCK);
	if (unlock_str && strcmp(unlock_str, xstr(NVRAM_SYSTEM_UNLOCK_MAGIC))) {
		return 1;
	}
	return 0;
}

static int starts_with(const char* str, const char* prefix)
{
	size_t str_len = strlen(str);
	size_t prefix_len = strlen(prefix);
	if (str_len > prefix_len) {
		if(!strncmp(str, prefix, prefix_len)) {
			return 1;
		}
	}
	return 0;
}

static int acquire_lockfile(const char *path, int *fdlock)
{
    int r = 0;
    int retries = 10;

    int fd = open(path, O_CREAT | O_WRONLY, S_IWUSR | S_IRUSR);
    if (fd < 0) {
        r = errno;
        pr_err("failed opening lockfile: %s [%d]: %s\n", path, r, strerror(r));
        return -r;
    }

    while (retries--) {
        if (flock(fd, LOCK_EX | LOCK_NB)) {
            r = errno;
            if (r != EWOULDBLOCK) {
                pr_err("failed locking lockfile: %s [%d]: %s\n", path, r,
                       strerror(r));
                close(fd);
                return -r;
            }
            else if (retries == 0) {
                pr_err("failed locking lockfile: %s [%d]: %s\n", path, r,
                       strerror(ETIMEDOUT));
                close(fd);
                return -ETIMEDOUT;
            }
            usleep(10000);
        } else {
            break;
        }
    }

    *fdlock = fd;
    pr_dbg("%s: locked\n", path);

    return 0;
}

static int release_lockfile(const char* path, int fdlock)
{
	int r = 0;

	if (fdlock) {
		if(close(fdlock)) {
			r = errno;
			pr_err("failed closing lockfile: %s [%d]: %s", path, r, strerror(r));
			return -r;
		}
		if (remove(path)) {
			r = errno;
			pr_err("failed removing lockfile: %s [%d]: %s", path, r, strerror(r));
			return -r;
		}
	}

	pr_dbg("%s: unlocked\n", path);

	return 0;
}

enum op {
	OP_LIST = 0,
	OP_SET,
	OP_GET,
	OP_DEL,
};

struct operation {
	enum op op;
	char* key;
	char* value;
};

#define MAX_OP 10
struct opts {
	int system_mode;
	int op_count;
	struct operation operations[MAX_OP];
};

static void print_usage(const char* progname)
{
	printf("%s, nvram interface, Data Respons Solutions AB\n", progname);
	printf("Version:   %s\n", xstr(SRC_VERSION));
	printf("Interface: %s\n", xstr(INTERFACE_TYPE));
	printf("\n");

	printf("Default paths:\n");
	printf("system_a: %s\n", xstr(NVRAM_SYSTEM_A));
	printf("system_b: %s\n", xstr(NVRAM_SYSTEM_B));
	printf("user_a:   %s\n", xstr(NVRAM_USER_A));
	printf("user_b:   %s\n", xstr(NVRAM_USER_B));
	printf("\n");

	printf("Usage:   %s [OPTION] [COMMAND] [KEY] [VALUE]\n", progname);
	printf("Example: %s set keyname value\n", progname);
	printf("Defaults to COMMAND list if none set\n");
	printf("\n");

	printf("Options:\n");
	printf("  --sys       ignore user section\n");
	printf("\n");

	printf("Commands:\n");
	printf("  --set       Write attribute. Requires KEY And VALUE\n");
	printf("  --get       Read attribute. Requires KEY\n");
	printf("  --del       Delete attributes. Requires KEY\n");
	printf("  --list      Lists attributes\n");
	printf("\n");

	printf("Return values:\n");
	printf("  0 if ok\n");
	printf("  errno for error\n");
	printf("\n");
}

enum print_options {
	PRINT_VALUE,
	PRINT_KEY_AND_VALUE,
};

static size_t calc_size(const uint32_t len, int is_str)
{
	if (is_str) {
		// null-terminator ignored
		return len - 1;
	}
	else {
		// prefix "0x" + every byte represtended by 2 char in hex
		return 2 + len * 2;
	}
}

// return bytes written, negative errno for error
static int append_hex(char* str, size_t size, uint8_t* data, uint32_t len)
{
	int bytes = 0;
	int r = snprintf(str, size, "0x");
	if (r < 0) {
		return -errno;
	}
	else
	if (r != 2) {
		return -EFAULT;
	}
	bytes += r;
	for (uint32_t i = 0; i < len; ++i) {
		r = snprintf(str + bytes, size - bytes, "%02" PRIx8 "", data[i]);
		if (r < 0) {
			return -errno;
		}
		else
		if (r != 2) {
			return -EFAULT;
		}
		bytes += r;
	}

	return bytes;
}

static int print_entry(const struct libnvram_entry* entry, enum print_options opts)
{
	if (entry->key_len > INT_MAX || entry->value_len > INT_MAX) {
		return -EINVAL;
	}
	size_t size = 2; // min size with null-termination and newline

	const int is_key_str = entry->key[entry->key_len - 1] =='\0';
	if (opts == PRINT_KEY_AND_VALUE) {
		size += 1; // separate key and value with =
		size += calc_size(entry->key_len, is_key_str);
	}

	const int is_value_str = entry->value[entry->value_len - 1] =='\0';
	size += calc_size(entry->value_len, is_value_str);

	char* str = malloc(size);
	if (!str) {
		return -ENOMEM;
	}

	int r = 0;
	size_t pos = 0;
	if (opts == PRINT_KEY_AND_VALUE) {
		if (is_key_str) {
			r = snprintf(str + pos, size - pos, "%s=", (char*) entry->key);
			if (r != (int) entry->key_len) {
				r = -errno;
				goto exit;
			}
			pos += r;
		}
		else {
			r = append_hex(str + pos, size - pos, entry->key, entry->key_len);
			if (r < 0) {
				goto exit;
			}
			pos += r;
			r = snprintf(str + pos, size - pos, "=");
			if (r != 1) {
				r = -errno;
				goto exit;
			}
			pos += r;
		}
	}

	if (is_value_str) {
		r = snprintf(str + pos, size - pos, "%s\n", (char*) entry->value);
		if (r != (int) entry->value_len) {
			r = -errno;
			goto exit;
		}
		pos += r;
	}
	else {
		r = append_hex(str + pos, size - pos, entry->value, entry->value_len);
		if (r < 0) {
			goto exit;
		}
		pos += r;
		r = snprintf(str + pos, size - pos, "\n");
		if (r != 1) {
			r = -errno;
			goto exit;
		}
		pos += r;
	}

	printf("%s", str);
	r = 0;

exit:
	free(str);
	return r;
}


// return 0 for OK or negative errno for error
static int print_list_entry(const char* list_name, const struct libnvram_list* list, const char* key)
{
	pr_dbg("getting key from %s: %s\n", list_name, key);
	struct libnvram_entry *entry = libnvram_list_get(list, (uint8_t*) key, strlen(key) + 1);
	if (!entry) {
		return -ENOENT;
	}

	return print_entry(entry, PRINT_VALUE);
}

static void print_list(const char* list_name, const struct libnvram_list* list)
{
	pr_dbg("listing %s\n", list_name);
	for (struct libnvram_list *cur = (struct libnvram_list*) list; cur; cur = cur->next) {
		print_entry(cur->entry, PRINT_KEY_AND_VALUE);
	}
}

// return 0 for equal
static int keycmp(const uint8_t* key1, uint32_t key1_len, const uint8_t* key2, uint32_t key2_len)
{
	if (key1_len == key2_len) {
		return memcmp(key1, key2, key1_len);
	}
	return 1;
}

// return 0 if already exists, 1 if added, negate errno for error
static int add_list_entry(const char* list_name, struct libnvram_list** list, const char* key, const char* value)
{
	const size_t key_len = strlen(key) + 1;
	const size_t value_len = strlen(value) + 1;

	pr_dbg("setting: %s: %s=%s\n", list_name, key, value);
	struct libnvram_entry *entry = libnvram_list_get(*list, (uint8_t*) key, key_len);
	if (entry && !keycmp(entry->value, entry->value_len, (uint8_t*) value, value_len)) {
		return 0;
	}
	struct libnvram_entry new;
	new.key = (uint8_t*) key;
	new.key_len = key_len;
	new.value = (uint8_t*) value;
	new.value_len = value_len;

	int r = libnvram_list_set(list, &new);
	if (r) {
		pr_err("failed setting to %s list [%d]: %s\n", list_name, -r, strerror(-r));
		return r;
	}

	return 1;
}

int main(int argc, char** argv)
{

	struct opts opts;
	memset(&opts, 0, sizeof(opts));

	int r = 0;

	if (get_env_long(NVRAM_ENV_DEBUG)) {
		enable_debug();
	}

	for (int i = 1; i < argc; i++) {
		if (!strcmp("--set", argv[i])) {
			if (i + 2 >= argc) {
				fprintf(stderr, "Too few arguments for command set\n");
				return EINVAL;
			}
			if (opts.op_count >= MAX_OP) {
				fprintf(stderr, "Too many operations\n");
				return EINVAL;
			}
			opts.operations[opts.op_count].op = OP_SET;
			opts.operations[opts.op_count].key = argv[i + 1];
			opts.operations[opts.op_count].value = argv[i + 2];
			opts.op_count++;
			i += 2;
		}
		else
		if (!strcmp("--get", argv[i])) {
			if (++i >= argc) {
				fprintf(stderr, "Too few arguments for command get\n");
				return EINVAL;
			}
			if (opts.op_count >= MAX_OP) {
				fprintf(stderr, "Too many operations\n");
				return EINVAL;
			}
			opts.operations[opts.op_count].op = OP_GET;
			opts.operations[opts.op_count].key = argv[i];
			opts.op_count++;
		}
		else
		if (!strcmp("--list", argv[i])) {
			if (opts.op_count >= MAX_OP) {
				fprintf(stderr, "Too many operations\n");
				return EINVAL;
			}
			opts.operations[opts.op_count].op = OP_LIST;
			opts.op_count++;
		}
		else
		if(!strcmp("--del", argv[i])) {
			if (++i >= argc) {
				fprintf(stderr, "Too few arguments for command delete\n");
				return EINVAL;
			}
			if (opts.op_count >= MAX_OP) {
				fprintf(stderr, "Too many operations\n");
				return EINVAL;
			}
			opts.operations[opts.op_count].op = OP_DEL;
			opts.operations[opts.op_count].key = argv[i];
			opts.op_count++;
		}
		else
		if (!strcmp("--sys", argv[i])) {
			opts.system_mode = 1;
		}
		else
		if (!strcmp("-h", argv[i]) || !strcmp("--help", argv[i])) {
			print_usage(NVRAM_PROGRAM_NAME);
			return 1;
		}
		else {
			fprintf(stderr, "unknown argument: %s\n", argv[i]);
			return 1;
		}
	}

	if (opts.op_count == 0) {
		opts.operations[0].op = OP_LIST;
		opts.op_count++;
	}

	int read_ops = 0;
	int write_ops = 0;
	pr_dbg("system_mode: %d\n", opts.system_mode);
	for (int i = 0; i < opts.op_count; ++i) {
		pr_dbg("operation: %d, key: %s, val: %s\n",
				opts.operations[i].op, opts.operations[i].key, opts.operations[i].value);
		switch (opts.operations[i].op) {
		case OP_SET:
			if (opts.system_mode) {
				if (!starts_with(opts.operations[i].key, NVRAM_SYSTEM_PREFIX)) {
					pr_err("required prefix \"%s\" missing in system attribute\n", NVRAM_SYSTEM_PREFIX);
					return EINVAL;
				}
				if (!system_unlocked()) {
					pr_err("system write locked\n")
					return EACCES;
				}
			}
			if (!opts.system_mode) {
				if (starts_with(opts.operations[i].key, NVRAM_SYSTEM_PREFIX)) {
					pr_err("forbidden prefix \"%s\" in user attribute\n", NVRAM_SYSTEM_PREFIX);
					return EINVAL;
				}
			}
			write_ops++;
			break;
		case OP_DEL:
			if (opts.system_mode && !system_unlocked()) {
				pr_err("system write locked\n")
				return EACCES;
			}
			write_ops++;
			break;
		case OP_LIST:
		case OP_GET:
			read_ops++;
			break;
		}
	}
	if (read_ops > 0 && write_ops > 0) {
		pr_err("can't mix read and write operations\n");
		return EINVAL;
	}
	if (read_ops > 1) {
		pr_err("maximum single read operation supported\n");
		return EINVAL;
	}

	struct nvram *nvram_system = NULL;
	struct libnvram_list *list_system = NULL;
	struct nvram *nvram_user = NULL;
	struct libnvram_list *list_user = NULL;

	r = acquire_lockfile(NVRAM_LOCKFILE, &FDLOCK);
	if (r) {
		goto exit;
	}

	const char *nvram_system_a = get_env_str(NVRAM_ENV_SYSTEM_A, xstr(NVRAM_SYSTEM_A));
	const char *nvram_system_b = get_env_str(NVRAM_ENV_SYSTEM_B, xstr(NVRAM_SYSTEM_B));
	pr_dbg("NVRAM_SYSTEM_A: %s\n", nvram_system_a);
	pr_dbg("NVRAM_SYSTEM_B: %s\n", nvram_system_b);

	r = nvram_init(&nvram_system, &list_system, nvram_system_a, nvram_system_b);
	if (r) {
		goto exit;
	}
	if (!opts.system_mode) {
		const char *nvram_user_a = get_env_str(NVRAM_ENV_USER_A, xstr(NVRAM_USER_A));
		const char *nvram_user_b = get_env_str(NVRAM_ENV_USER_B, xstr(NVRAM_USER_B));
		pr_dbg("NVRAM_USER_A: %s\n", nvram_user_a);
		pr_dbg("NVRAM_USER_B: %s\n", nvram_user_b);
		r = nvram_init(&nvram_user, &list_user, nvram_user_a, nvram_user_b);
		if (r) {
			goto exit;
		}
	}

	int write_performed = 0;
	if (opts.operations[0].op == OP_LIST) {
		print_list("system", list_system);
		if (!opts.system_mode) {
			print_list("user", list_user);
		}
	}
	else
	if (opts.operations[0].op == OP_GET) {
		r = print_list_entry("system", list_system, opts.operations[0].key);
		if (r && !opts.system_mode) {
			r = print_list_entry("user", list_user, opts.operations[0].key);
		}
		if (r) {
			pr_dbg("key not found: %s\n", opts.operations[0].key);
			r = -ENOENT;
			goto exit;
		}
	}
	else {
		for (int i = 0; i < opts.op_count; ++i) {
			if (opts.operations[i].op == OP_SET) {
				pr_dbg("HEre: %d: op_count: %d\n", i, opts.op_count);
				r = add_list_entry(opts.system_mode ? "system" : "user", opts.system_mode ? &list_system : &list_user, opts.operations[i].key, opts.operations[i].value);
				if (r < 0)
					goto exit;
				if (r == 1) {
					pr_dbg("written\n");
					write_performed = 1;
				}
			}
			if (opts.operations[i].op == OP_DEL) {
				pr_dbg("deleting %s: %s\n", opts.system_mode ? "system" : "user", opts.operations[i].key);
				if(libnvram_list_remove(opts.system_mode ? &list_system : &list_user, (uint8_t*) opts.operations[i].key, strlen(opts.operations[i].key) + 1))
					write_performed = 1;
			}
		}
	}

	if (write_performed) {
		pr_dbg("Commit changes\n");
		r = nvram_commit(opts.system_mode ? nvram_system : nvram_user, opts.system_mode ? list_system : list_user);
		if (r) {
			goto exit;
		}
	}

	r = 0;

exit:
	release_lockfile(NVRAM_LOCKFILE, FDLOCK);
	if (list_system) {
		destroy_libnvram_list(&list_system);
	}
	if (list_user) {
		destroy_libnvram_list(&list_user);
	}
	nvram_close(&nvram_system);
	nvram_close(&nvram_user);
	return -r;
}
