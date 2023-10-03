/*
 * MD4C: Markdown parser for C
 * (http://github.com/mity/md4c)
 *
 * Copyright (c) 2016-2019 Martin Mitas
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>

#include "md4c-html.h"
#include "entity.h"


#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199409L
    /* C89/90 or old compilers in general may not understand "inline". */
    #if defined __GNUC__
        #define inline __inline__
    #elif defined _MSC_VER
        #define inline __inline
    #else
        #define inline
    #endif
#endif

#ifdef _WIN32
    #define snprintf _snprintf
#endif

#define SELF_LINK_MAX_CHARS 1024

#define MD_HTML_TRY(lvalue, expr) \
    do { \
        lvalue = expr; \
        if (lvalue != 0) return lvalue; \
    } while(0)

typedef struct MD_SELF_LINK_tag MD_SELF_LINK;
struct MD_SELF_LINK_tag {
    char text[SELF_LINK_MAX_CHARS];
    unsigned text_size;
    unsigned count;
    MD_SELF_LINK* next;
};

struct MD_HTML_tag {
    void (*process_output)(const MD_CHAR*, MD_SIZE, void*);
    int (*render_self_link)(const MD_CHAR*, MD_SIZE, void*, MD_HTML* html,
            int (*render)(MD_HTML* html, const MD_CHAR* data, MD_SIZE size));
    int (*record_self_link)(const MD_CHAR*, MD_SIZE, void*);
    int (*render_code_link)(const MD_CHAR*, MD_SIZE, void*, MD_HTML* html,
            int (*render)(MD_HTML* html, const MD_CHAR* data, MD_SIZE size));
    void* userdata;
    unsigned flags;
    int image_nesting_level;
    char escape_map[256];
};

#define NEED_HTML_ESC_FLAG   0x1
#define NEED_URL_ESC_FLAG    0x2


/*****************************************
 ***  HTML rendering helper functions  ***
 *****************************************/

#define ISDIGIT(ch)     ('0' <= (ch) && (ch) <= '9')
#define ISLOWER(ch)     ('a' <= (ch) && (ch) <= 'z')
#define ISUPPER(ch)     ('A' <= (ch) && (ch) <= 'Z')
#define ISALNUM(ch)     (ISLOWER(ch) || ISUPPER(ch) || ISDIGIT(ch))


static inline int
render_verbatim(MD_HTML* r, const MD_CHAR* text, MD_SIZE size)
{
    r->process_output(text, size, r->userdata);
    return 0;
}

/* Keep this as a macro. Most compiler should then be smart enough to replace
 * the strlen() call with a compile-time constant if the string is a C literal. */
#define RENDER_VERBATIM(r, verbatim)                                    \
        render_verbatim((r), (verbatim), (MD_SIZE) (strlen(verbatim)))


static int
render_html_escaped(MD_HTML* r, const MD_CHAR* data, MD_SIZE size)
{
    MD_OFFSET beg = 0;
    MD_OFFSET off = 0;
    int ret = 0;

    /* Some characters need to be escaped in normal HTML text. */
    #define NEED_HTML_ESC(ch)   (r->escape_map[(unsigned char)(ch)] & NEED_HTML_ESC_FLAG)

    while(1) {
        /* Optimization: Use some loop unrolling. */
        while(off + 3 < size  &&  !NEED_HTML_ESC(data[off+0])  &&  !NEED_HTML_ESC(data[off+1])
                              &&  !NEED_HTML_ESC(data[off+2])  &&  !NEED_HTML_ESC(data[off+3]))
            off += 4;
        while(off < size  &&  !NEED_HTML_ESC(data[off]))
            off++;

        if(off > beg)
            MD_HTML_TRY(ret, render_verbatim(r, data + beg, off - beg));

        if(off < size) {
            switch(data[off]) {
                case '&':   RENDER_VERBATIM(r, "&amp;"); break;
                case '<':   RENDER_VERBATIM(r, "&lt;"); break;
                case '>':   RENDER_VERBATIM(r, "&gt;"); break;
                case '"':   RENDER_VERBATIM(r, "&quot;"); break;
            }
            off++;
        } else {
            break;
        }
        beg = off;
    }

    return ret;
}

static int
render_url_escaped(MD_HTML* r, const MD_CHAR* data, MD_SIZE size)
{
    static const MD_CHAR hex_chars[] = "0123456789ABCDEF";
    MD_OFFSET beg = 0;
    MD_OFFSET off = 0;
    int ret = 0;

    /* Some characters need to be escaped in URL attributes. */
    #define NEED_URL_ESC(ch)    (r->escape_map[(unsigned char)(ch)] & NEED_URL_ESC_FLAG)

    while(1) {
        while(off < size  &&  !NEED_URL_ESC(data[off]))
            off++;
        if(off > beg)
            MD_HTML_TRY(ret, render_verbatim(r, data + beg, off - beg));

        if(off < size) {
            char hex[3];

            switch(data[off]) {
                case '&':   RENDER_VERBATIM(r, "&amp;"); break;
                default:
                    hex[0] = '%';
                    hex[1] = hex_chars[((unsigned)data[off] >> 4) & 0xf];
                    hex[2] = hex_chars[((unsigned)data[off] >> 0) & 0xf];
                    MD_HTML_TRY(ret, render_verbatim(r, hex, 3));
                    break;
            }
            off++;
        } else {
            break;
        }

        beg = off;
    }

    return ret;
}

static int
render_codelink_url_escaped(MD_HTML* r, const MD_CHAR* data, MD_SIZE size)
{
    if (r->render_code_link) {
        return r->render_code_link(data, size, r->userdata, r, render_url_escaped);
    } else {
        render_url_escaped(r, data, size);
        return 0;
    }
}

static int
render_self_url_escaped(MD_HTML* r, const MD_CHAR* data, MD_SIZE size)
{
    if (r->render_self_link) {
        return r->render_self_link(data, size, r->userdata, r, render_url_escaped);
    } else {
        render_url_escaped(r, data, size);
        return 0;
    }
}

static int
record_self_url(MD_HTML* r, const MD_CHAR* data, MD_SIZE size)
{
    if (r->record_self_link)
        return r->record_self_link(data, size, r->userdata);
    else
        return 0;
}

static unsigned
hex_val(char ch)
{
    if('0' <= ch && ch <= '9')
        return ch - '0';
    if('A' <= ch && ch <= 'Z')
        return ch - 'A' + 10;
    else
        return ch - 'a' + 10;
}

static int
render_utf8_codepoint(MD_HTML* r, unsigned codepoint,
                      int (*fn_append)(MD_HTML*, const MD_CHAR*, MD_SIZE))
{
    static const MD_CHAR utf8_replacement_char[] = { 0xef, 0xbf, 0xbd };
    int ret = 0;

    unsigned char utf8[4];
    size_t n;

    if(codepoint <= 0x7f) {
        n = 1;
        utf8[0] = codepoint;
    } else if(codepoint <= 0x7ff) {
        n = 2;
        utf8[0] = 0xc0 | ((codepoint >>  6) & 0x1f);
        utf8[1] = 0x80 + ((codepoint >>  0) & 0x3f);
    } else if(codepoint <= 0xffff) {
        n = 3;
        utf8[0] = 0xe0 | ((codepoint >> 12) & 0xf);
        utf8[1] = 0x80 + ((codepoint >>  6) & 0x3f);
        utf8[2] = 0x80 + ((codepoint >>  0) & 0x3f);
    } else {
        n = 4;
        utf8[0] = 0xf0 | ((codepoint >> 18) & 0x7);
        utf8[1] = 0x80 + ((codepoint >> 12) & 0x3f);
        utf8[2] = 0x80 + ((codepoint >>  6) & 0x3f);
        utf8[3] = 0x80 + ((codepoint >>  0) & 0x3f);
    }

    if(0 < codepoint  &&  codepoint <= 0x10ffff)
        MD_HTML_TRY(ret, fn_append(r, (char*)utf8, (MD_SIZE)n));
    else
        MD_HTML_TRY(ret, fn_append(r, utf8_replacement_char, 3));
    return ret;
}

/* Translate entity to its UTF-8 equivalent, or output the verbatim one
 * if such entity is unknown (or if the translation is disabled). */
static int
render_entity(MD_HTML* r, const MD_CHAR* text, MD_SIZE size,
              int (*fn_append)(MD_HTML*, const MD_CHAR*, MD_SIZE))
{
    int ret = 0;

    if(r->flags & MD_HTML_FLAG_VERBATIM_ENTITIES) {
        MD_HTML_TRY(ret, render_verbatim(r, text, size));
        return ret;
    }

    /* We assume UTF-8 output is what is desired. */
    if(size > 3 && text[1] == '#') {
        unsigned codepoint = 0;

        if(text[2] == 'x' || text[2] == 'X') {
            /* Hexadecimal entity (e.g. "&#x1234abcd;")). */
            MD_SIZE i;
            for(i = 3; i < size-1; i++)
                codepoint = 16 * codepoint + hex_val(text[i]);
        } else {
            /* Decimal entity (e.g. "&1234;") */
            MD_SIZE i;
            for(i = 2; i < size-1; i++)
                codepoint = 10 * codepoint + (text[i] - '0');
        }

        MD_HTML_TRY(ret, render_utf8_codepoint(r, codepoint, fn_append));
        return ret;
    } else {
        /* Named entity (e.g. "&nbsp;"). */
        const struct entity* ent;

        ent = entity_lookup(text, size);
        if(ent != NULL) {
            MD_HTML_TRY(ret, render_utf8_codepoint(r, ent->codepoints[0], fn_append));
            if(ent->codepoints[1])
                MD_HTML_TRY(ret, render_utf8_codepoint(r, ent->codepoints[1], fn_append));
            return ret;
        }
    }

    MD_HTML_TRY(ret, fn_append(r, text, size));
    return ret;
}

static int
render_attribute(MD_HTML* r, const MD_ATTRIBUTE* attr,
                 int (*fn_append)(MD_HTML*, const MD_CHAR*, MD_SIZE))
{
    int i;
    int ret = 0;

    for(i = 0; attr->substr_offsets[i] < attr->size; i++) {
        MD_TEXTTYPE type = attr->substr_types[i];
        MD_OFFSET off = attr->substr_offsets[i];
        MD_SIZE size = attr->substr_offsets[i+1] - off;
        const MD_CHAR* text = attr->text + off;

        switch(type) {
            case MD_TEXT_NULLCHAR: MD_HTML_TRY(ret, render_utf8_codepoint(r, 0x0000, render_verbatim)); break;
            case MD_TEXT_ENTITY:   MD_HTML_TRY(ret, render_entity(r, text, size, fn_append)); break;
            default:               MD_HTML_TRY(ret, fn_append(r, text, size)); break;
        }
    }

    return ret;
}


static int
render_open_ol_block(MD_HTML* r, const MD_BLOCK_OL_DETAIL* det)
{
    char buf[64];
    int ret = 0;

    if(det->start == 1) {
        RENDER_VERBATIM(r, "<ol>\n");
        return ret;
    }

    snprintf(buf, sizeof(buf), "<ol start=\"%u\">\n", det->start);
    RENDER_VERBATIM(r, buf);
    return ret;
}

static int
render_open_li_block(MD_HTML* r, const MD_BLOCK_LI_DETAIL* det)
{
    int ret = 0;

    if(det->is_task) {
        RENDER_VERBATIM(r, "<li class=\"task-list-item\">"
                          "<input type=\"checkbox\" class=\"task-list-item-checkbox\" disabled");
        if(det->task_mark == 'x' || det->task_mark == 'X')
            RENDER_VERBATIM(r, " checked");
        RENDER_VERBATIM(r, ">");
    } else {
        RENDER_VERBATIM(r, "<li>");
    }
    return ret;
}

static int
render_open_code_block(MD_HTML* r, const MD_BLOCK_CODE_DETAIL* det)
{
    int ret = 0;

    RENDER_VERBATIM(r, "<pre><code");

    /* If known, output the HTML 5 attribute class="language-LANGNAME". */
    if(det->lang.text != NULL) {
        RENDER_VERBATIM(r, " class=\"language-");
        MD_HTML_TRY(ret, render_attribute(r, &det->lang, render_html_escaped));
        RENDER_VERBATIM(r, "\"");
    }

    RENDER_VERBATIM(r, ">");
    return ret;
}

static int
render_open_td_block(MD_HTML* r, const MD_CHAR* cell_type, const MD_BLOCK_TD_DETAIL* det)
{
    int ret = 0;

    RENDER_VERBATIM(r, "<");
    RENDER_VERBATIM(r, cell_type);

    switch(det->align) {
        case MD_ALIGN_LEFT:     RENDER_VERBATIM(r, " align=\"left\">"); break;
        case MD_ALIGN_CENTER:   RENDER_VERBATIM(r, " align=\"center\">"); break;
        case MD_ALIGN_RIGHT:    RENDER_VERBATIM(r, " align=\"right\">"); break;
        default:                RENDER_VERBATIM(r, ">"); break;
    }
    return ret;
}

static int
render_open_a_span(MD_HTML* r, const MD_SPAN_A_DETAIL* det)
{
    int ret = 0;

    RENDER_VERBATIM(r, "<a href=\"");
    MD_HTML_TRY(ret, render_attribute(r, &det->href, render_url_escaped));

    if(det->title.text != NULL) {
        RENDER_VERBATIM(r, "\" title=\"");
        MD_HTML_TRY(ret, render_attribute(r, &det->title, render_html_escaped));
    }

    RENDER_VERBATIM(r, "\">");
    return ret;
}

static int
render_open_a_codelink_span(MD_HTML* r, const MD_SPAN_A_DETAIL* det)
{
    int ret = 0;

    RENDER_VERBATIM(r, "<a href=\"");
    MD_HTML_TRY(ret, render_attribute(r, &det->href, render_codelink_url_escaped));

    if(det->title.text != NULL) {
        RENDER_VERBATIM(r, "\" title=\"");
        MD_HTML_TRY(ret, render_attribute(r, &det->title, render_html_escaped));
    }

    RENDER_VERBATIM(r, "\">");
    return ret;
}

static int
render_open_a_self_span(MD_HTML* r, const MD_SPAN_A_DETAIL* det)
{
    int ret = 0;

    RENDER_VERBATIM(r, "<a name=\"");
    MD_HTML_TRY(ret, render_attribute(r, &det->href, render_self_url_escaped));
    RENDER_VERBATIM(r, "\" href=\"#");
    MD_HTML_TRY(ret, render_attribute(r, &det->href, render_self_url_escaped));

    MD_HTML_TRY(ret, render_attribute(r, &det->href, record_self_url));

    if(det->title.text != NULL) {
        RENDER_VERBATIM(r, "\" title=\"");
        MD_HTML_TRY(ret, render_attribute(r, &det->title, render_html_escaped));
    }

    RENDER_VERBATIM(r, "\">");
    return ret;
}

static int
render_open_img_span(MD_HTML* r, const MD_SPAN_IMG_DETAIL* det)
{
    int ret = 0;

    RENDER_VERBATIM(r, "<img src=\"");
    MD_HTML_TRY(ret, render_attribute(r, &det->src, render_url_escaped));

    RENDER_VERBATIM(r, "\" alt=\"");

    r->image_nesting_level++;
    return ret;
}

static int
render_close_img_span(MD_HTML* r, const MD_SPAN_IMG_DETAIL* det)
{
    int ret = 0;

    if(det->title.text != NULL) {
        RENDER_VERBATIM(r, "\" title=\"");
        MD_HTML_TRY(ret, render_attribute(r, &det->title, render_html_escaped));
    }

    RENDER_VERBATIM(r, (r->flags & MD_HTML_FLAG_XHTML) ? "\" />" : "\">");

    r->image_nesting_level--;
    return ret;
}

static int
render_open_wikilink_span(MD_HTML* r, const MD_SPAN_WIKILINK_DETAIL* det)
{
    int ret = 0;

    RENDER_VERBATIM(r, "<x-wikilink data-target=\"");
    MD_HTML_TRY(ret, render_attribute(r, &det->target, render_html_escaped));

    RENDER_VERBATIM(r, "\">");
    return ret;
}


/**************************************
 ***  HTML renderer implementation  ***
 **************************************/

static int
enter_block_callback(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    static const MD_CHAR* head[6] = { "<h1>", "<h2>", "<h3>", "<h4>", "<h5>", "<h6>" };
    MD_HTML* r = (MD_HTML*) userdata;
    int ret = 0;

    switch(type) {
        case MD_BLOCK_DOC:      /* noop */ break;
        case MD_BLOCK_QUOTE:    RENDER_VERBATIM(r, "<blockquote>\n"); break;
        case MD_BLOCK_UL:       RENDER_VERBATIM(r, "<ul>\n"); break;
        case MD_BLOCK_OL:       ret = render_open_ol_block(r, (const MD_BLOCK_OL_DETAIL*)detail); break;
        case MD_BLOCK_LI:       ret = render_open_li_block(r, (const MD_BLOCK_LI_DETAIL*)detail); break;
        case MD_BLOCK_HR:       RENDER_VERBATIM(r, (r->flags & MD_HTML_FLAG_XHTML) ? "<hr />\n" : "<hr>\n"); break;
        case MD_BLOCK_H:        RENDER_VERBATIM(r, head[((MD_BLOCK_H_DETAIL*)detail)->level - 1]); break;
        case MD_BLOCK_CODE:     ret = render_open_code_block(r, (const MD_BLOCK_CODE_DETAIL*) detail); break;
        case MD_BLOCK_HTML:     /* noop */ break;
        case MD_BLOCK_P:        RENDER_VERBATIM(r, "<p>"); break;
        case MD_BLOCK_TABLE:    RENDER_VERBATIM(r, "<table>\n"); break;
        case MD_BLOCK_THEAD:    RENDER_VERBATIM(r, "<thead>\n"); break;
        case MD_BLOCK_TBODY:    RENDER_VERBATIM(r, "<tbody>\n"); break;
        case MD_BLOCK_TR:       RENDER_VERBATIM(r, "<tr>\n"); break;
        case MD_BLOCK_TH:       ret = render_open_td_block(r, "th", (MD_BLOCK_TD_DETAIL*)detail); break;
        case MD_BLOCK_TD:       ret = render_open_td_block(r, "td", (MD_BLOCK_TD_DETAIL*)detail); break;
    }

    return ret;
}

static int
leave_block_callback(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    static const MD_CHAR* head[6] = { "</h1>\n", "</h2>\n", "</h3>\n", "</h4>\n", "</h5>\n", "</h6>\n" };
    MD_HTML* r = (MD_HTML*) userdata;
    int ret = 0;

    switch(type) {
        case MD_BLOCK_DOC:      /*noop*/ break;
        case MD_BLOCK_QUOTE:    RENDER_VERBATIM(r, "</blockquote>\n"); break;
        case MD_BLOCK_UL:       RENDER_VERBATIM(r, "</ul>\n"); break;
        case MD_BLOCK_OL:       RENDER_VERBATIM(r, "</ol>\n"); break;
        case MD_BLOCK_LI:       RENDER_VERBATIM(r, "</li>\n"); break;
        case MD_BLOCK_HR:       /*noop*/ break;
        case MD_BLOCK_H:        RENDER_VERBATIM(r, head[((MD_BLOCK_H_DETAIL*)detail)->level - 1]); break;
        case MD_BLOCK_CODE:     RENDER_VERBATIM(r, "</code></pre>\n"); break;
        case MD_BLOCK_HTML:     /* noop */ break;
        case MD_BLOCK_P:        RENDER_VERBATIM(r, "</p>\n"); break;
        case MD_BLOCK_TABLE:    RENDER_VERBATIM(r, "</table>\n"); break;
        case MD_BLOCK_THEAD:    RENDER_VERBATIM(r, "</thead>\n"); break;
        case MD_BLOCK_TBODY:    RENDER_VERBATIM(r, "</tbody>\n"); break;
        case MD_BLOCK_TR:       RENDER_VERBATIM(r, "</tr>\n"); break;
        case MD_BLOCK_TH:       RENDER_VERBATIM(r, "</th>\n"); break;
        case MD_BLOCK_TD:       RENDER_VERBATIM(r, "</td>\n"); break;
    }

    return ret;
}

static int
enter_span_callback(MD_SPANTYPE type, void* detail, void* userdata)
{
    MD_HTML* r = (MD_HTML*) userdata;
    int ret = 0;

    if(r->image_nesting_level > 0) {
        /* We are inside a Markdown image label. Markdown allows to use any
         * emphasis and other rich contents in that context similarly as in
         * any link label.
         *
         * However, unlike in the case of links (where that contents becomes
         * contents of the <a>...</a> tag), in the case of images the contents
         * is supposed to fall into the attribute alt: <img alt="...">.
         *
         * In that context we naturally cannot output nested HTML tags. So lets
         * suppress them and only output the plain text (i.e. what falls into
         * text() callback).
         *
         * This make-it-a-plain-text approach is the recommended practice by
         * CommonMark specification (for HTML output).
         */
        return ret;
    }

    switch(type) {
        case MD_SPAN_EM:                RENDER_VERBATIM(r, "<em>"); break;
        case MD_SPAN_STRONG:            RENDER_VERBATIM(r, "<strong>"); break;
        case MD_SPAN_U:                 RENDER_VERBATIM(r, "<u>"); break;
        case MD_SPAN_A:                 ret = render_open_a_span(r, (MD_SPAN_A_DETAIL*) detail); break;
        case MD_SPAN_A_CODELINK:        ret = render_open_a_codelink_span(r, (MD_SPAN_A_DETAIL*) detail); break;
        case MD_SPAN_A_SELF:            ret = render_open_a_self_span(r, (MD_SPAN_A_DETAIL*) detail); break;
        case MD_SPAN_IMG:               ret = render_open_img_span(r, (MD_SPAN_IMG_DETAIL*) detail); break;
        case MD_SPAN_CODE:              RENDER_VERBATIM(r, "<code>"); break;
        case MD_SPAN_DEL:               RENDER_VERBATIM(r, "<del>"); break;
        case MD_SPAN_LATEXMATH:         RENDER_VERBATIM(r, "<x-equation>"); break;
        case MD_SPAN_LATEXMATH_DISPLAY: RENDER_VERBATIM(r, "<x-equation type=\"display\">"); break;
        case MD_SPAN_WIKILINK:          ret = render_open_wikilink_span(r, (MD_SPAN_WIKILINK_DETAIL*) detail); break;
    }

    return ret;
}

static int
leave_span_callback(MD_SPANTYPE type, void* detail, void* userdata)
{
    MD_HTML* r = (MD_HTML*) userdata;
    int ret = 0;

    if(r->image_nesting_level > 0) {
        /* Ditto as in enter_span_callback(), except we have to allow the
         * end of the <img> tag. */
        if(r->image_nesting_level == 1  &&  type == MD_SPAN_IMG) {
            MD_HTML_TRY(ret, render_close_img_span(r, (MD_SPAN_IMG_DETAIL*) detail));
        }
        return ret;
    }

    switch(type) {
        case MD_SPAN_EM:                RENDER_VERBATIM(r, "</em>"); break;
        case MD_SPAN_STRONG:            RENDER_VERBATIM(r, "</strong>"); break;
        case MD_SPAN_U:                 RENDER_VERBATIM(r, "</u>"); break;
        case MD_SPAN_A:                 RENDER_VERBATIM(r, "</a>"); break;
        case MD_SPAN_A_CODELINK:        RENDER_VERBATIM(r, "</a>"); break;
        case MD_SPAN_A_SELF:            RENDER_VERBATIM(r, "</a>"); break;
        case MD_SPAN_IMG:               /*noop, handled above*/ break;
        case MD_SPAN_CODE:              RENDER_VERBATIM(r, "</code>"); break;
        case MD_SPAN_DEL:               RENDER_VERBATIM(r, "</del>"); break;
        case MD_SPAN_LATEXMATH:         /*fall through*/
        case MD_SPAN_LATEXMATH_DISPLAY: RENDER_VERBATIM(r, "</x-equation>"); break;
        case MD_SPAN_WIKILINK:          RENDER_VERBATIM(r, "</x-wikilink>"); break;
    }

    return ret;
}

static int
text_callback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    MD_HTML* r = (MD_HTML*) userdata;
    int ret = 0;

    switch(type) {
        case MD_TEXT_NULLCHAR:  ret = render_utf8_codepoint(r, 0x0000, render_verbatim); break;
        case MD_TEXT_BR:        RENDER_VERBATIM(r, (r->image_nesting_level == 0
                                        ? ((r->flags & MD_HTML_FLAG_XHTML) ? "<br />\n" : "<br>\n")
                                        : " "));
                                break;
        case MD_TEXT_SOFTBR:    RENDER_VERBATIM(r, (r->image_nesting_level == 0 ? "\n" : " ")); break;
        case MD_TEXT_HTML:      ret = render_verbatim(r, text, size); break;
        case MD_TEXT_ENTITY:    ret = render_entity(r, text, size, render_html_escaped); break;
        default:                ret = render_html_escaped(r, text, size); break;
    }

    return ret;
}

static void
debug_log_callback(const char* msg, void* userdata)
{
    MD_HTML* r = (MD_HTML*) userdata;
    if(r->flags & MD_HTML_FLAG_DEBUG)
        fprintf(stderr, "MD4C: %s\n", msg);
}

int
md_html(const MD_CHAR* input, MD_SIZE input_size, MD_HTML_CALLBACKS callbacks,
        void* userdata, unsigned parser_flags, unsigned renderer_flags)
{
    MD_HTML render = { callbacks.process_output, callbacks.render_self_link, callbacks.record_self_link, callbacks.render_code_link, userdata, renderer_flags, 0, { 0 } };
    int i;

    MD_PARSER parser = {
        0,
        parser_flags,
        enter_block_callback,
        leave_block_callback,
        enter_span_callback,
        leave_span_callback,
        text_callback,
        debug_log_callback,
        NULL
    };

    /* Build map of characters which need escaping. */
    for(i = 0; i < 256; i++) {
        unsigned char ch = (unsigned char) i;

        if(strchr("\"&<>", ch) != NULL)
            render.escape_map[i] |= NEED_HTML_ESC_FLAG;

        if(!ISALNUM(ch)  &&  strchr("~-_.+!*(),%#@?=;:/,+$", ch) == NULL)
            render.escape_map[i] |= NEED_URL_ESC_FLAG;
    }

    /* Consider skipping UTF-8 byte order mark (BOM). */
    if(renderer_flags & MD_HTML_FLAG_SKIP_UTF8_BOM  &&  sizeof(MD_CHAR) == 1) {
        static const MD_CHAR bom[3] = { 0xef, 0xbb, 0xbf };
        if(input_size >= sizeof(bom)  &&  memcmp(input, bom, sizeof(bom)) == 0) {
            input += sizeof(bom);
            input_size -= sizeof(bom);
        }
    }

    int ret = md_parse(input, input_size, &parser, (void*) &render);

    return ret;
}

