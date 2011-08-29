Crustache, an embeddable templating engine
==========================================

> Hubot mustache me C

CRUSTACHE! Shout it loud when you read it. With anger.
It is an (experimental) implementation of the [{{Mustache}}][http://mustache.github.com/]
templating engine, in C, with the goal of improving the speed of
other Mustache implementations in higher level languages.

You should totally try it out. It may blow up your computer. Or it may
increase the rendering speed of your native Crustache implementation
by up to 40 times. WHO KNOWS WHATS GONNA HAPPEN. DO IT FOR SCIENCE.

## Features

Currently Crustache supports all the features available in the original Mustache.
Yes, even partials. Ain't that awesome?

- Variables
- Sections (including lambdas, lists, inverted sections)
- Comments
- Partials
- Set tag delimiters

## Try out Crustache.rb!

You probably don't have the time to write a Crustache wrapper for a high
level language (even though it's super easy and quick and fun and you should
do it anyway), so I've written Crustache.rb as a reference implementation, and
to let you try the library out.

Crustache.rb is a simple wrapper that implements a `Crustache::Template` class,
in Ruby. It has the same API as the `Mustache::Template` class in the original
Mustache, so you can use it standalone (works *very* nicely):

~~~ ruby
require 'crustache'

Crustache::Template.new(
    'This was rendered with {{mustache}}.').render("mustache" => "crustache")
~~~

Or you can do **SCIENCE**:

~~~ ruby
# dangerous science ahead -- don't try at home
require 'mustache'
require 'crustache'

# Monkeypatch from hell
Mustache::Template = Crustache::Template

class MyView < Mustache
    ... # Powerman 9000 - When worlds collide
end

# How is this thing even working?
~~~~

## Get your own Crustache!

The main goal of Crustache is acting as a backend for other Mustache
implementations. Here's a rough overview on the Crustache API and how
to use it.

*Note from the author:* when you start getting a mild headache and/or a
skin rash from this technical documentation, I suggest you check out
the reference implementation, Crustache.rb.


### Defining the Crustache API

Crustache is data-model-agnostic. That's a fancy way of saying it doesn't
really understand about hash tables, lists and so on -- which is a reasonable
thing for a C library, given that C doesn't even have *strings* for shit's sake.

Before rendering a Crustache template, you must define a Crustache API to
stablish how the renderer interacts with the different data types in the
rendering context.

The Crustache API is defined as a series of callbacks, which the library uses
while rendering to gather the relevant variable data.

~~~~ c
typedef struct {
	int (*context_find)(crustache_var *, void *context, const char *key, size_t key_size);
	int (*list_get)(crustache_var *, void *list, size_t i);
	int (*lambda)(crustache_var *, void *lambda, const char *raw_template, size_t raw_size);
	void (*var_free)(crustache_var_t type, void *var);

	int (*partial)(crustache_template **partial, const char *partial_name, size_t name_size);
	int free_partials;
} crustache_api;
~~~~

- `int context_find(crustache_var *, void*, const char *, size_t)`:

    The `context_find` callback is issued everytime the renderer needs to fetch
    a variable from the context. You can think of it as a a hash table loookup --
    but of course, you can pass anything as a context (a hash table, a class instance...)

    The context will be passed as a `void` pointer, so you should cast it back to
    its original type. You can then query it for the variable `key`.

    The queried variable must be made opaque again and stored in the `var` pointer
    so the library can process it.

    The method must return `0` if the variable was found and stored, or a negative
    value if it was not found or there was an error.

    Here's an example on how to implement context lookups using the Ruby C API:

    ~~~ c
    static int
    rb_crustache__context_get(crustache_var *var, void *ctx,
        const char *key, size_t key_size)
    {
        VALUE rb_hash = (VALUE)ctx;
        VALUE rb_key, rb_val;

        rb_key = rb_str_new(key, (long)key_size);
        rb_val = rb_hash_lookup(rb_ctx, rb_key);

        if (NIL_P(rb_val)) /* not found */
            return -1;

        rb_crustache__setvar(var, rb_val);
        return 0;
    }
    ~~~


- `int (*list_get)(crustache_var *, void *list, size_t i)`:

    The `list_get` callback is issued when Crustache needs to access a variable
    which you have previously defined as a list.

    The list will be passed as a `void` pointer, which you can cast to whatever
    your native type is.

    You are then expected to lookup the variable in position `i` inside the
    list, and store it in the `var` pointer so the library can process it.

    The method must return `0` if the variable was found and stored, or a negative
    value if it was not found or there was an error.


- `int (*lambda)(crustache_var *, void *, const char *, size_t)`:

    The `lambda` callback is issued when Crustache finds a Section whose tag resolved
    to a lambda ("callable") object.

    Like in the original Mustache, the lambda callback will receive a string containing
    part of the original template, which must be processed (or not) by the callback
    and returned also as a string.

    The returned string will be replaced in the template.

    The method must return `0` if the template fragment was successfully processed,
    or a negative number otherwise.


- `int (*partial)(crustache_template **, const char *, size_t)`

    The `partial` callback is issued when Crustache encounters a partial tag in the
    original template.

    The name of the template will be passed in the `template_name` variable, and this
    name must be resolved to an actual template string (using whatever method you deem
    reasonable, e.g. look on the filesystem for a file called `template_name.mustache`).

    The string must then be instantiated as a `crustache_template`, which will be
    automatically rendered using the parent template's active context.

    Here's an example implementation:

    ~~~~ c
    static int
    int partial(crustache_template **template,
            const char *tmpl_name, size_t size)
    {
        const char *template_string;

        template_string = lookup_template(tmpl_name, size);
        if (template_string == NULL)
            return -1;

        return crustache_new(template, &DEFAULT_API,
            template_string, strlen(template_string));
    }
    ~~~~

    If the `free_templates` variable is set to 1, the new template will be freed by
    the library once it has been rendered.

- `void (*var_free)(crustache_var_t type, void *var)`

    If this optional callback is not NULL, it will be called everytime Crustache
    no longer needs a variable for rendering, so it can be freed by whatever
    means you want.
    

### Using Crustache

    Once the interaction API has been defined, using crustache is *sooo* easy:

- `int crustache_new(crustache_template **output, crustache_api *api, const char *raw_template, size_t raw_length)`:

    Create a new Crustache template. The `api` paremeter is a pointer to your defined API for
    context interaction. `raw_template` points to the raw text of the template. A copy of this text
    will be stored internally by the template.

    The raw template text will be parsed and compiled internally, ready for rendering. The new template
    will be stored in the `output` pointer.

    If the compilation or parsing fails, a negative value (error code) will be returned,
    but the template object will be created anyway. The template object can then be queried for
    the syntax error(s) in the raw text.

- `void crustache_free(crustache_template *template)`:

    Free an existing Crustache template, once it's no longer needed. Crustache templates must be
    free'd even if their compilation with `crustache_new` failed.

- `int crustache_render(struct buf *ob, crustache_template *template, crustache_var *context)`:

    Render a compiled template, and write the rendered output to the `ob` buffer.

    `template` is a pointer to a previously compiled template. `context` is an opaque pointer to
    the initial context for the template, which will be accessed through the Crustache API.

    The method will return 0 on success, or a negative value (error code) if the rendering failed
    for whatever reason.

- `const char * crustache_error_syntaxline(
	size_t *line_n, size_t *col_n, size_t *line_len, crustache_template *template)`:

    Query the detailed information about the last compilation error in the template. The returned
    value is the exact position in the raw text of the template where the compilation failed. This
    information will only be available if `crustache_new` returned an error code.

- `void crustache_error_rendernode(char *buffer, size_t size, crustache_template *template)`:

    Query the detailed information about the last rendering error in the template. The returned
    value is a textual representation of the Mustache node in the compiled template that could not
    be rendered with the given context. This information will only be available if `crustache_render`
    returned an error code.

- `const char * crustache_strerror(int error)`

    Get a representative error message from a given error code.
