#include <assert.h>
#include <libgen.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#if __APPLE__
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

#include "agent.h"
#include "builtin_modules.h"
#include "cb_util.h"
#include "error.h"
#include "eval.h"
#include "module.h"
#include "str.h"

#define INITIAL_STRING_TABLE_SIZE 4
#define MAX_IMPORT_PATHS 256
#define MAX_IMPORT_PATH_LEN 512

struct cb_agent {
	int inited;

	cb_str *string_table;
	cb_modspec **modules;
	size_t string_table_size,
	       modules_size;

	size_t next_module_id,
	       next_string_id;

	char *cbcvm_path;
	char *import_paths[MAX_IMPORT_PATHS];
};

static struct cb_agent agent;

static char *get_stdlib_dir()
{
	char exe_path[PATH_MAX];
	char *dir;
#if __APPLE__
	char buf[PATH_MAX];
	uint32_t bufsize = PATH_MAX;
	assert(!_NSGetExecutablePath(buf, &bufsize));
	(void) bufsize;
	realpath(buf, exe_path);
#else
	/* Linux */
	ssize_t count = readlink("/proc/self/exe", exe_path, PATH_MAX);
	if (count < 0) {
		perror("readlink");
		abort();
	} else if (count >= PATH_MAX) {
		fputs("Path to cbcvm executable is too long\n", stderr);
		abort();
	}
	exe_path[count] = '\0';
#endif
	dir = dirname(exe_path);
	size_t sz = snprintf(NULL, 0, "%s/lib", dir) + 1;

	char *result = malloc(sizeof(char) * sz);
	snprintf(result, sz, "%s/lib", dir);

	return result;
}

int cb_agent_init(void)
{
	size_t i;
	char *path;

	assert(!agent.inited);
	agent.inited = 1;

	agent.string_table = NULL;
	agent.modules = NULL;
	agent.next_module_id = 0;
	agent.next_string_id = 0;
	agent.string_table_size = 0;
	agent.modules_size = 0;

	agent.import_paths[0] = get_stdlib_dir();
	for (i = 1; i < MAX_IMPORT_PATHS; i += 1)
		agent.import_paths[i] = NULL;

	char *lasts;
	agent.cbcvm_path = getenv("CBCVM_PATH");
	if (agent.cbcvm_path) {
		agent.cbcvm_path = strdup(agent.cbcvm_path);
		for (i = 1, path = strtok_r(agent.cbcvm_path, ":", &lasts); path;
				i += 1, path = strtok_r(NULL, ":", &lasts)) {
			if (i >= MAX_IMPORT_PATHS) {
				fprintf(stderr, "Too many paths in CBCVM_PATH\n");
				goto error;
			}
			agent.import_paths[i] = path;
		}
	}

	cb_initialize_builtin_modules();
	return 0;

error:
	return 1;
}

void cb_agent_deinit(void)
{
	unsigned i;

	assert(agent.inited);
	agent.inited = 0;

	for (i = 0; i < agent.next_string_id; i += 1)
		cb_str_free(agent.string_table[i]);

	if (agent.string_table)
		free(agent.string_table);
	if (agent.modules) {
		for (i = 0; i < agent.next_module_id; i += 1) {
			/* Some entries in the modules array are only reserved,
			 * they don't have a modspec in them yet */
			if (agent.modules[i])
				cb_modspec_free(agent.modules[i]);
		}
		free(agent.modules);
	}

	agent.string_table = NULL;
	agent.modules = NULL;

	free(agent.import_paths[0]);
	agent.import_paths[0] = NULL;
	if (agent.cbcvm_path) {
		free(agent.cbcvm_path);
		agent.cbcvm_path = NULL;
		for (i = 1; i < MAX_IMPORT_PATHS && agent.import_paths[i];
				i += 1) {
			agent.import_paths[i] = NULL;
		}
	}
}

ssize_t cb_agent_get_string_id(const char *str, size_t len)
{
	size_t i;

	for (i = 0; i < agent.next_string_id; i += 1) {
		if (cb_str_eq_cstr(agent.string_table[i], str, len))
			return i;
	}
	return -1;
}

ssize_t cb_agent_intern_string(const char *str, size_t len)
{
	size_t id;

	if (!agent.string_table) {
		agent.string_table = malloc(sizeof(cb_str)
				* INITIAL_STRING_TABLE_SIZE);
		agent.string_table_size = INITIAL_STRING_TABLE_SIZE;
	}

	for (id = 0; id < agent.next_string_id; id += 1) {
		if (cb_str_eq_cstr(agent.string_table[id], str, len))
			return id;
	}

	id = agent.next_string_id++;

	if (id >= agent.string_table_size) {
		/* need to resize */
		agent.string_table_size <<= 1;
		agent.string_table = realloc(agent.string_table,
				agent.string_table_size * sizeof(cb_str));
	}

	ssize_t result = cb_str_from_cstr(str, len, &agent.string_table[id]);

	return result < 0 ? result : (ssize_t) id;
}

/* Strings returned from this function should not be freed */
CB_INLINE cb_str cb_agent_get_string(size_t id)
{
	assert(id < agent.next_string_id);

	return agent.string_table[id];
}

static void maybe_grow_modules()
{
	if (agent.next_module_id >= agent.modules_size) {
		agent.modules_size = agent.modules_size == 0
			? 4
			: agent.modules_size << 1;
		agent.modules = realloc(agent.modules,
				agent.modules_size * sizeof(cb_modspec *));
	}
	if (cb_vm_state.modules)
		cb_vm_grow_modules_array();
}

CB_INLINE void cb_agent_add_modspec(cb_modspec *spec)
{
	assert(spec != NULL);
	agent.modules[cb_modspec_id(spec)] = spec;
}

inline void cb_agent_clear_modspec(cb_modspec *spec)
{
	agent.modules[cb_modspec_id(spec)] = NULL;
}

inline size_t cb_agent_reserve_modspec_id()
{
	size_t id;

	maybe_grow_modules();
	id = agent.next_module_id++;
	agent.modules[id] = NULL;
	return id;
}

CB_INLINE void cb_agent_unreserve_modspec_id(size_t id)
{
	assert(id == agent.next_module_id - 1);
	agent.next_module_id -= 1;
}

CB_INLINE const cb_modspec *cb_agent_get_modspec(size_t id)
{
	assert(id < agent.next_module_id);
	return agent.modules[id];
}

cb_modspec *cb_agent_get_modspec_by_name(size_t name)
{
	unsigned i;

	for (i = 0; i < agent.next_module_id; i += 1) {
		if (!agent.modules[i])
			continue;
		if (cb_modspec_name(agent.modules[i]) == name)
			return agent.modules[i];
	}

	return NULL;
}

CB_INLINE size_t cb_agent_modspec_count(void)
{
	return agent.next_module_id;
}

/* Given a name like "hashmap", find a matching file relative to one of the
   paths in CBCVM_PATH with a .cb extension. If found, it will be opened and
   a FILE handle returned. Otherwise NULL will be returned, and an error
   message will be printed.

   It is the caller's responsibility to close the returned file handle. */
FILE *cb_agent_resolve_import(cb_str import_name, const char *pwd,
		char **path_out)
{
#define min(A, B) ({ typeof((A)) _a = (A), _b = (B); _a < _b ? _a : _b; })
#define CHECK_LEN ({ \
		if (path[MAX_IMPORT_PATH_LEN - 1] != 0) { \
			struct cb_value err; \
			cb_value_from_fmt(&err, \
					"Import path for '%.*s' is too long\n", \
					(int) cb_strlen(import_name), \
					cb_strptr(&import_name)); \
			cb_error_set(err); \
			return NULL; \
		} \
	})

	static char path[MAX_IMPORT_PATH_LEN];
	ssize_t i, j;
	FILE *f;

	for (i = -1; i < MAX_IMPORT_PATHS && (i == -1 || agent.import_paths[i]);
			i += 1) {
		if (i == -1 && !pwd)
			continue;
		path[MAX_IMPORT_PATH_LEN - 1] = 0;
		strncpy(path, i == -1 ? pwd : agent.import_paths[i],
				MAX_IMPORT_PATH_LEN);
		CHECK_LEN;

		j = strlen(path);
		assert(j + 1 < MAX_IMPORT_PATH_LEN);
		path[j++] = '/';
		memcpy(path + j, cb_strptr(&import_name), min(
				MAX_IMPORT_PATH_LEN - j,
				cb_strlen(import_name)));
		CHECK_LEN;
		j += cb_strlen(import_name);
		strncpy(path + j, ".cb", MAX_IMPORT_PATH_LEN - j);
		CHECK_LEN;

		f = fopen(path, "rb");
		if (!f)
			continue;
		if (path_out)
			*path_out = strdup(path);
		return f;
	}

	size_t buf_len = 0;
	for (i = -1; i < MAX_IMPORT_PATHS && (i == -1 || agent.import_paths[i]); i++) {
		if (i == -1 && !pwd)
			continue;
		buf_len += strlen(i == -1 ? pwd : agent.import_paths[i]);
		if (i >= 0)
			buf_len += 2; /* ", " */
	}
	if (!agent.import_paths[0])
		buf_len = 6;

	char *buf = malloc(sizeof(char) * buf_len + 1);
	size_t nwritten = 0;
	for (i = -1; i < MAX_IMPORT_PATHS && (i == -1 || agent.import_paths[i]);
			i += 1) {
		if (i == -1 && !pwd)
			continue;
		if (i > 0 || (pwd && i > -1))
			nwritten += snprintf(buf + nwritten,
					buf_len - nwritten + 1, ", ");
		nwritten += snprintf(buf + nwritten, buf_len - nwritten + 1,
				"%s", i == -1 ? pwd : agent.import_paths[i]);
	}
	if (!agent.import_paths[0])
		snprintf(buf, 7, "(none)");
	buf[buf_len] = 0;

	struct cb_value err;
	cb_value_from_fmt(&err, "Import '%.*s' not found, checked in: %s",
			(int) cb_strlen(import_name), cb_strptr(&import_name),
			buf);
	cb_error_set(err);
	free(buf);
	return NULL;

#undef CHECK_LEN
#undef min
}