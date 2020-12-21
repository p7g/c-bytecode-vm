#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "agent.h"
#include "builtin_modules.h"
#include "error.h"
#include "intrinsics.h"
#include "module.h"
#include "str.h"
#include "structs.h"
#include "value.h"

#define IS_DIR(V) ((V).val.as_userdata->gc_header.deinit == deinit_dir)
#define IS_FILE(V) ((V).val.as_userdata->gc_header.deinit == deinit_file)

/* Directories */
static size_t ident_opendir, ident_closedir, ident_readdir;

static void deinit_dir(void *ptr)
{
	struct cb_userdata *dir = ptr;
	if (*cb_userdata_ptr(dir))
		closedir((DIR *) *cb_userdata_ptr(dir));
}

static int wrapped_opendir(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str name;
	DIR *dir;
	struct cb_userdata *dirval;

	name = CB_EXPECT_STRING(argv[0]);
	dir = opendir(cb_strptr(name));

	if (!dir) {
		result->type = CB_VALUE_NULL;
		return 0;
	}

	dirval = cb_userdata_new(sizeof(DIR *), deinit_dir);
	*cb_userdata_ptr(dirval) = dir;

	result->type = CB_VALUE_USERDATA;
	result->val.as_userdata = dirval;

	return 0;
}

static int wrapped_closedir(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_userdata *data;
	DIR *dir;

	CB_EXPECT_TYPE(CB_VALUE_USERDATA, argv[0]);
	if (!IS_DIR(argv[0])) {
		cb_error_set(cb_value_from_string(
					"closedir: Not a directory object"));
		return 1;
	}

	data = argv[0].val.as_userdata;
	dir = *cb_userdata_ptr(data);
	if (dir)
		closedir(dir);
	*cb_userdata_ptr(data) = NULL;

	return 0;
}

static int wrapped_readdir(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_userdata *data;
	DIR *dir;
	struct dirent *ent;

	CB_EXPECT_TYPE(CB_VALUE_USERDATA, argv[0]);
	if (!IS_DIR(argv[0])) {
		cb_error_set(cb_value_from_string(
					"readdir: Not a directory object"));
		return 1;
	}

	data = argv[0].val.as_userdata;
	dir = *cb_userdata_ptr(data);

	if (!(ent = readdir(dir))) {
		result->type = CB_VALUE_NULL;
	} else {
		result->type = CB_VALUE_STRING;
		result->val.as_string = cb_string_new();
		result->val.as_string->string = cb_str_from_cstr(ent->d_name,
				strlen(ent->d_name));
	}

	return 0;
}

/* Files */
static size_t ident_stat, ident_fopen, ident_fclose, ident_fread, ident_fgetc,
	ident_fgets, ident_feof, ident_ferror;

struct cb_struct_spec *get_stat_struct_spec(void)
{
	static struct cb_struct_spec *spec = NULL;
	size_t i = 0;

	if (spec)
		return spec;

#define F(NAME) cb_struct_spec_set_field_name(spec, i++, \
		cb_agent_intern_string((NAME), sizeof((NAME)) - 1));

	spec = cb_struct_spec_new(cb_agent_intern_string("stat", 4), 10);
	F("mode");
	F("nlink");
	F("uid");
	F("gid");
	F("size");
	F("blksize");
	F("blocks");
	F("atime");
	F("mtime");
	F("ctime");

	cb_gc_adjust_refcount(&spec->gc_header, 1); /* Never collect it */
	return spec;
#undef F
}

void populate_stat_struct(struct cb_struct *out, struct stat *in)
{
#define SET_FIELD(K, V) cb_struct_set_field(out, cb_agent_intern_string((K), \
		sizeof((K)) - 1), V)
	SET_FIELD("mode", cb_int(in->st_mode));
	SET_FIELD("nlink", cb_int(in->st_nlink));
	SET_FIELD("uid", cb_int(in->st_uid));
	SET_FIELD("gid", cb_int(in->st_gid));
	SET_FIELD("size", cb_int(in->st_size));
	SET_FIELD("blksize", cb_int(in->st_blksize));
	SET_FIELD("blocks", cb_int(in->st_blocks));
	SET_FIELD("atime", cb_int(in->st_atime));
	SET_FIELD("mtime", cb_int(in->st_mtime));
	SET_FIELD("ctime", cb_int(in->st_ctime));
#undef SET_FIELD
}

static int wrapped_stat(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str filename;
	struct stat s;

	filename = CB_EXPECT_STRING(argv[0]);
	if (stat(cb_strptr(filename), &s)) {
		result->type = CB_VALUE_NULL;
	} else {
		result->type = CB_VALUE_STRUCT;
		result->val.as_struct = cb_struct_spec_instantiate(
				get_stat_struct_spec());
		populate_stat_struct(result->val.as_struct, &s);
	}
	return 0;
}

static void deinit_file(void *ptr)
{
	struct cb_userdata *data = ptr;
	if (*cb_userdata_ptr(data))
		fclose((FILE *) *cb_userdata_ptr(data));
}

static int wrapped_fopen(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str fname, mode;
	struct cb_userdata *data;
	FILE *f;

	fname = CB_EXPECT_STRING(argv[0]);
	mode = CB_EXPECT_STRING(argv[1]);

	f = fopen(cb_strptr(fname), cb_strptr(mode));
	if (!f) {
		result->type = CB_VALUE_NULL;
	} else {
		result->type = CB_VALUE_USERDATA;
		data = result->val.as_userdata = cb_userdata_new(
				sizeof(FILE *), deinit_file);
		*cb_userdata_ptr(data) = f;
	}
	return 0;
}

static int wrapped_fclose(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_userdata *data;
	FILE *f;

	CB_EXPECT_TYPE(CB_VALUE_USERDATA, argv[0]);
	if (!IS_FILE(argv[0])) {
		cb_error_set(cb_value_from_string("fclose: Not a file object"));
		return 1;
	}

	data = argv[0].val.as_userdata;
	f = *cb_userdata_ptr(data);
	if (f)
		fclose(f);
	*cb_userdata_ptr(data) = NULL;

	return 0;
}
static int wrapped_fread(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_value bufv, n, file;
	struct cb_userdata *data;
	FILE *f;
	char *buf;
	size_t nread;
	int fits_buf;

	bufv = argv[0];
	n = argv[1];
	file = argv[2];

	CB_EXPECT_TYPE(CB_VALUE_STRING, bufv); /* not interned */
	CB_EXPECT_TYPE(CB_VALUE_INT, n);
	CB_EXPECT_TYPE(CB_VALUE_USERDATA, file);
	if (n.val.as_int < 0) {
		cb_error_set(cb_value_from_string("fread: Invalid size"));
		return 1;
	}
	if (!IS_FILE(file)) {
		cb_error_set(cb_value_from_string("fread: Not a file object"));
		return 1;
	}

	data = file.val.as_userdata;
	f = *cb_userdata_ptr(data);
	if (!f) {
		cb_error_set(cb_value_from_string(
				"fread: Cannot read from closed file"));
		return 1;
	}

	fits_buf = bufv.val.as_string->string.len >= n.val.as_int;

	if (fits_buf)
		buf = bufv.val.as_string->string.chars;
	else
		buf = malloc(sizeof(char) * n.val.as_int);
	nread = fread(buf, sizeof(char), n.val.as_int, f);

	/* kinda sketchy */
	if (!fits_buf) {
		cb_str_free(bufv.val.as_string->string);
		bufv.val.as_string->string.chars = buf;
	}
	bufv.val.as_string->string.len = nread;

	result->type = CB_VALUE_INT;
	result->val.as_int = nread;
	return 0;
}

static int wrapped_fgets(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_value bufv, n, file;
	struct cb_userdata *data;
	FILE *f;
	char *buf;
	size_t nread;
	int fits_buf;

	bufv = argv[0];
	n = argv[1];
	file = argv[2];

	CB_EXPECT_TYPE(CB_VALUE_STRING, bufv); /* not interned */
	CB_EXPECT_TYPE(CB_VALUE_INT, n);
	CB_EXPECT_TYPE(CB_VALUE_USERDATA, file);
	if (n.val.as_int < 0) {
		cb_error_set(cb_value_from_string("fgets: Invalid size"));
		return 1;
	}
	if (!IS_FILE(file)) {
		cb_error_set(cb_value_from_string("fgets: Not a file object"));
		return 1;
	}

	data = file.val.as_userdata;
	f = *cb_userdata_ptr(data);
	if (!f) {
		cb_error_set(cb_value_from_string(
				"fgets: Cannot read from closed file"));
		return 1;
	}

	fits_buf = bufv.val.as_string->string.len >= n.val.as_int;

	if (fits_buf)
		buf = bufv.val.as_string->string.chars;
	else
		buf = malloc(sizeof(char) * n.val.as_int);
	if (NULL == fgets(buf, n.val.as_int, f)) {
		if (!fits_buf)
			free(buf);
		result->type = CB_VALUE_NULL;
	} else {
		nread = strlen(buf);
		if (!fits_buf) {
			cb_str_free(bufv.val.as_string->string);
			bufv.val.as_string->string.chars = buf;
		}
		bufv.val.as_string->string.len = nread;

		result->type = CB_VALUE_INT;
		result->val.as_int = nread;
	}
	return 0;
}

static int wrapped_fgetc(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_userdata *data;
	FILE *f;
	int c;

	CB_EXPECT_TYPE(CB_VALUE_USERDATA, argv[0]);
	if (!IS_FILE(argv[0])) {
		cb_error_set(cb_value_from_string("fgetc: Not a file object"));
		return 1;
	}

	data = argv[0].val.as_userdata;
	f = *cb_userdata_ptr(data);
	if (!f) {
		cb_error_set(cb_value_from_string(
				"fgetc: Can't read from closed file"));
		return 1;
	}

	c = fgetc(f);

	if (c == EOF) {
		result->type = CB_VALUE_NULL;
	} else {
		result->type = CB_VALUE_CHAR;
		result->val.as_char = c;
	}
	return 0;
}

static int wrapped_feof(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_userdata *data;
	FILE *f;

	CB_EXPECT_TYPE(CB_VALUE_USERDATA, argv[0]);
	if (!IS_FILE(argv[0])) {
		cb_error_set(cb_value_from_string("feof: Not a file object"));
		return 1;
	}

	data = argv[0].val.as_userdata;
	f = *cb_userdata_ptr(data);
	if (!f) {
		cb_error_set(cb_value_from_string("feof: File is closed"));
		return 1;
	}

	result->type = CB_VALUE_BOOL;
	result->val.as_bool = feof(f);
	return 0;
}

static int wrapped_ferror(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_userdata *data;
	FILE *f;

	CB_EXPECT_TYPE(CB_VALUE_USERDATA, argv[0]);
	if (!IS_FILE(argv[0])) {
		cb_error_set(cb_value_from_string(
					"ferror: Not a file object"));
		return 1;
	}

	data = argv[0].val.as_userdata;
	f = *cb_userdata_ptr(data);
	if (!f) {
		cb_error_set(cb_value_from_string("ferror: File is closed"));
		return 1;
	}

	result->type = CB_VALUE_BOOL;
	result->val.as_bool = ferror(f);
	return 0;
}

#define CONSTS(C) \
	/* File types */ \
	C(S_IFMT) \
	C(S_IFSOCK) \
	C(S_IFLNK) \
	C(S_IFREG) \
	C(S_IFBLK) \
	C(S_IFDIR) \
	C(S_IFCHR) \
	C(S_IFIFO) \
	/* set-{user,group}-id bit, sticky bit */ \
	C(S_ISUID) \
	C(S_ISGID) \
	C(S_ISVTX) \
	/* Owner permissions */ \
	C(S_IRWXU) \
	C(S_IRUSR) \
	C(S_IWUSR) \
	C(S_IXUSR) \
	/* Group permissions */ \
	C(S_IRWXG) \
	C(S_IRGRP) \
	C(S_IWGRP) \
	C(S_IXGRP) \
	/* Others permissions */ \
	C(S_IRWXO) \
	C(S_IROTH) \
	C(S_IWOTH) \
	C(S_IXOTH)

#define IDENT_VAR(C) size_t ident_ ## C;
CONSTS(IDENT_VAR);
#undef IDENT_VAR

void cb_fs_build_spec(cb_modspec *spec)
{
	// TODO: writing
	CB_DEFINE_EXPORT(spec, "opendir", ident_opendir);
	CB_DEFINE_EXPORT(spec, "closedir", ident_closedir);
	CB_DEFINE_EXPORT(spec, "readdir", ident_readdir);
	CB_DEFINE_EXPORT(spec, "stat", ident_stat);
	CB_DEFINE_EXPORT(spec, "fopen", ident_fopen);
	CB_DEFINE_EXPORT(spec, "fclose", ident_fclose);
	CB_DEFINE_EXPORT(spec, "fread", ident_fread);
	CB_DEFINE_EXPORT(spec, "fgetc", ident_fgetc);
	CB_DEFINE_EXPORT(spec, "fgets", ident_fgets);
	CB_DEFINE_EXPORT(spec, "feof", ident_feof);
	CB_DEFINE_EXPORT(spec, "ferror", ident_ferror);

#define DEF_CONST(C) CB_DEFINE_EXPORT(spec, #C, ident_ ## C);
	CONSTS(DEF_CONST);
#undef DEF_CONST
}

void cb_fs_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_opendir,
			cb_cfunc_new(ident_opendir, 1, wrapped_opendir));
	CB_SET_EXPORT(mod, ident_closedir,
			cb_cfunc_new(ident_closedir, 1, wrapped_closedir));
	CB_SET_EXPORT(mod, ident_readdir,
			cb_cfunc_new(ident_readdir, 1, wrapped_readdir));
	CB_SET_EXPORT(mod, ident_stat,
			cb_cfunc_new(ident_stat, 1, wrapped_stat));
	CB_SET_EXPORT(mod, ident_fopen,
			cb_cfunc_new(ident_fopen, 2, wrapped_fopen));
	CB_SET_EXPORT(mod, ident_fclose,
			cb_cfunc_new(ident_fclose, 1, wrapped_fclose));
	CB_SET_EXPORT(mod, ident_fread,
			cb_cfunc_new(ident_fread, 3, wrapped_fread));
	CB_SET_EXPORT(mod, ident_fgetc,
			cb_cfunc_new(ident_fgetc, 1, wrapped_fgetc));
	CB_SET_EXPORT(mod, ident_fgets,
			cb_cfunc_new(ident_fgets, 1, wrapped_fgets));
	CB_SET_EXPORT(mod, ident_feof,
			cb_cfunc_new(ident_feof, 1, wrapped_feof));
	CB_SET_EXPORT(mod, ident_ferror,
			cb_cfunc_new(ident_ferror, 1, wrapped_ferror));

#define SET_CONST(C) CB_SET_EXPORT(mod, ident_ ## C, cb_int(C));
	CONSTS(SET_CONST);
#undef SET_CONST
}
