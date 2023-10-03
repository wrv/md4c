/*
 * MD4C: Markdown parser for C
 * (http://github.com/mity/md4c)
 *
 * Copyright (c) 2016-2017 Martin Mitas
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

#ifndef MD4C_HTML_H
#define MD4C_HTML_H

#include "md4c.h"

#ifdef __cplusplus
    extern "C" {
#endif


/* If set, debug output from md_parse() is sent to stderr. */
#define MD_HTML_FLAG_DEBUG                  0x0001
#define MD_HTML_FLAG_VERBATIM_ENTITIES      0x0002
#define MD_HTML_FLAG_SKIP_UTF8_BOM          0x0004
#define MD_HTML_FLAG_XHTML                  0x0008


typedef struct MD_HTML_tag MD_HTML;
struct MD_HTML_tag;


typedef struct MD_HTML_CALLBACKS_tag MD_HTML_CALLBACKS;
struct MD_HTML_CALLBACKS_tag {
    /*
     * The callback is called with chunks of HTML output.
     *
     * Typical implementation may just output the bytes to a file or append to
     * some buffer.
     *
     * This callback is required.
     */
     void (*process_output)(const MD_CHAR*, MD_SIZE, void*);
    /* The callback receives the text in the self link and can adjust the text to what the
     * anchor name and link should be.
     *
     * This will be called twice, once for the name and once for the href. It should do the
     * same thing both times.
     *
     * If it returns non-0 to report an error, that error will be passed back to the parser and
     * terminate parsing.
     *
     * This callback is optional, and may be NULL.
     */
    int (*render_self_link)(const MD_CHAR* /*chars*/, MD_SIZE /*size*/, void* /*userdata*/, MD_HTML* /*html*/);
    /* Called after render_self_link was called, in order to mutate any state recording the link
     * that was generated, if needed. Allows each link to be unique.
     *
     * If it returns non-0 to report an error, that error will be passed back to the parser and
     * terminate parsing.
     *
     * This callback is optional, and may be NULL.
     */
    int (*record_self_link)(const MD_CHAR* /*chars*/, MD_SIZE /*size*/, void* /*userdata*/);
    /* The callbacks receives the link text for a code link: `$[display](the link text)`. It
     * should turn the link text into a URL.
     *
     * If it returns non-0 to report an error, that error will be passed back to the parser and
     * terminate parsing.
     *
     * This callback is optional, and may be NULL.
     */
    int (*render_code_link)(const MD_CHAR* /*chars*/, MD_SIZE /*size*/, void* /*userdata*/, MD_HTML* /*html*/);
};

/* Render Markdown into HTML.
 *
 * Note only contents of <body> tag is generated. Caller must generate
 * HTML header/footer manually before/after calling md_html().
 *
 * Params input and input_size specify the Markdown input.
 * Callbacks is a set of callbacks to be provided by the application which
 * handle events during parsing and html generation.
 * Param userdata is just propagated back to process_output() callback.
 * Param parser_flags are flags from md4c.h propagated to md_parse().
 * Param render_flags is bitmask of MD_HTML_FLAG_xxxx.
 *
 * Returns -1 on error (if md_parse() fails.)
 * Returns 0 on success.
 */
int md_html(const MD_CHAR* input, MD_SIZE input_size, MD_HTML_CALLBACKS callbacks,
            void* userdata, unsigned parser_flags, unsigned renderer_flags);

int render_url_escaped(MD_HTML* r, const MD_CHAR* data, MD_SIZE size);

#ifdef __cplusplus
    }  /* extern "C" { */
#endif

#endif  /* MD4C_HTML_H */
