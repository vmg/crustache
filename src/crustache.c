#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "buffer.h"

#define MAX_TEMPLATE_RECURSION 8

typedef enum {
	CRUSTACHE_VAR_FALSE,
	CRUSTACHE_VAR_STR,
	CRUSTACHE_VAR_HASH,
	CRUSTACHE_VAR_LIST,
	CRUSTACHE_VAR_LAMBDA
} crustache_var_t;

typedef enum {
	CRUSTACHE_NODE_MULTIROOT,
	CRUSTACHE_NODE_STATIC,
	CRUSTACHE_NODE_TAG,
	CRUSTACHE_NODE_SECTION,
	CRUSTACHE_NODE_FETCH
} crustache_node_t;

struct crustache_str {
	const char *ptr;
	size_t size;
};

struct node {
	crustache_node_t type;
	struct node *next;
};

struct node_static {
	struct node base;
	struct crustache_str str;
};

struct node_fetch {
	struct node base;
	struct crustache_str var;
};

struct node_tag {
	struct node base;
	struct node *tag_value;
	int flags;
};

struct node_section {
	struct node base;
	struct node *section_key;
	struct node *content;
	struct crustache_str raw_content;
	int inverted;
};

struct crustache_var {
	crustache_var_t type;
	void *data;
	size_t size;
};

struct crustache_api {
	void (*hash_get)(struct crustache_var *, void *hash, const struct crustache_str *key);
	void (*list_get)(struct crustache_var *, void *list, size_t i);
	void (*lambda)(struct crustache_var *, void *lambda, const struct crustache_str *text);
	void (*var_free)(crustache_var_t type, void *var);
};

struct crustache_template {
	struct {
		const char *chars;
		size_t size;
	} mustache_open, mustache_close;

	struct crustache_api api;

	struct buf output;
	struct crustache_str template;
	int depth, busy;
};

static inline void
html_putchar(struct buf *ob, char c)
{
	switch (c) {
		case '<': BUFPUTSL(ob, "&lt;"); break;
		case '>': BUFPUTSL(ob, "&gt;"); break;
		case '&': BUFPUTSL(ob, "&amp;"); break;
		case '"': BUFPUTSL(ob, "&quot;"); break;
		default: bufputc(ob, c); break;
	}
}

static void
html_escape(struct buf *ob, const char *src, size_t size)
{
	size_t  i = 0, org;

	while (i < size) {
		org = i;
		while (i < size && src[i] != '<' && src[i] != '>' && src[i] != '&' && src[i] != '"')
			i++;

		if (i > org)
			bufput(ob, src + org, i - org);

		/* escaping */
		if (i >= size) break;

		html_putchar(ob, src[i]);
		i++;
	}
}

const char *railgun(
	const char *target, size_t target_len,
	const char *pattern, size_t pattern_len)
{
	const char *target_max = target + target_len;
	register unsigned long  hash_pattern;
	unsigned long hash_target;
	unsigned long count;
	unsigned long count_static;

	if (pattern_len > target_len)
		return NULL;

	target = target + pattern_len;
	hash_pattern = ((*(char *)(pattern)) << 8) + *(pattern + (pattern_len - 1));
	count_static = pattern_len - 2;

	for (;;) {
		if (hash_pattern == ((*(char *)(target - pattern_len)) << 8) + *(target - 1)) {
			count = count_static;
			while (count && *(char *)(pattern + 1 + (count_static - count)) ==
				*(char *)(target - pattern_len + 1 + (count_static - count))) {
				count--;
			}

			if (count == 0)
				return (target - pattern_len);
		}

		target++;
		if (target > target_max)
			return NULL;
	}
	/* never reached */
}

static int
find_mustache(size_t *mst_pos, size_t *mst_size, struct crustache_template *template, size_t i, const char *buffer, size_t size)
{
	const char *mst_start;
	const char *mst_end;
	
	mst_start = railgun(
		buffer + i,
		size - i,
		template->mustache_open.chars,
		template->mustache_open.size);

	if (mst_start == NULL) {
		*mst_pos = size;
		*mst_size = 0;
		return 0;
	}

	mst_end = railgun(
		buffer + i,
		size - i,
		template->mustache_close.chars,
		template->mustache_close.size);

	if (mst_end == NULL || mst_end < mst_start)
		return -1;

	*mst_pos = mst_start - buffer;
	*mst_size = mst_end + template->mustache_close.size - mst_start;

	return 0;
}

struct mustache {
	char modifier;
	const char *name;
	size_t size;
};

static int
parse_mustache(struct mustache *mst, const char *buffer, size_t size)
{
	size_t i;

	mst->modifier = 0;

	if (size) {
		switch (buffer[0]) {
			case '#': /* section */
			case '/': /* closing section */
			case '^': /* inverted section */
			case '!': /* comment */
			case '=': /* set delimiter */
			case '>': /* partials (not supported) */
			case '&': /* unescape HTML */
			case '{': /* raw html */
				mst->modifier = buffer[0];
				buffer++;
				size--;
		}

		if (mst->modifier == '{' || mst->modifier == '=') {
			if (!size || buffer[size - 1] != mst->modifier)
				return -1;

			size--;
		}
	}

	while (size && isspace(buffer[0])) {
		buffer++; size--;
	}

	while (size && isspace(buffer[size - 1]))
		size--;

	if (size == 0)
		return -1;

#if 0
	for (i = 0; i < size; ++i) {
		if (!isalnum(buffer[i]) && buffer[i] != '_')
			return -1;
	}
#endif

	mst->name = buffer;
	mst->size = size;
	return 0;
}

static int
node_add_static(struct node *node, const char *buffer, size_t size)
{
	printf("S: |%.*s|\n", (int)size, buffer);
}

static int parse_set_delim(struct crustache_template *template, struct mustache *mst)
{
	const char *buffer = mst->name;
	size_t size = mst->size;
	
	size_t open_size = 0;

	while (open_size < size && !isspace(buffer[open_size])) {
		if (buffer[open_size] == '=')
			return -1;

		open_size++;
	}

	if (open_size == 0 || open_size == size || open_size >= 4)
		return -1;

	template->mustache_open.chars = buffer;
	template->mustache_open.size = open_size;
	return 0;
}

static int
parse_internal(struct crustache_template *template, const char *buffer, size_t size)
{
	size_t org, m, i = 0;
	int result;

	if (template->depth++ >= MAX_TEMPLATE_RECURSION)
		return -1;

	while (i < size) {
		size_t mst_pos, mst_size;
		struct mustache mst;

		if (find_mustache(&mst_pos, &mst_size, template, i, buffer, size) < 0)
			return -1;

		if (mst_pos > i)
			node_add_static(NULL, buffer + i, mst_pos - i);

		if (mst_pos == size)
			break;

		if (parse_mustache(&mst,
			buffer + mst_pos + template->mustache_open.size,
			mst_size - template->mustache_open.size - template->mustache_close.size) < 0)
			return -1;

		i = mst_pos + mst_size;

		switch (mst.modifier) {
		case '#': /* section */
		case '^': /* inverted section */
		{
			size_t original_i = i;
			while (i < size) {
				struct mustache closing_mst;

				if (find_mustache(&mst_pos, &mst_size, template, i, buffer, size) < 0)
					return -1;

				if (parse_mustache(&closing_mst,
					buffer + mst_pos + template->mustache_open.size,
					mst_size - template->mustache_open.size - template->mustache_close.size) < 0)
					return -1;

				i = mst_pos + mst_size;

				if (mst.size == closing_mst.size &&
					memcmp(mst.name, closing_mst.name, mst.size) == 0 &&
					mst.modifier == '/')
					break;
			}

			if (i == size)
				return -1;

			/* TODO: create node, parse into that */
			result = parse_internal(template, buffer + original_i, mst_pos - original_i);
			printf("SECTION: |%.*s|[%.*s]\n", (int)mst.size, mst.name, (int)(mst_pos - original_i), buffer + original_i);
			break;
		}

		case '!': /* comment */
			break;

		case '=': /* set delimiter */
			result = parse_set_delim(template, &mst);
			break;

		case '>': /* partials (not supported) */
			return -2;

		case '{': /* raw html */
		case '&': /* unescape HTML */
		default:
			printf("TAG: |%.*s|\n", (int)mst.size, mst.name);
			// result = parse_tag(); /* TODO */
			break;
		}

		if (result < 0)
			return result;

	}

	return 0;
}

struct crustache_template *
crustache_new_template(const struct crustache_str *template)
{
	struct crustache_template *crt;

	crt = malloc(sizeof(struct crustache_template));
	if (!crt)
		return NULL;

	memset(crt, 0x0, sizeof(struct crustache_template));

	crt->template.ptr = template->ptr;
	crt->template.size = template->size;

	crt->output.unit = 128;
	bufgrow(&crt->output, template->size);

	return crt;
}

void
crustache_free_template(struct crustache_template *template)
{
	bufreset(&template->output);
	free(template);
}

static void free_var(struct crustache_template *template, struct crustache_var *var)
{
	if (template->api.var_free != NULL)
		template->api.var_free(var->type, var->data);
}

static int
crustachify_node(
	struct buf *ob,
	struct crustache_template *template,
	struct node *node,
	struct crustache_var *context);

static int
crustachify_node_static(struct buf *ob, struct node_static *node)
{
	bufput(ob, node->str.ptr, node->str.size);
	return 0;
}

static int
crustachify_node_fetch(
	struct crustache_var *out,
	struct crustache_template *template,
	struct node_fetch *node,
	struct crustache_var *context)
{
	if (node->base.type != CRUSTACHE_NODE_FETCH || context->type != CRUSTACHE_VAR_HASH)
		return -1;

	out->type = CRUSTACHE_VAR_FALSE;
	out->data = NULL;
	template->api.hash_get(out, context->data, &node->var);

	return 0;
}

static int
crustachify_node_tag(
	struct buf *ob,
	struct crustache_template *template,
	struct node_tag *node,
	struct crustache_var *context)
{
	struct crustache_var tag_value;

	if (crustachify_node_fetch(&tag_value, template, (struct node_fetch *)node->tag_value, context) < 0)
		return -1;

	if (tag_value.type != CRUSTACHE_VAR_STR) {
		free_var(template, &tag_value);
		return -1;
	}

	bufput(ob, tag_value.data, tag_value.size);
	free_var(template, &tag_value);
	return 0;
}

static int
crustachify_node_section(
	struct buf *ob,
	struct crustache_template *template,
	struct node_section *node,
	struct crustache_var *context)
{
	struct crustache_var section_key;
	int result = 0;

	if (crustachify_node_fetch(&section_key, template, (struct node_fetch *)node->section_key, context) < 0)
		return -1;

	if (node->inverted) {
		if (section_key.type == CRUSTACHE_VAR_FALSE ||
			(section_key.type == CRUSTACHE_VAR_LIST && section_key.size == 0))
			result = crustachify_node(ob, template, node->content, context); /* TODO: context? */

	} else {
		switch (section_key.type) {
		case CRUSTACHE_VAR_FALSE:
			break;

		case CRUSTACHE_VAR_HASH:
			result = crustachify_node(ob, template, node->content, &section_key);
			break;

		case CRUSTACHE_VAR_LIST:
		{
			size_t i;

			for (i = 0; result == 0 && i < section_key.size; ++i) {
				struct crustache_var subcontext = {0, 0, 0};
				template->api.list_get(&subcontext, section_key.data, i);
				result = crustachify_node(ob, template, node->content, &subcontext);
				free_var(template, &subcontext);
			}
			break;
		}

		case CRUSTACHE_VAR_LAMBDA:
		{
			struct crustache_var lambda_result = {0, 0, 0};
			template->api.lambda(&lambda_result, section_key.data, &node->raw_content);

			if (lambda_result.type == CRUSTACHE_VAR_STR) {
				bufput(ob, lambda_result.data, lambda_result.size);
			} else {
				result = -1;
			}

			free_var(template, &lambda_result);
			break;
		}

		default:
			result = -1;
			break;
		}
	}

	free_var(template, &section_key);
	return result;
}

static int
crustachify_node(
	struct buf *ob,
	struct crustache_template *template,
	struct node *node,
	struct crustache_var *context)
{
	int result = 0;

	if (context->type != CRUSTACHE_VAR_HASH)
		return -1;

	while (result == 0 && node != NULL) {
		switch (node->type) {
		case CRUSTACHE_NODE_STATIC:
			result = crustachify_node_static(ob, (struct node_static *)node);
			break;

		case CRUSTACHE_NODE_TAG:
			result = crustachify_node_tag(ob, template, (struct node_tag *)node, context);
			break;

		case CRUSTACHE_NODE_SECTION:
			result = crustachify_node_section(ob, template, (struct node_section *)node, context);
			break;

		default:
			return -1;
		}

		node = node->next;
	}
}

static int 
parse(struct crustache_template *template)
{
	static const char MUSTACHE_OPEN[] = "{{";
	static const char MUSTACHE_CLOSE[] = "}}";

	int result;

	template->mustache_open.chars = MUSTACHE_OPEN;
	template->mustache_open.size = 2;

	template->mustache_close.chars = MUSTACHE_CLOSE;
	template->mustache_close.size = 2;

	template->depth = 0;
	result = parse_internal(template, template->template.ptr, template->template.size);

	return result;
}

const char *
crustache_output(struct crustache_template *template)
{
	bufnullterm(&template->output);
	return template->output.data;
}

size_t
crustache_output_size(struct crustache_template *template)
{
	return template->output.size;
}

#define TEST
#ifdef TEST
int main()
{
	const char TEST_TEMPLATE[] = "This is {{just}} a {{&simple}} {{#section}} a {{! this is just a test and all that }} {{subtag}} {{/ section}} end";

	struct crustache_str template_string;
	struct crustache_template *tmpl;

	template_string.ptr = TEST_TEMPLATE;
	template_string.size = STRLEN(TEST_TEMPLATE);

	tmpl = crustache_new_template(&template_string);

	parse(tmpl);

	crustache_free_template(tmpl);
	return 0;
}
#endif
