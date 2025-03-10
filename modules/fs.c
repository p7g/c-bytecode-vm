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
#include "bytes.h"
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

static DIR **dir_ptr(struct cb_userdata *data)
{
	return (DIR **) &data->userdata;
}

static void deinit_dir(void *ptr)
{
	struct cb_userdata *dir = ptr;
	if (*dir_ptr(dir))
		closedir(*dir_ptr(dir));
}

static int wrapped_opendir(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str name;
	DIR *dir;
	struct cb_userdata *dirval;

	name = CB_EXPECT_STRING(argv[0]);
	dir = opendir(cb_strptr(&name));

	if (!dir) {
		result->type = CB_VALUE_NULL;
		return 0;
	}

	dirval = cb_userdata_new(sizeof(DIR *), deinit_dir);
	*dir_ptr(dirval) = dir;

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
		struct cb_value err;
		(void) cb_value_from_string(&err, "closedir: Not a directory object");
		cb_error_set(err);
		return 1;
	}

	data = argv[0].val.as_userdata;
	dir = *dir_ptr(data);
	if (dir)
		closedir(dir);
	*dir_ptr(data) = NULL;

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
		struct cb_value err;
		(void) cb_value_from_string(&err, "readdir: Not a directory object");
		cb_error_set(err);
		return 1;
	}

	data = argv[0].val.as_userdata;
	dir = *dir_ptr(data);

	if (!(ent = readdir(dir))) {
		result->type = CB_VALUE_NULL;
	} else {
		result->type = CB_VALUE_STRING;
		result->val.as_string = cb_string_new();
		ssize_t strvalid = cb_str_from_cstr(ent->d_name,
				strlen(ent->d_name), &result->val.as_string->string);
		if (strvalid < 0) {
			struct cb_value err;
			cb_value_from_string(&err, cb_str_errmsg(strvalid));
			cb_error_set(err);
			return 1;
		}
	}

	return 0;
}

/* Files */
static size_t ident_stat, ident_statbuf, ident_fopen, ident_fclose, ident_fread,
    ident_fgetc, ident_fgets, ident_feof, ident_ferror;

struct cb_struct_spec *get_stat_struct_spec(void)
{
	static struct cb_struct_spec *spec = NULL;

	if (spec)
		return spec;

	spec = cb_declare_struct("stat", 10, "mode", "nlink", "uid", "gid",
			"size", "blksize", "blocks", "atime", "mtime", "ctime");
	return spec;
}

struct cb_struct *make_stat_struct(struct stat *in)
{
	return cb_struct_make(get_stat_struct_spec(),
			cb_int(in->st_mode),
			cb_int(in->st_nlink),
			cb_int(in->st_uid),
			cb_int(in->st_gid),
			cb_int(in->st_size),
			cb_int(in->st_blksize),
			cb_int(in->st_blocks),
			cb_int(in->st_atime),
			cb_int(in->st_mtime),
			cb_int(in->st_ctime));
}

static int wrapped_stat(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str filename;
	struct stat s;

	filename = CB_EXPECT_STRING(argv[0]);
	if (stat(cb_strptr(&filename), &s)) {
		result->type = CB_VALUE_NULL;
	} else {
		result->type = CB_VALUE_STRUCT;
		result->val.as_struct = make_stat_struct(&s);
	}
	return 0;
}

static FILE **file_ptr(struct cb_userdata *data)
{
	return (FILE **) &data->userdata;
}

static void deinit_file(void *ptr)
{
	struct cb_userdata *data = ptr;
	if (*file_ptr(data))
		fclose(*file_ptr(data));
}

static int wrapped_fopen(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str fname, mode;
	struct cb_userdata *data;
	FILE *f;

	fname = CB_EXPECT_STRING(argv[0]);
	mode = CB_EXPECT_STRING(argv[1]);

	f = fopen(cb_strptr(&fname), cb_strptr(&mode));
	if (!f) {
		result->type = CB_VALUE_NULL;
	} else {
		result->type = CB_VALUE_USERDATA;
		data = result->val.as_userdata = cb_userdata_new(
				sizeof(FILE *), deinit_file);
		*file_ptr(data) = f;
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
		struct cb_value err;
		(void) cb_value_from_string(&err, "fclose: Not a file object");
		cb_error_set(err);
		return 1;
	}

	data = argv[0].val.as_userdata;
	f = *file_ptr(data);
	if (f)
		fclose(f);
	*file_ptr(data) = NULL;

	return 0;
}
static int wrapped_fread(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_value bufv, n, file;
	struct cb_userdata *data;
	FILE *f;
	struct cb_bytes *buf;
	size_t nread;
	int fits_buf;

	bufv = argv[0];
	n = argv[1];
	file = argv[2];

	CB_EXPECT_TYPE(CB_VALUE_BYTES, bufv);
	CB_EXPECT_TYPE(CB_VALUE_INT, n);
	CB_EXPECT_TYPE(CB_VALUE_USERDATA, file);
	if (n.val.as_int < 0) {
		struct cb_value err;
		(void) cb_value_from_string(&err, "fread: Invalid size");
		cb_error_set(err);
		return 1;
	}
	if (!IS_FILE(file)) {
		struct cb_value err;
		(void) cb_value_from_string(&err, "fread: Not a file object");
		cb_error_set(err);
		return 1;
	}

	data = file.val.as_userdata;
	f = *file_ptr(data);
	if (!f) {
		struct cb_value err;
		(void) cb_value_from_string(&err,
				"fread: Cannot read from closed file");
		cb_error_set(err);
		return 1;
	}

	buf = bufv.val.as_bytes;
	fits_buf = cb_bytes_len(buf) >= (size_t) n.val.as_int;

	if (!fits_buf) {
		struct cb_value err;
		(void) cb_value_from_string(&err,
				"fread: Buffer is too small to hold content");
		cb_error_set(err);
		return 1;
	}
	nread = fread(buf->data, sizeof(char), n.val.as_int, f);

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
	struct cb_bytes *buf;
	size_t nread;
	int fits_buf;
	struct cb_value err;

	bufv = argv[0];
	n = argv[1];
	file = argv[2];

	CB_EXPECT_TYPE(CB_VALUE_BYTES, bufv);
	CB_EXPECT_TYPE(CB_VALUE_INT, n);
	CB_EXPECT_TYPE(CB_VALUE_USERDATA, file);
	if (n.val.as_int < 0) {
		(void) cb_value_from_string(&err, "fgets: Invalid size");
		cb_error_set(err);
		return 1;
	}
	if (!IS_FILE(file)) {
		(void) cb_value_from_string(&err, "fgets: Not a file object");
		cb_error_set(err);
		return 1;
	}

	data = file.val.as_userdata;
	f = *file_ptr(data);
	if (!f) {
		(void) cb_value_from_string(&err, "fgets: Cannot read from closed file");
		cb_error_set(err);
		return 1;
	}

	buf = bufv.val.as_bytes;
	fits_buf = cb_bytes_len(buf) >= (size_t) n.val.as_int;

	if (!fits_buf) {
		(void) cb_value_from_string(&err,
				"fgets: Buffer is too small to hold content");
		cb_error_set(err);
		return 1;
	}
	if (NULL == fgets(cb_bytes_ptr(buf), n.val.as_int, f)) {
		result->type = CB_VALUE_NULL;
	} else {
		nread = strlen(cb_bytes_ptr(buf));

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
		struct cb_value err;
		(void) cb_value_from_string(&err, "fgetc: Not a file object");
		cb_error_set(err);
		return 1;
	}

	data = argv[0].val.as_userdata;
	f = *file_ptr(data);
	if (!f) {
		struct cb_value err;
		(void) cb_value_from_string(&err, "fgetc: Can't read from closed file");
		cb_error_set(err);
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
		struct cb_value err;
		(void) cb_value_from_string(&err, "feof: Not a file object");
		cb_error_set(err);
		return 1;
	}

	data = argv[0].val.as_userdata;
	f = *file_ptr(data);
	if (!f) {
		struct cb_value err;
		(void) cb_value_from_string(&err, "feof: File is closed");
		cb_error_set(err);
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
		struct cb_value err;
		(void) cb_value_from_string(&err, "ferror: Not a file object");
		cb_error_set(err);
		return 1;
	}

	data = argv[0].val.as_userdata;
	f = *file_ptr(data);
	if (!f) {
		struct cb_value err;
		(void) cb_value_from_string(&err, "ferror: File is closed");
		cb_error_set(err);
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
	CB_DEFINE_EXPORT(spec, "statbuf", ident_statbuf);
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

	struct cb_value spec_value;
	spec_value.type = CB_VALUE_STRUCT_SPEC;
	spec_value.val.as_struct_spec = get_stat_struct_spec();
	CB_SET_EXPORT(mod, ident_statbuf, spec_value);

#define SET_CONST(C) CB_SET_EXPORT(mod, ident_ ## C, cb_int(C));
	CONSTS(SET_CONST);
#undef SET_CONST
}