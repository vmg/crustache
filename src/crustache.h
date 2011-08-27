#ifndef __CRUSTACHE_H__
#define __CRUSTACHE_H__

#include "buffer.h"
#include "array.h"

typedef enum {
	CRUSTACHE_OK = 0,

	CR_EPARSE_MISMATCHED_MUSTACHE = -1,
	CR_EPARSE_BAD_MUSTACHE_NAME = -2,
	CR_EPARSE_MISMATCHED_SECTION = -3,
	CR_EPARSE_BAD_DELIM = -4,
	CR_EPARSE_NOT_IMPLEMENTED = -5,

	CR_ERENDER_TOO_DEEP = -6,
	CR_ERENDER_WRONG_VARTYPE = -7,
	CR_ERENDER_INVALID_CONTEXT = -8,
	CR_ERENDER_NOT_FOUND = -9,
} crustache_error_t;

typedef enum {
	CRUSTACHE_VAR_FALSE,
	CRUSTACHE_VAR_STR,
	CRUSTACHE_VAR_LIST,
	CRUSTACHE_VAR_LAMBDA,
	CRUSTACHE_VAR_CONTEXT,
} crustache_var_t;

typedef struct {
	crustache_var_t type;
	void *data;
	size_t size;
} crustache_var;

typedef struct {
	int (*context_find)(crustache_var *, void *context, const char *key, size_t key_size);
	void (*list_get)(crustache_var *, void *list, size_t i);
	void (*lambda)(crustache_var *, void *lambda, const char *raw_template, size_t raw_size);
	void (*var_free)(crustache_var_t type, void *var);
} crustache_api;

typedef struct crustache_template crustache_template;

extern void
crustache_free_template(crustache_template *template);

extern int
crustache_new_template(crustache_template **output, crustache_api *api, const char *raw_template, size_t raw_length);

extern int
crustache_render(struct buf *ob, crustache_template *template, crustache_var *context);

const char *
crustache_error_syntaxline(
	size_t *line_n,
	size_t *col_n,
	size_t *line_len,
	crustache_template *template);

extern void
crustache_error_rendernode(char *buffer, size_t size, crustache_template *template);

extern const char *
crustache_strerror(int error);

#endif
