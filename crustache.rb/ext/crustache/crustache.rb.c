/*
 * Copyright (c) 2011, Vicent Marti
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#define RSTRING_NOT_MODIFIED

#include "ruby.h"

#ifdef HAVE_RUBY_ENCODING_H
#include <ruby/encoding.h>
#else
#define rb_enc_copy(dst, src)
#endif

#include "crustache.h"
#include "buffer.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static VALUE rb_mCrustache;
static VALUE rb_cTemplate;
static VALUE rb_eParse;
static VALUE rb_eRender;

static void
rb_crustache__setvar(crustache_var *variable, VALUE rb_obj)
{

	switch (TYPE(rb_obj)) {
	case T_ARRAY:
		variable->type = CRUSTACHE_VAR_LIST;
		variable->data = (void *)rb_obj;
		variable->size = RARRAY_LEN(rb_obj);
		break;

	case T_HASH:
		variable->type = CRUSTACHE_VAR_CONTEXT;
		variable->data = (void *)rb_obj;
		break;

	case T_STRING:
		variable->type = CRUSTACHE_VAR_STR;
		variable->data = (void *)RSTRING_PTR(rb_obj);
		variable->size = RSTRING_LEN(rb_obj);
		break;

	case T_NIL:
	case T_FALSE:
		variable->type = CRUSTACHE_VAR_FALSE;
		break;

	default:
		if (rb_respond_to(rb_obj, rb_intern("call"))) {
			variable->type = CRUSTACHE_VAR_LAMBDA;
			variable->data = (void *)rb_obj;
			break;
		}

		variable->type = CRUSTACHE_VAR_CONTEXT;
		variable->data = (void *)rb_obj;
	}
}

static int
rb_crustache__context_get(
	crustache_var *var,
	void *ctx,
	const char *key,
	size_t key_size)
{
	VALUE rb_ctx = (VALUE)ctx;
	VALUE rb_key, rb_val;

	rb_key = rb_str_new(key, (long)key_size);
	rb_val = Qnil;

	if (TYPE(rb_ctx) == T_HASH) {
		rb_val = rb_hash_lookup(rb_ctx, rb_key);
		if (NIL_P(rb_val))
			rb_val = rb_hash_lookup(rb_ctx, rb_str_intern(rb_key));
	} else {
		ID method = rb_to_id(rb_key);

		if (rb_respond_to(rb_ctx, method))
			rb_val = rb_funcall(rb_ctx, method, 0);

		else if (rb_respond_to(rb_ctx, rb_intern("[]")))
			rb_val = rb_funcall(rb_ctx, rb_intern("[]"), 1, key);
	}

	if (NIL_P(rb_val)) /* not found */
		return -1;

	rb_crustache__setvar(var, rb_val);
	return 0;
}

static void
rb_crustache__list_get(
	crustache_var *var,
	void *list,
	size_t i)
{
	VALUE rb_array = (VALUE)list;
	Check_Type(rb_array, T_ARRAY);
	rb_crustache__setvar(var, rb_ary_entry(rb_array, (long)i));
}

static void
rb_crustache__lambda(
	crustache_var *var,
	void *lambda,
	const char *raw_template,
	size_t raw_size)
{
	VALUE rb_lambda = (VALUE)lambda;
	VALUE rb_val, rb_tmpl;

	rb_tmpl = rb_str_new(raw_template, (long)raw_size);
	rb_val = rb_funcall(rb_lambda, rb_intern("call"), 1, rb_tmpl);
	Check_Type(rb_val, T_STRING);

	rb_crustache__setvar(var, rb_val);
}

static void
rb_template__free(void *template)
{
	crustache_free_template((crustache_template *)template);
}

static void
rb_template__parser_error(crustache_template *template, int error)
{
	const char *error_line;
	size_t line_len, line_n, col_n;

	error_line = crustache_error_syntaxline(&line_n, &col_n, &line_len, template);
	crustache_free_template(template);

	rb_raise(rb_eParse,
		"%s (line %d, col %d)\n\t%.*s\n\t%*s\n",
		crustache_strerror(error), (int)line_n, (int)col_n,
		(int)line_len, error_line,
		(int)col_n, "^");
}

static void
rb_template__render_error(crustache_template *template, int error)
{
	char error_node[256];
	crustache_error_rendernode(error_node, sizeof(error_node), template);
	rb_raise(rb_eRender, "%s (%s)", crustache_strerror(error), error_node);
}

static VALUE
rb_template_new(VALUE klass, VALUE rb_raw_template)
{
	static crustache_api DEFAULT_API = {
		rb_crustache__context_get,
		rb_crustache__list_get,
		rb_crustache__lambda,
		NULL
	};

	crustache_template *template;
	int error;

	error = crustache_new_template(&template,
		&DEFAULT_API,
		RSTRING_PTR(rb_raw_template),
		RSTRING_LEN(rb_raw_template));

	if (error < 0)
		rb_template__parser_error(template, error);

	return Data_Wrap_Struct(klass, NULL, rb_template__free, template);
}

static VALUE
rb_template_render(VALUE self, VALUE rb_context)
{
	crustache_template *template;
	crustache_var ctx;
	struct buf *output_buf;

	int error;
	VALUE result;

	Data_Get_Struct(self, crustache_template, template);
	rb_crustache__setvar(&ctx, rb_context);
	output_buf = bufnew(128);

	error = crustache_render(output_buf, template, &ctx);
	if (error < 0)
		rb_template__render_error(template, error);

	result = rb_str_new(output_buf->data, output_buf->size);
	bufrelease(output_buf);

	return result;
}

void Init_crustache()
{
	rb_mCrustache = rb_define_module("Crustache");

	rb_eParse = rb_define_class_under(rb_mCrustache, "ParserError", rb_eException);
	rb_eRender = rb_define_class_under(rb_mCrustache, "RenderError", rb_eException);

	rb_cTemplate = rb_define_class_under(rb_mCrustache, "Template", rb_cObject);
	rb_define_singleton_method(rb_cTemplate, "new", rb_template_new, 1);
	rb_define_method(rb_cTemplate, "render", rb_template_render, 1);
}

