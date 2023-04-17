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
#include "nvram_format.h"
#include "nvram_interface.h"
#include "libnvram/libnvram.h"

#define xstr(a) str(a)
#define str(a) #a

#define NVRAM_ENV_INTERFACE "NVRAM_INTERFACE"
#define NVRAM_ENV_FORMAT "NVRAM_FORMAT"
#define NVRAM_LOCKFILE "/run/lock/nvram.lock"
#define NVRAM_ENV_DEBUG "NVRAM_DEBUG"
#define NVRAM_ENV_SYSTEM_UNLOCK "NVRAM_SYSTEM_UNLOCK"
#define NVRAM_SYSTEM_UNLOCK_MAGIC "16440"

static const char* get_env_str(const char* env, const char* def)
{
	const char *str = getenv(env);
	if (str)
		return str;
	return def;
}

static long get_env_long(const char* env)
{
	const int base = 10;
	const char *val = getenv(env);
	if (val) {
		char *endptr = NULL;
		return strtol(val, &endptr, base);
	}
	return 0;
}

static int system_unlocked(void)
{
	const char* unlock_str = getenv(NVRAM_ENV_SYSTEM_UNLOCK);
	return unlock_str && strcmp(unlock_str, NVRAM_SYSTEM_UNLOCK_MAGIC) == 0;
}

static int starts_with_sysprefix(const char* str)
{
	const char* prefix = xstr(NVRAM_SYSTEM_PREFIX);
	const size_t prefix_len = strlen(prefix);
	/* prefix enforcement disabled if string empty */
	if (prefix_len == 0)
		return 0;
	const size_t str_len = strlen(str);
	if (str_len > prefix_len) {
		if(!strncmp(str, prefix, prefix_len)) {
			return 1;
		}
	}
	return 0;
}

static int acquire_lockfile(const char *path)
{
    const int allowed_retries = 10;
    const int retry_delay_us = 10000;
    int r = 0;

    int fd = open(path, O_CREAT | O_WRONLY, S_IWUSR | S_IRUSR);
    if (fd < 0) {
        r = errno;
        pr_err("failed opening lockfile: %s [%d]: %s\n", path, r, strerror(r));
        return -r;
    }

    int retries = allowed_retries;
    while (retries--) {
        if (flock(fd, LOCK_EX | LOCK_NB)) {
            r = errno;
            if (r != EWOULDBLOCK) {
                pr_err("failed locking lockfile: %s [%d]: %s\n", path, r,
                       strerror(r));
                close(fd);
                return -r;
            }
            if (retries == 0) {
                pr_err("failed locking lockfile: %s [%d]: %s\n", path, r,
                       strerror(ETIMEDOUT));
                close(fd);
                return -ETIMEDOUT;
            }
            usleep(retry_delay_us);
        } else {
            break;
        }
    }

    pr_dbg("%s: locked\n", path);

    return fd;
}

static int release_lockfile(const char* path, int fdlock)
{
	int r = 0;

	if (fdlock >= 0) {
		if(close(fdlock)) {
			r = errno;
			pr_err("failed closing lockfile: %s [%d]: %s", path, r, strerror(r));
			return -r;
		}
		if (unlink(path)) {
			r = errno;
			pr_err("failed removing lockfile: %s [%d]: %s", path, r, strerror(r));
			return -r;
		}
	}

	pr_dbg("%s: unlocked\n", path);

	return 0;
}

static void print_usage()
{
	const char* interface_name = xstr(NVRAM_INTERFACE_DEFAULT);
	printf("nvram, nvram interface, Data Respons Solutions AB\n");
	printf("Version:   %s\n", xstr(SRC_VERSION));
	printf("\n");
	printf("Defaults:\n");
	printf("sys prefix: %s\n", xstr(NVRAM_SYSTEM_PREFIX));
	printf("interface:  %s\n", interface_name);
	printf("format:     %s\n", xstr(NVRAM_FORMAT_DEFAULT));
	printf("system_a:   %s\n", nvram_get_interface_section(interface_name, SYSTEM_A));
	printf("system_b:   %s\n", nvram_get_interface_section(interface_name, SYSTEM_B));
	printf("user_a:     %s\n", nvram_get_interface_section(interface_name, USER_A));
	printf("user_b:     %s\n", nvram_get_interface_section(interface_name, USER_B));
	printf("\n");

	printf("Usage:   nvram [OPTION] COMMAND [COMMAND]\n");
	printf("Example: nvram --set keyname value\n");
	printf("Defaults to COMMAND list if none set\n");
	printf("\n");

	printf("Options:\n");
	printf("  --sys             ignore user section\n");
	printf("  --user            ignore sys section\n");
	printf("  -i, --interface   select interface\n");
	printf("  -f, --format      select format\n");
	printf("  --user_a          set user_a section\n");
	printf("  --user_b          set user_b section\n");
	printf("  --sys_a           set sys_a section\n");
	printf("  --sys_b           set sys_b section\n");
	printf("\n");

	printf("Commands:\n");
	printf("  --set KEY VALUE  Write attribute with KEY and VALUE\n");
	printf("  --get KEY        Read attribute with KEY\n");
	printf("  --del KEY        Delete attribute with KEY\n");
	printf("  --list           Lists attributes\n");
	printf("\n");

	printf("Return values:\n");
	printf("  0 if ok\n");
	printf("  errno for error\n");
	printf("\n");
}

enum print_options {
	PRINT_KEY = 1 << 0,
	PRINT_VALUE = 1 << 2,
	PRINT_KEY_AND_VALUE = PRINT_KEY | PRINT_VALUE,
};

static void print_arr_u8(uint8_t* data, uint32_t size)
{
	/* Print as string if null-terminated, else as hex */
	const int is_string = data[size - 1] == '\0';
	if (is_string) {
		printf("%s", data);
	}
	else {
		printf("0x");
		for (uint32_t i = 0; i < size; ++i)
			printf("%02" PRIx8 "", data[i]);
	}
}

static int print_entry(const struct libnvram_entry* entry, enum print_options opts)
{
	if (entry->key_len > INT_MAX || entry->value_len > INT_MAX)
		return -EINVAL;
	if ((opts & PRINT_KEY_AND_VALUE) == 0)
		return -EINVAL;

	if ((opts & PRINT_KEY) == PRINT_KEY)
		print_arr_u8(entry->key, entry->key_len);
	if ((opts & PRINT_KEY_AND_VALUE) == PRINT_KEY_AND_VALUE)
		printf("=");
	if ((opts & PRINT_VALUE) == PRINT_VALUE)
		print_arr_u8(entry->value, entry->value_len);
	printf("\n");
	return 0;
}

// return 0 for OK or negative errno for error
static int print_list_entry(const char* list_name, const struct libnvram_list* list, const char* key)
{
	pr_dbg("getting key from %s: %s\n", list_name, key);
	struct libnvram_entry *entry = libnvram_list_get(list, (uint8_t*) key, strlen(key) + 1);
	if (!entry)
		return -ENOENT;
	return print_entry(entry, PRINT_VALUE);
}

static void print_list(const char* list_name, const struct libnvram_list* list)
{
	pr_dbg("listing %s\n", list_name);
	for (struct libnvram_list *cur = (struct libnvram_list*) list; cur; cur = cur->next)
		print_entry(cur->entry, PRINT_KEY_AND_VALUE);
}

// return 0 for equal
static int keycmp(const uint8_t* key1, uint32_t key1_len, const uint8_t* key2, uint32_t key2_len)
{
	if (key1_len == key2_len)
		return memcmp(key1, key2, key1_len);
	return 1;
}

// return 0 if already exists, 1 if added, negate errno for error
static int add_list_entry(const char* list_name, struct libnvram_list** list, const char* key, const char* value)
{
	const size_t key_len = strlen(key) + 1;
	const size_t value_len = strlen(value) + 1;

	pr_dbg("setting: %s: %s=%s\n", list_name, key, value);
	struct libnvram_entry *entry = libnvram_list_get(*list, (uint8_t*) key, key_len);
	if (entry && !keycmp(entry->value, entry->value_len, (uint8_t*) value, value_len))
		return 0;
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

// return 0 if not found, 1 if removed
static int remove_list_entry(const char* list_name, struct libnvram_list** list, const char* key)
{
	const size_t key_len = strlen(key) + 1;

	pr_dbg("deleting %s: %s\n", list_name, key);
	return libnvram_list_remove(list, (uint8_t*) key, key_len) == 1;
}

enum op {
	OP_NONE = 0,
	OP_LIST = 1 << 0,
	OP_SET = 1 << 1,
	OP_GET = 1 << 2,
	OP_DEL = 1 << 3,
};

enum mode {
	MODE_NONE = 0,
	MODE_USER_READ = 1 << 0,
	MODE_USER_WRITE = 1 << 1,
	MODE_SYSTEM_READ = 1 << 2,
	MODE_SYSTEM_WRITE = 1 << 3,
};

struct operation {
	/* commandline arguments */
	enum op op;
	char* key;
	char* value;
	/* filled in when created */
	int (*validate)(const struct operation* operation, enum mode mode);
	int (*execute)(const struct operation* operation, enum mode mode,
			struct libnvram_list** list_system, struct libnvram_list** list_user, int* write_performed);
	struct operation* next;
};

struct opts {
	enum mode mode;
	struct operation* operations;
};

static int validate_set(const struct operation* operation, enum mode mode)
{
	if ((mode & MODE_SYSTEM_WRITE) == MODE_SYSTEM_WRITE) {
		if (!starts_with_sysprefix(operation->key)) {
			pr_err("required prefix \"%s\" missing in system attribute\n", xstr(NVRAM_SYSTEM_PREFIX));
			return -EINVAL;
		}
		if (!system_unlocked()) {
			pr_err("system write locked\n")
			return -EACCES;
		}
	}
	if ((mode & MODE_USER_WRITE) == MODE_USER_WRITE) {
		if (starts_with_sysprefix(operation->key)) {
			pr_err("forbidden prefix \"%s\" in user attribute\n", xstr(NVRAM_SYSTEM_PREFIX));
			return -EINVAL;
		}
	}
	return 0;
}

static int validate_del(const struct operation* operation, enum mode mode)
{
	(void) operation;

	if (((mode & MODE_SYSTEM_WRITE) == MODE_SYSTEM_WRITE) && !system_unlocked()) {
		pr_err("system write locked\n")
		return -EACCES;
	}
	return 0;
}

static int exec_list(const struct operation* operation, enum mode mode,
		struct libnvram_list** list_system, struct libnvram_list** list_user, int* write_performed)
{
	(void) write_performed;
	(void) operation;

	if ((mode & MODE_SYSTEM_READ) == MODE_SYSTEM_READ)
		print_list("system", *list_system);
	if ((mode & MODE_USER_READ) == MODE_USER_READ)
		print_list("user", *list_user);
	return 0;
}

static int exec_set(const struct operation* operation, enum mode mode,
		struct libnvram_list** list_system, struct libnvram_list** list_user, int* write_performed)
{
	int r = -EINVAL;
	if ((mode & MODE_SYSTEM_WRITE) == MODE_SYSTEM_WRITE)
		r = add_list_entry("system", list_system, operation->key, operation->value);
	else if ((mode & MODE_USER_WRITE) == MODE_USER_WRITE)
		r = add_list_entry("user", list_user, operation->key, operation->value);
	if (r < 0)
		return r;
	if (r == 1) {
		pr_dbg("written\n");
		*write_performed = 1;
	}
	return 0;
}

static int exec_get(const struct operation* operation, enum mode mode,
		struct libnvram_list** list_system, struct libnvram_list** list_user, int* write_performed)
{
	(void) write_performed;

	int r = -ENOENT;
	/* Prefer retrieving from system if allowed */
	if ((mode & MODE_SYSTEM_READ) == MODE_SYSTEM_READ)
		r = print_list_entry("system", *list_system, operation->key);
	/* Retrieve from user if not already found and allowed */
	if (r != 0 && (mode & MODE_USER_READ) == MODE_USER_READ)
		r = print_list_entry("user", *list_user, operation->key);
	if (r != 0)
		pr_dbg("key not found: %s\n", operation->key);
	return r;
}

static int exec_del(const struct operation* operation, enum mode mode,
		struct libnvram_list** list_system, struct libnvram_list** list_user, int* write_performed)
{
	int r = -EINVAL;
	if ((mode & MODE_SYSTEM_WRITE) == MODE_SYSTEM_WRITE)
		r = remove_list_entry("system", list_system, operation->key);
	else if ((mode & MODE_USER_WRITE) == MODE_USER_WRITE)
		r = remove_list_entry("user", list_user, operation->key);
	if (r == 1) {
		pr_dbg("deleted\n");
		*write_performed = 1;
	}
	return 0;
}

static int add_operation(struct operation** list, enum op op, char* key, char* value)
{
	struct operation* operation = malloc(sizeof(struct operation));
	if (operation == NULL) {
		pr_err("Failed allocating operation memory\n");
		return -ENOMEM;
	}
	operation->op = op;
	operation->key = key;
	operation->value = value;
	switch (operation->op) {
	case OP_LIST:
		operation->validate = NULL;
		operation->execute = exec_list;
		break;
	case OP_SET:
		operation->validate = validate_set;
		operation->execute = exec_set;
		break;
	case OP_GET:
		operation->validate = NULL;
		operation->execute = exec_get;
		break;
	case OP_DEL:
		operation->validate = validate_del;
		operation->execute = exec_del;
		break;
	case OP_NONE:
		break;
	}
	operation->next = NULL;
	/* First entry */
	if (*list == NULL) {
		*list = operation;
		return 0;
	}
	struct operation* it = *list;
	while (it->next != NULL)
		it = it->next;
	it->next = operation;
	return 0;
}

static void destroy_operations(struct operation** list)
{
	if (list != NULL) {
		struct operation* it = *list;
		struct operation* next = NULL;
		while (it != NULL) {
			next = it->next;
			free(it);
			it = next;

		}
		*list = NULL;
	}
}

static int validate_operations(const struct opts* opts)
{
	enum op found_op_types = OP_NONE;
	int r = 0;
	for (struct operation* it = opts->operations; it != NULL; it = it->next) {
		pr_dbg("operation: %d, key: %s, val: %s\n",
				it->op, it->key, it->value);
		found_op_types |= it->op;
		if (it->validate != NULL) {
			r = it->validate(it, opts->mode);
			if (r != 0)
				return r;
		}
	}

	const int read_ops = OP_GET | OP_LIST;
	const int write_ops = OP_SET | OP_DEL;
	if ((found_op_types & read_ops) != 0 && (found_op_types & write_ops) != 0) {
		pr_err("can't mix read and write operations\n");
		return -EINVAL;
	}
	if ((found_op_types & OP_LIST) == OP_LIST && (found_op_types & OP_GET) == OP_GET) {
		pr_err("can't mix --get and --list operations\n");
		return -EINVAL;
	}
	return 0;
}

static int execute_operations(const struct opts* opts, struct nvram_format* format,
								struct nvram* nvram_system, struct libnvram_list** list_system,
								struct nvram* nvram_user, struct libnvram_list** list_user)
{
	int r = 0;
	int write_performed = 0;

	for (struct operation* it = opts->operations; it != NULL; it = it->next) {
		if (it->execute == NULL) {
			pr_err("operation should not be NULL\n");
			return -EBADF;
		}
		r = it->execute(it, opts->mode, list_system, list_user, &write_performed);
		if (r != 0)
			return r;
	}

	r = 0;
	if (write_performed) {
		pr_dbg("Commit changes\n");
		if ((opts->mode & MODE_SYSTEM_WRITE) == MODE_SYSTEM_WRITE)
			r = format->commit(nvram_system, *list_system);
		else if ((opts->mode & MODE_USER_WRITE) == MODE_USER_WRITE)
			r = format->commit(nvram_user, *list_user);
		if (r)
			pr_err("Failed committing changes [%d]: %s\n", -r, strerror(-r));
	}
	return r;
}

/* Allow greater cognitive complexity due to argument parsing
 *
 * NOLINTNEXTLINE(readability-function-cognitive-complexity) */
int main(int argc, char** argv)
{
	struct nvram *nvram_system = NULL;
	struct libnvram_list *list_system = NULL;
	struct nvram *nvram_user = NULL;
	struct libnvram_list *list_user = NULL;
	struct nvram_interface* interface = NULL;
	struct nvram_format* format = NULL;
	struct opts opts;
	memset(&opts, 0, sizeof(opts));
	opts.mode = MODE_USER_READ | MODE_USER_WRITE | MODE_SYSTEM_READ;
	char* interface_override = NULL;
	char* format_override = NULL;
	char* user_a_override = NULL;
	char* user_b_override = NULL;
	char* system_a_override = NULL;
	char* system_b_override = NULL;
	int fd_lock = -1;
	int r = 0;

	if (get_env_long(NVRAM_ENV_DEBUG))
		enable_debug();

	for (int i = 1; i < argc; i++) {
		if (!strcmp("--set", argv[i]) || !strcmp("set", argv[i])) {
			if (i + 2 >= argc) {
				fprintf(stderr, "Too few arguments for command set\n");
				r = -EINVAL;
				goto exit;
			}
			r = add_operation(&opts.operations, OP_SET, argv[i + 1], argv[i + 2]);
			if (r != 0)
				goto exit;
			i += 2;
		}
		else if (!strcmp("--get", argv[i]) || !strcmp("get", argv[i])) {
			if (++i >= argc) {
				fprintf(stderr, "Too few arguments for command get\n");
				r = -EINVAL;
				goto exit;
			}
			r = add_operation(&opts.operations, OP_GET, argv[i], NULL);
			if (r != 0)
				goto exit;
		}
		else if (!strcmp("--list", argv[i]) || !strcmp("list", argv[i])) {
			r = add_operation(&opts.operations, OP_LIST, NULL, NULL);
			if (r != 0)
				goto exit;
		}
		else if(!strcmp("--del", argv[i]) || !strcmp("delete", argv[i])) {
			if (++i >= argc) {
				fprintf(stderr, "Too few arguments for command delete\n");
				r = -EINVAL;
				goto exit;
			}
			r = add_operation(&opts.operations, OP_DEL, argv[i], NULL);
			if (r != 0)
				goto exit;
		}
		else if (!strcmp("--sys", argv[i])) {
			opts.mode = MODE_SYSTEM_READ | MODE_SYSTEM_WRITE;
		}
		else if (!strcmp("--user", argv[i])) {
			opts.mode = MODE_USER_READ | MODE_USER_WRITE;
		}
		else if (!strcmp("-h", argv[i]) || !strcmp("--help", argv[i])) {
			print_usage();
			r = -EINVAL;
			goto exit;
		}
		else if (!strcmp("-f", argv[i]) || !strcmp("--format", argv[i])) {
			if (++i >= argc) {
				fprintf(stderr, "Too few arguments for -f, --format\n");
				r = -EINVAL;
				goto exit;
			}
			format_override = argv[i];
		}
		else if (!strcmp("-i", argv[i]) || !strcmp("--interface", argv[i])) {
			if (++i >= argc) {
				fprintf(stderr, "Too few arguments for -i, --interface\n");
				r = -EINVAL;
				goto exit;
			}
			interface_override = argv[i];
		}
		else if (!strcmp("--user_a", argv[i])) {
			if (++i >= argc) {
				fprintf(stderr, "Too few arguments for --user_a\n");
				r = -EINVAL;
				goto exit;
			}
			user_a_override = argv[i];
		}
		else if (!strcmp("--user_b", argv[i])) {
			if (++i >= argc) {
				fprintf(stderr, "Too few arguments for --user_b\n");
				r = -EINVAL;
				goto exit;
			}
			user_b_override = argv[i];
		}
		else if (!strcmp("--sys_a", argv[i])) {
			if (++i >= argc) {
				fprintf(stderr, "Too few arguments for --sys_a\n");
				r = -EINVAL;
				goto exit;
			}
			system_a_override = argv[i];
		}
		else if (!strcmp("--sys_b", argv[i])) {
			if (++i >= argc) {
				fprintf(stderr, "Too few arguments for --sys_b\n");
				r = -EINVAL;
				goto exit;
			}
			system_b_override = argv[i];
		}
		else {
			fprintf(stderr, "unknown argument: %s\n", argv[i]);
			r = -EINVAL;
			goto exit;
		}
	}

	if (opts.operations == NULL) {
		r = add_operation(&opts.operations, OP_LIST, NULL, NULL);
		if (r != 0)
			goto exit;
	}

	const char* interface_selected = get_env_str(NVRAM_ENV_INTERFACE, xstr(NVRAM_INTERFACE_DEFAULT));
	const char* interface_name = interface_override != NULL ? interface_override : interface_selected;
	interface = nvram_get_interface(interface_name);
	if (interface == NULL) {
		fprintf(stderr, "Unresolved interface: %s\n", interface_name);
		r = -EINVAL;
		goto exit;
	}
	const char* format_selected = get_env_str(NVRAM_ENV_FORMAT, xstr(NVRAM_FORMAT_DEFAULT));
	const char* format_name = format_override != NULL ? format_override : format_selected;
	format = nvram_get_format(format_name);
	if (format == NULL) {
		fprintf(stderr, "Unresolved format: %s\n", format_name);
		r = -EINVAL;
		goto exit;
	}

	pr_dbg("interface: %s\n", interface_name);
	pr_dbg("format: %s\n", format_name);
	pr_dbg("system_write: %s\n", (opts.mode & MODE_SYSTEM_WRITE) == MODE_SYSTEM_WRITE ? "yes" : "no");
	pr_dbg("system_read: %s\n", (opts.mode & MODE_SYSTEM_READ) == MODE_SYSTEM_READ ? "yes" : "no");
	pr_dbg("user_write: %s\n", (opts.mode & MODE_USER_WRITE) == MODE_USER_WRITE ? "yes" : "no");
	pr_dbg("user_read: %s\n", (opts.mode & MODE_USER_READ) == MODE_USER_READ ? "yes" : "no");

	r = validate_operations(&opts);
	if (r)
		goto exit;

	fd_lock = acquire_lockfile(NVRAM_LOCKFILE);
	if (fd_lock < 0)
		goto exit;

	if ((opts.mode & (MODE_SYSTEM_WRITE | MODE_SYSTEM_READ)) != 0) {
		const char *nvram_system_a = system_a_override != NULL ? system_a_override :
										nvram_get_interface_section(interface_name, SYSTEM_A);
		const char *nvram_system_b = system_b_override != NULL ? system_b_override :
										nvram_get_interface_section(interface_name, SYSTEM_B);
		pr_dbg("NVRAM_SYSTEM_A: %s\n", nvram_system_a);
		pr_dbg("NVRAM_SYSTEM_B: %s\n", nvram_system_b);

		r = format->init(&nvram_system, interface, &list_system, nvram_system_a, nvram_system_b);
		if (r) {
			goto exit;
		}
	}

	if ((opts.mode & (MODE_USER_WRITE | MODE_USER_READ)) != 0) {
		const char *nvram_user_a = user_a_override != NULL ? user_a_override :
									nvram_get_interface_section(interface_name, USER_A);
		const char *nvram_user_b = user_b_override != NULL ? user_b_override :
									nvram_get_interface_section(interface_name, USER_B);
		pr_dbg("NVRAM_USER_A: %s\n", nvram_user_a);
		pr_dbg("NVRAM_USER_B: %s\n", nvram_user_b);
		r = format->init(&nvram_user, interface, &list_user, nvram_user_a, nvram_user_b);
		if (r) {
			goto exit;
		}
	}

	r = execute_operations(&opts, format, nvram_system, &list_system, nvram_user, &list_user);
	if (r)
		goto exit;

	r = 0;

exit:
	release_lockfile(NVRAM_LOCKFILE, fd_lock);
	destroy_operations(&opts.operations);
	if (list_system)
		destroy_libnvram_list(&list_system);
	if (list_user)
		destroy_libnvram_list(&list_user);
	if (format != NULL) {
		format->close(&nvram_system);
		format->close(&nvram_user);
	}
	return -r;
}
