#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "crustache.h"

#define MAX_TEMPLATE_RECURSION 8

typedef enum {
	CRUSTACHE_NODE_MULTIROOT,
	CRUSTACHE_NODE_STATIC,
	CRUSTACHE_NODE_TAG,
	CRUSTACHE_NODE_SECTION,
	CRUSTACHE_NODE_FETCH
} crustache_node_t;

struct node {
	crustache_node_t type;
	struct node *next;
};

struct node_str {
	const char *ptr;
	size_t size;
};

struct node_static {
	struct node base;
	struct node_str str;
};

struct node_fetch {
	struct node base;
	struct node_str var;
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
	struct node_str raw_content;
	int inverted;
};

struct mustache {
	char modifier;
	const char *name;
	size_t size;
};

struct crustache_template {
	struct {
		const char *chars;
		size_t size;
	} mustache_open, mustache_close;

	struct crustache_api api;

	struct {
		char *ptr;
		size_t size;
	} raw_content;

	struct node root;

	int depth;
};

static void
print_indent(int depth)
{
	int i;
	for (i = 0; i < depth; ++i)
		printf("  ");
}

static void
print_tree(struct node *node, int depth)
{
	int multi = 0;

	while (node != NULL) {
		print_indent(depth);

		switch (node->type) {
			case CRUSTACHE_NODE_MULTIROOT:
				printf("[\n");
				depth++;
				multi = 1;
				break;

			case CRUSTACHE_NODE_STATIC: {
				struct node_static *stnode = (struct node_static *)node;
				printf("static [\"%.*s\"]\n", (int)stnode->str.size, stnode->str.ptr);
				break;
			}

			case CRUSTACHE_NODE_TAG: {
				struct node_tag *tag = (struct node_tag *)node;
				printf("tag [%d] =>\n", tag->flags);
				print_tree(tag->tag_value, depth + 1);
				break;
			}

			case CRUSTACHE_NODE_FETCH: {
				struct node_fetch *fnode = (struct node_fetch *)node;
				printf("fetch [%.*s]\n", (int)fnode->var.size, fnode->var.ptr);
				break;
			}

			case CRUSTACHE_NODE_SECTION: {
				struct node_section *section = (struct node_section *)node;
				printf("section [%c] =>\n", section->inverted ? 'i' : 'n');

				print_indent(depth + 1);
				printf("key =>\n");
				print_tree(section->section_key, depth + 2);

				print_indent(depth + 1);
				printf("contents =>\n");
				print_tree(section->content, depth + 2);

				break;
			}
		}

		node = node->next;
	}

	if (multi) {
		print_indent(depth - 1);
		printf("]\n");
	}
}

static void
node_free(struct node *node)
{
	while (node != NULL) {
		struct node *next = node->next;

		switch (node->type) {
		case CRUSTACHE_NODE_TAG:
			node_free(((struct node_tag *)node)->tag_value);
			break;

		case CRUSTACHE_NODE_SECTION:
			node_free(((struct node_section *)node)->section_key);
			node_free(((struct node_section *)node)->content);
			break;

		case CRUSTACHE_NODE_MULTIROOT:
		case CRUSTACHE_NODE_STATIC:
		case CRUSTACHE_NODE_FETCH:
			break;
		}

		free(node);
		node = next;
	}
}

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

static const char *
railgun(
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
find_mustache(
	size_t *mst_pos,
	size_t *mst_size,
	struct crustache_template *template,
	size_t i,
	const char *buffer,
	size_t size)
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

	/* TODO: greedy matching */

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

	mst->name = buffer;
	mst->size = size;
	return 0;
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
	/* TODO: closing delims */
	return 0;
}

static int
parse_mustache_name(struct node_str *str, struct mustache *mst)
{
	size_t i;

	for (i = 0; i < mst->size; ++i) {
		if (!isalnum(mst->name[i]) && mst->name[i] != '_')
			return -1;
	}

	str->ptr = mst->name;
	str->size = mst->size;
	return 0;
}

static int
parse_internal(
	struct crustache_template *template,
	const char *buffer,
	size_t size,
	struct node *root_node)
{
	size_t i = 0;
	int result;

	struct parray node_stack;

	if (template->depth++ >= MAX_TEMPLATE_RECURSION)
		return -1;

	parr_init(&node_stack);
	parr_push(&node_stack, root_node);

	while (i < size) {
		size_t mst_pos, mst_size;
		struct mustache mst;

		if (find_mustache(&mst_pos, &mst_size, template, i, buffer, size) < 0)
			return -1;

		if (mst_pos > i) {
			struct node_static *stnode;
			struct node *old_root;

			stnode = malloc(sizeof(struct node_static));
			stnode->base.type = CRUSTACHE_NODE_STATIC;
			stnode->base.next = NULL;

			stnode->str.ptr = buffer + i;
			stnode->str.size = mst_pos - i;

			old_root = parr_pop(&node_stack);
			old_root->next = (struct node *)stnode;

			parr_push(&node_stack, stnode);
		}

		if (mst_pos == size)
			break;

		if (parse_mustache(&mst,
			buffer + mst_pos + template->mustache_open.size,
			mst_size - template->mustache_open.size - template->mustache_close.size) < 0)
			return -1;

		i = mst_pos + mst_size;

		switch (mst.modifier) {
			case '#': /* section */
			case '^': {
				struct node_section *section;
				struct node_fetch *section_key;
				struct node *old_root, *child_root;

				/* Child root */
				child_root = malloc(sizeof(struct node));
				child_root->type = CRUSTACHE_NODE_MULTIROOT;
				child_root->next = NULL;

				/* Section key */
				section_key = malloc(sizeof(struct node_fetch));
				section_key->base.type = CRUSTACHE_NODE_FETCH;
				section_key->base.next = NULL;
				if (parse_mustache_name(&section_key->var, &mst) < 0)
					return -1;

				/* Section node */
				section = malloc(sizeof(struct node_section));
				section->base.type = CRUSTACHE_NODE_SECTION;
				section->base.next = NULL;
				section->section_key = (struct node *)section_key;
				section->content = child_root;
				section->raw_content.ptr = buffer + i;
				section->raw_content.size = 0;
				section->inverted = (mst.modifier == '^');

				old_root = parr_pop(&node_stack);
				old_root->next = (struct node *)section;

				parr_push(&node_stack, section);
				parr_push(&node_stack, child_root);
				break;
			}

			case '/': { /* closing section */
				struct node_section *section_open;
				struct node_fetch *section_open_key;

				/* pop the subtree */
				parr_pop(&node_stack);

				/* top of the stack should now be the SECTION node that
				 * was hosting the subtree */
				section_open = parr_top(&node_stack);
				if (!section_open || section_open->base.type != CRUSTACHE_NODE_SECTION)
					return -1;

				section_open_key = (struct node_fetch *)section_open->section_key;

				if (section_open_key->var.size != mst.size ||
					memcmp(section_open_key->var.ptr, mst.name, mst.size) != 0)
					return -1;

				section_open->raw_content.size = (buffer + mst_pos - section_open->raw_content.ptr);
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
			default: { /* normal tag */
				struct node_tag *tag;
				struct node_fetch *tag_name;
				struct node *old_root;

				/* Section name */
				tag_name = malloc(sizeof(struct node_fetch));
				tag_name->base.type = CRUSTACHE_NODE_FETCH;
				tag_name->base.next = NULL;
				if (parse_mustache_name(&tag_name->var, &mst) < 0)
					return -1;

				/* actual tag */
				tag = malloc(sizeof(struct node_tag));
				tag->base.type = CRUSTACHE_NODE_TAG;
				tag->base.next = NULL;
				tag->tag_value = (struct node *)tag_name;
				tag->flags = 0;

				/* push into stack */
				old_root = parr_pop(&node_stack);
				old_root->next = (struct node *)tag;
				parr_push(&node_stack, tag);
				break;
			}
		} /* switch */

		if (result < 0)
			return result;
	}

	return 0;
}

static void free_var(struct crustache_template *template, struct crustache_var *var)
{
	if (template->api.var_free != NULL)
		template->api.var_free(var->type, var->data);
}

static int
render_node(
	struct buf *ob,
	struct crustache_template *template,
	struct node *node,
	struct crustache_var *context);

static int
render_node_static(struct buf *ob, struct node_static *node)
{
	bufput(ob, node->str.ptr, node->str.size);
	return 0;
}

static int
render_node_fetch(
	struct crustache_var *out,
	struct crustache_template *template,
	struct node_fetch *node,
	struct crustache_var *context)
{
	if (node->base.type != CRUSTACHE_NODE_FETCH || context->type != CRUSTACHE_VAR_HASH)
		return -1;

	out->type = CRUSTACHE_VAR_FALSE;
	out->data = NULL;
	template->api.hash_get(out, context->data, node->var.ptr, node->var.size);

	return 0;
}

static int
render_node_tag(
	struct buf *ob,
	struct crustache_template *template,
	struct node_tag *node,
	struct crustache_var *context)
{
	struct crustache_var tag_value;

	if (render_node_fetch(&tag_value, template, (struct node_fetch *)node->tag_value, context) < 0)
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
render_node_section(
	struct buf *ob,
	struct crustache_template *template,
	struct node_section *node,
	struct crustache_var *context)
{
	struct crustache_var section_key;
	int result = 0;

	if (render_node_fetch(&section_key, template, (struct node_fetch *)node->section_key, context) < 0)
		return -1;

	if (node->inverted) {
		if (section_key.type == CRUSTACHE_VAR_FALSE ||
			(section_key.type == CRUSTACHE_VAR_LIST && section_key.size == 0))
			result = render_node(ob, template, node->content, context); /* TODO: context? */

	} else {
		switch (section_key.type) {
		case CRUSTACHE_VAR_FALSE:
			break;

		case CRUSTACHE_VAR_HASH:
			result = render_node(ob, template, node->content, &section_key);
			break;

		case CRUSTACHE_VAR_LIST:
		{
			size_t i;

			for (i = 0; result == 0 && i < section_key.size; ++i) {
				struct crustache_var subcontext = {0, 0, 0};
				template->api.list_get(&subcontext, section_key.data, i);
				result = render_node(ob, template, node->content, &subcontext);
				free_var(template, &subcontext);
			}
			break;
		}

		case CRUSTACHE_VAR_LAMBDA:
		{
			struct crustache_var lambda_result = {0, 0, 0};

			template->api.lambda(
				&lambda_result,
				section_key.data,
				node->raw_content.ptr,
				node->raw_content.size);

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
render_node(
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
		case CRUSTACHE_NODE_MULTIROOT:
			break;

		case CRUSTACHE_NODE_STATIC:
			result = render_node_static(ob, (struct node_static *)node);
			break;

		case CRUSTACHE_NODE_TAG:
			result = render_node_tag(ob, template, (struct node_tag *)node, context);
			break;

		case CRUSTACHE_NODE_SECTION:
			result = render_node_section(ob, template, (struct node_section *)node, context);
			break;

		default:
			return -1;
		}

		node = node->next;
	}

	return result;
}

int
crustache_render(struct buf *ob, struct crustache_template *template, struct crustache_var *context)
{
	return render_node(ob, template, &template->root, context);
}

int
crustache_new_template(struct crustache_template **output, const char *raw_template, size_t raw_length)
{
	static const char MUSTACHE_OPEN[] = "{{";
	static const char MUSTACHE_CLOSE[] = "}}";

	struct crustache_template *crt;

	crt = malloc(sizeof(struct crustache_template));
	if (!crt)
		return -1;

	memset(crt, 0x0, sizeof(struct crustache_template));
	*output = crt;

	crt->raw_content.ptr = malloc(raw_length);
	crt->raw_content.size = raw_length;
	memcpy(crt->raw_content.ptr, raw_template, raw_length);

	crt->mustache_open.chars = MUSTACHE_OPEN;
	crt->mustache_open.size = 2;

	crt->mustache_close.chars = MUSTACHE_CLOSE;
	crt->mustache_close.size = 2;

	crt->depth = 0;

	crt->root.type = CRUSTACHE_NODE_MULTIROOT;
	crt->root.next = NULL;

	return parse_internal(crt, crt->raw_content.ptr, crt->raw_content.size, &crt->root);
}

void
crustache_free_template(struct crustache_template *template)
{
	node_free(template->root.next);
	free(template->raw_content.ptr);
	free(template);
}

#define TEST
#ifdef TEST
int main()
{
	const char TEST_TEMPLATE[] = "This is {{just}} a {{&simple}} test with {{#sname}} a {{inside}} section {{/sname}} end";

	struct crustache_template *tmpl;

	if (crustache_new_template(&tmpl, TEST_TEMPLATE, STRLEN(TEST_TEMPLATE)) == 0) {
		print_tree(&tmpl->root, 0);
	} else {
		printf("FAILED TO PARSE TEMPLATE\n");
	}

	crustache_free_template(tmpl);
	return 0;
}
#endif
