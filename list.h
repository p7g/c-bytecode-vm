#ifndef cb_list_h
#define cb_list_h

#include <assert.h>
#include <stdlib.h>

#define LIST_T(T) list__ ## T
#define DECLARE_LIST_T(T) typedef struct LIST_T(T) { \
		T *elements; \
		size_t len, size; \
	} LIST_T(T)
#define LIST_INIT(L) do { \
		(L)->len = (L)->size = 0; \
		(L)->elements = NULL; \
	} while (0)
#define LIST_PUSH(L, V) do { \
		if ((L)->len >= (L)->size) { \
			(L)->size = (L)->size == 0 ? 4 : (L)->size << 1; \
			(L)->elements = realloc((L)->elements, \
					(L)->size * sizeof((L)->elements[0])); \
		} \
		(L)->elements[(L)->len++] = (V); \
	} while (0)
#define LIST_GET(L, I) (assert((I) < LIST_LEN(L)), (L)->elements[(I)])
#define LIST_LEN(L) ((L)->len)

#endif
