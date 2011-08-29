#include "houdini.h"

static const char HTML_ESCAPE_TABLE[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 1, 0, 0, 0, 2, 3, 0, 0, 0, 0, 0, 0, 0, 4, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 6, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char *HTML_ESCAPES[] = {
	"",
	"&quot;",
	"&amp;",
	"&#39;",
	"&#47;",
	"&lt;",
	"&gt;"
};

void
houdini_escape_html(struct buf *ob, const char *src, size_t size)
{
	size_t  i = 0, org, esc;

	while (i < size) {
		org = i;
		while (i < size &&
			(esc = HTML_ESCAPE_TABLE[src[i] & 0x7F]) == 0 &&
			(src[i] & ~0x7F) == 0)
			i++;

		if (i > org)
			bufput(ob, src + org, i - org);

		/* escaping */
		if (i >= size)
			break;

		bufputs(ob, HTML_ESCAPES[esc]);
		i++;
	}
}

void
houdini_unescape_html(struct buf *ob, const char *src, size_t size)
{
	size_t  i = 0, org;

	while (i < size) {
		org = i;
		while (i < size && src[i] != '&')
			i++;

		if (i > org)
			bufput(ob, src + org, i - org);

		/* escaping */
		if (i >= size)
			break;

		#define REPLACE(pat,rep)\
			if (i + sizeof(pat) <= size && !memcmp(src + i + 1, pat, sizeof(pat) - 1)) { bufputc(ob, rep); i += sizeof(pat); }

		REPLACE("lt;", '<')
		else REPLACE("gt;", '>')
		else REPLACE("amp;", '&')
		else REPLACE("#39;", '\'')
		else REPLACE("#47;", '/')
		else REPLACE("quot;", '"')
		else {
			bufputc(ob, src[i]);
			i++;
		}

		#undef REPLACE
	}
}
