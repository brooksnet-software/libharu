/*
 * << Haru Free PDF Library >> -- utf_demo.c
 *
 * Demonstrates how to render UTF-8 encoded text covering a range of scripts
 * (Latin, Cyrillic, Greek, CJK) as well as supplementary-plane characters
 * above U+FFFF such as mathematical alphanumerics and emoji.
 *
 * The recipe is:
 *
 *   1. HPDF_UseUTFEncodings(pdf)            -- register the "UTF-8" encoder
 *   2. name = HPDF_LoadTTFontFromFile(...)  -- load a TrueType font, or
 *      name = HPDF_LoadTTFontFromFile2(...) -- a font from a .ttc collection
 *   3. font = HPDF_GetFont(pdf, name, "UTF-8")
 *
 * After that the UTF-8 bytes you pass to HPDF_Page_ShowText() are decoded
 * and mapped to glyphs via the font's Unicode cmap. Embed the font
 * (embedding = HPDF_TRUE) so the glyphs are available in every viewer.
 *
 * Code points above U+FFFF require a font whose 'cmap' table has a format 12
 * sub-table (most modern fonts do); the glyph and its ToUnicode mapping are
 * embedded automatically.
 *
 * Limitations (by design - libHaru performs no complex text layout):
 *   - No bidirectional reordering and no contextual shaping, so right to
 *     left scripts (Arabic, Hebrew) and Indic scripts will not render
 *     correctly without pre-shaping the text yourself.
 *
 * Copyright (c) 1999-2006 Takeshi Kanno <takeshi_kanno@est.hi-ho.ne.jp>
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.
 * It is provided "as is" without express or implied warranty.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "hpdf.h"

jmp_buf env;

#ifdef HPDF_DLL
void  __stdcall
#else
void
#endif
error_handler (HPDF_STATUS   error_no,
               HPDF_STATUS   detail_no,
               void         *user_data)
{
    (void) user_data; /* Not used */
    printf ("ERROR: error_no=%04X, detail_no=%u\n", (HPDF_UINT)error_no,
                (HPDF_UINT)detail_no);
    longjmp (env, 1);
}

/* A few BMP sample strings, written as raw UTF-8 bytes so this file stays
 * portable regardless of the compiler's source-charset handling. */
typedef struct {
    const char *label;
    const char *text;
} Sample;

static const Sample SAMPLES[] = {
    { "Latin",    "The quick brown fox jumps over the lazy dog" },
    /* "Café naïve résumé" */
    { "Latin-1",  "Caf\xC3\xA9 na\xC3\xAFve r\xC3\xA9sum\xC3\xA9" },
    /* Greek: "Γειά σου Κόσμε" */
    { "Greek",    "\xCE\x93\xCE\xB5\xCE\xB9\xCE\xAC \xCF\x83\xCE\xBF\xCF\x85 "
                  "\xCE\x9A\xCF\x8C\xCF\x83\xCE\xBC\xCE\xB5" },
    /* Cyrillic: "Привет, мир" */
    { "Cyrillic", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82, "
                  "\xD0\xBC\xD0\xB8\xD1\x80" },
    /* CJK: "日本語のテスト" (only renders if the font has these glyphs) */
    { "CJK",      "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x81\xAE"
                  "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88" },
    /* Supplementary plane (above U+FFFF): U+1D400 MATHEMATICAL BOLD CAPITAL A,
     * U+1D419 ...Z, and U+1F600 GRINNING FACE. Needs a font with these
     * glyphs and a format 12 cmap (e.g. Segoe UI Symbol). */
    { "U+1Dxxx",  "\xF0\x9D\x90\x80\xF0\x9D\x90\x81\xF0\x9D\x90\x82" },
    { "U+1F600",  "\xF0\x9F\x98\x80" },
};

int main (int argc, char **argv)
{
    HPDF_Doc    pdf;
    HPDF_Page   page;
    HPDF_Font   label_font;
    HPDF_Font   font;
    const char *font_name;
    char        out_name[256];
    HPDF_UINT   ttc_index = 0;
    HPDF_REAL   y;
    size_t      i;

    if (argc < 2) {
        printf ("usage: utf_demo <font.ttf | font.ttc> [ttc-index]\n");
        printf ("  e.g. utf_demo C:\\Windows\\Fonts\\arial.ttf\n");
        printf ("       utf_demo C:\\Windows\\Fonts\\msgothic.ttc 0\n");
        return 1;
    }
    if (argc > 2)
        ttc_index = (HPDF_UINT) atoi (argv[2]);

    strcpy (out_name, argv[0]);
    strcat (out_name, ".pdf");

    pdf = HPDF_New (error_handler, NULL);
    if (!pdf) {
        printf ("error: cannot create PdfDoc object\n");
        return 1;
    }

    if (setjmp (env)) {
        HPDF_Free (pdf);
        return 1;
    }

    /* 1. Register the UTF-8 encoder. */
    HPDF_UseUTFEncodings (pdf);

    /* 2. Load a TrueType font (embedding it so glyphs travel with the PDF).
     *    Use the plain loader for .ttf and the indexed loader for .ttc. */
    if (strstr (argv[1], ".ttc") || strstr (argv[1], ".TTC"))
        font_name = HPDF_LoadTTFontFromFile2 (pdf, argv[1], ttc_index,
                                              HPDF_TRUE);
    else
        font_name = HPDF_LoadTTFontFromFile (pdf, argv[1], HPDF_TRUE);

    /* 3. Get the font handle bound to the "UTF-8" encoding. */
    font = HPDF_GetFont (pdf, font_name, "UTF-8");

    label_font = HPDF_GetFont (pdf, "Helvetica", NULL);

    page = HPDF_AddPage (pdf);
    HPDF_Page_SetSize (page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);

    HPDF_Page_BeginText (page);
    HPDF_Page_SetFontAndSize (page, label_font, 16);
    HPDF_Page_TextOut (page, 50, 790, "UTF-8 multilingual demo");
    HPDF_Page_SetFontAndSize (page, label_font, 9);
    HPDF_Page_TextOut (page, 50, 775, font_name);

    y = 730;
    for (i = 0; i < sizeof (SAMPLES) / sizeof (SAMPLES[0]); i++) {
        HPDF_REAL w;

        HPDF_Page_SetFontAndSize (page, label_font, 9);
        HPDF_Page_TextOut (page, 50, y, SAMPLES[i].label);

        HPDF_Page_SetFontAndSize (page, font, 18);
        HPDF_Page_TextOut (page, 130, y, SAMPLES[i].text);

        /* A non-zero width confirms the glyphs were resolved by the font. */
        w = HPDF_Page_TextWidth (page, SAMPLES[i].text);
        printf ("%-9s width=%.2f\n", SAMPLES[i].label, w);

        y -= 40;
    }
    HPDF_Page_EndText (page);

    HPDF_SaveToFile (pdf, out_name);
    printf ("wrote %s\n", out_name);

    HPDF_Free (pdf);
    return 0;
}
