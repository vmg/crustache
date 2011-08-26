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

		if (src[i + 1] == 'l' && src[i + 2] == 't' && src[i + 3] == ';') {
			bufputc(ob, '<');
			i += 4;
		} else if (src[i + 1] == 'g' && src[i + 2] == 't' && src[i + 3] == ';') {
			bufputc(ob, '>');
			i += 4;
		} else if (src[i + 1] == 'a' && src[i + 2] == 'm' && src[i + 3] == 'p' && src[i + 4] == ';') {
			bufputc(ob, '&');
			i += 5;
		} else if (src[i + 1] == '#' && src[i + 2] == '3' && src[i + 3] == '9' && src[i + 4] == ';') {
			bufputc(ob, '\'');
			i += 5;
		} else if (src[i + 1] == '#' && src[i + 2] == '4' && src[i + 3] == '7' && src[i + 4] == ';') {
			bufputc(ob, '/');
			i += 5;
		} else if (src[i + 1] == 'q' && src[i + 2] == 'u' && src[i + 3] == 'o' && src[i + 4] == 't' && src[i + 5] == ';') {
			bufputc(ob, '"');
			i += 6;
		} else {
			bufputc(ob, src[i]);
			i++;
		}
	}
}
