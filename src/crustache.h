#ifndef __CRUSTACHE_H__
#define __CRUSTACHE_H__

#include "buffer.h"
#include "array.h"

typedef enum {
	CRUSTACHE_VAR_FALSE,
	CRUSTACHE_VAR_STR,
	CRUSTACHE_VAR_HASH,
	CRUSTACHE_VAR_LIST,
	CRUSTACHE_VAR_LAMBDA
} crustache_var_t;

struct crustache_var {
	crustache_var_t type;
	void *data;
	size_t size;
};

struct crustache_api {
	void (*hash_get)(struct crustache_var *, void *hash, const char *key, size_t key_size);
	void (*list_get)(struct crustache_var *, void *list, size_t i);
	void (*lambda)(struct crustache_var *, void *lambda, const char *raw_template, size_t raw_size);
	void (*var_free)(crustache_var_t type, void *var);
};

struct crustache_template;

extern void
crustache_free_template(struct crustache_template *template);

extern int
crustache_new_template(struct crustache_template **output, const char *raw_template, size_t raw_length);

extern int
crustache_render(struct buf *ob, struct crustache_template *template, struct crustache_var *context);

#endif
