#ifndef cb_function_h
#define cb_function_h

enum cb_function_type {
	CB_FUNCTION_NATIVE,
	CB_FUNCTION_USER,
};

/* FIXME: signature? */
typedef int (cb_native_function)(void);

typedef struct cb_user_function {

} cb_user_function;

typedef struct cb_function {
	enum cb_function_type f_type;
	union {
		cb_native_function *as_native;
		cb_user_function *as_user;
	} f_value;
} cb_function;

#endif
