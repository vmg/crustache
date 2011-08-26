#ifndef __CRUSTACHE_H__
#define __CRUSTACHE_H__

#include "buffer.h"
#include "array.h"

typedef enum {
	CRUSTACHE_OK = 0,
	CRUSTACHE_E_MISMATCHED_MUSTACHE = -1,
	CRUSTACHE_E_BAD_MUSTACHE_NAME = -2,
	CRUSTACHE_E_MISMATCHED_SECTION = -3,
	CRUSTACHE_E_TOO_DEEP = -4,
	CRUSTACHE_E_VARTYPE = -5,
	CRUSTACHE_E_INVALID_DELIM = -6,
} crustache_error_t;

typedef enum {
	CRUSTACHE_VAR_FALSE,
	CRUSTACHE_VAR_STR,
	CRUSTACHE_VAR_HASH,
	CRUSTACHE_VAR_LIST,
	CRUSTACHE_VAR_LAMBDA
} crustache_var_t;

typedef struct {
	crustache_var_t type;
	void *data;
	size_t size;
} crustache_var;

typedef struct {
	void (*hash_get)(crustache_var *, void *hash, const char *key, size_t key_size);
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

extern void
crustache_parser_error(
	size_t *line_n,
	size_t *col_n,
	char *line_buffer,
	size_t line_buf_size,
	crustache_template *template);

extern const char *
crustache_strerror(int error);

#endif
