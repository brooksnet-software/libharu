/*
 * << Haru Free PDF Library >> -- svg_demo.c
 *
 * Renders an SVG file into a PDF as real, scalable vector graphics (not a
 * rasterized image). The SVG is parsed with nanosvg (a small, zlib-licensed
 * single-header library, bundled here as demo/nanosvg.h), which flattens every
 * shape to cubic Bezier paths. Those map almost 1:1 onto libHaru's path
 * operators (HPDF_Page_CurveTo etc.), so the translation loop is tiny.
 *
 * Covered: paths and basic shapes, solid fills and strokes, even-odd vs
 * nonzero winding (holes/compound paths), stroke width/cap/join/miter.
 * Not covered (SVG features nanosvg does not expand): gradients, opacity,
 * live <text> (convert to paths first), clip paths, filters, patterns,
 * embedded images.
 *
 * usage: svg_demo <input.svg> [width-inches]   ->  writes svg_demo.pdf
 *        (width defaults to 3 inches; height scales proportionally)
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
#include <math.h>
#include "hpdf.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

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

/* nanosvg packs colors as 0xAABBGGRR; extract 0..1 RGB. */
static void
nsvg_rgb (unsigned int c, HPDF_REAL *r, HPDF_REAL *g, HPDF_REAL *b)
{
    *r = (HPDF_REAL)(( c        & 0xff) / 255.0);
    *g = (HPDF_REAL)(((c >> 8)  & 0xff) / 255.0);
    *b = (HPDF_REAL)(((c >> 16) & 0xff) / 255.0);
}

/* Draw a parsed SVG onto `page`. SVG user units map to PDF points at `scale`,
 * with the SVG top-left placed at PDF coordinate (ox, oy_top), measured from
 * the page bottom (i.e. the usual top edge). */
static void
DrawSvg (HPDF_Page  page,
         NSVGimage *svg,
         HPDF_REAL  ox,
         HPDF_REAL  oy_top,
         HPDF_REAL  scale)
{
    NSVGshape *shape;
    NSVGpath  *path;

    HPDF_Page_GSave (page);

    /* One transform handles placement, scaling and the Y flip (SVG y grows
     * down, PDF y grows up): SVG (x,y) -> PDF (ox + scale*x, oy_top - scale*y). */
    HPDF_Page_Concat (page, scale, 0, 0, -scale, ox, oy_top);

    for (shape = svg->shapes; shape; shape = shape->next) {
        int has_fill, has_stroke, eo;
        HPDF_REAL r, g, b;

        if (!(shape->flags & NSVG_FLAGS_VISIBLE))
            continue;

        has_fill   = (shape->fill.type   == NSVG_PAINT_COLOR);
        has_stroke = (shape->stroke.type == NSVG_PAINT_COLOR);
        if (!has_fill && !has_stroke)
            continue;   /* gradients / none: not handled by this demo */

        eo = (shape->fillRule == NSVG_FILLRULE_EVENODD);

        if (has_fill) {
            nsvg_rgb (shape->fill.color, &r, &g, &b);
            HPDF_Page_SetRGBFill (page, r, g, b);
        }
        if (has_stroke) {
            nsvg_rgb (shape->stroke.color, &r, &g, &b);
            HPDF_Page_SetRGBStroke (page, r, g, b);
            HPDF_Page_SetLineWidth (page, shape->strokeWidth);
            HPDF_Page_SetLineCap (page, (HPDF_LineCap) shape->strokeLineCap);
            HPDF_Page_SetLineJoin (page, (HPDF_LineJoin) shape->strokeLineJoin);
            HPDF_Page_SetMiterLimit (page, shape->miterLimit);
        }

        /* Emit every subpath of the shape, then paint once, so holes and
         * compound paths fill with the correct winding rule. */
        for (path = shape->paths; path; path = path->next) {
            float *p = path->pts;     /* x0,y0, c1x,c1y,c2x,c2y,x1,y1, ... */
            int i;

            HPDF_Page_MoveTo (page, p[0], p[1]);
            for (i = 0; i < path->npts - 1; i += 3) {
                float *c = &p[i * 2]; /* c[0..1]=current; c[2..7]=ctrl1,ctrl2,end */
                HPDF_Page_CurveTo (page, c[2], c[3], c[4], c[5], c[6], c[7]);
            }
            if (path->closed)
                HPDF_Page_ClosePath (page);
        }

        if (has_fill && has_stroke)
            (eo ? HPDF_Page_EofillStroke : HPDF_Page_FillStroke) (page);
        else if (has_fill)
            (eo ? HPDF_Page_Eofill : HPDF_Page_Fill) (page);
        else
            HPDF_Page_Stroke (page);
    }

    HPDF_Page_GRestore (page);
}

int
main (int argc, char **argv)
{
    HPDF_Doc   pdf;
    HPDF_Page  page;
    NSVGimage *svg;
    HPDF_REAL  width_in = 3.0f;   /* target output width in inches */
    HPDF_REAL  scale;

    if (argc < 2) {
        printf ("usage: svg_demo <input.svg> [width-inches]\n");
        return 1;
    }
    if (argc > 2)
        width_in = (HPDF_REAL) atof (argv[2]);

    svg = nsvgParseFromFile (argv[1], "px", 96.0f);
    if (!svg || svg->width <= 0 || svg->height <= 0) {
        printf ("error: could not parse SVG '%s'\n", argv[1]);
        if (svg) nsvgDelete (svg);
        return 1;
    }
    printf ("parsed %s: %.1f x %.1f, shapes:", argv[1], svg->width, svg->height);
    {
        int n = 0;
        NSVGshape *s;
        for (s = svg->shapes; s; s = s->next) n++;
        printf (" %d\n", n);
    }

    pdf = HPDF_New (error_handler, NULL);
    if (!pdf) {
        printf ("error: cannot create PdfDoc object\n");
        nsvgDelete (svg);
        return 1;
    }

    if (setjmp (env)) {
        HPDF_Free (pdf);
        nsvgDelete (svg);
        return 1;
    }

    /* Scale so the SVG's width maps to the requested inches (72 pt/inch);
     * the height follows proportionally. */
    scale = (width_in * 72.0f) / svg->width;

    page = HPDF_AddPage (pdf);
    HPDF_Page_SetSize (page, HPDF_PAGE_SIZE_LETTER, HPDF_PAGE_PORTRAIT);

    /* Offset the art 1 inch in from the left and 1 inch down from the top.
     * DrawSvg's oy_top is measured from the page bottom, so "1 inch below the
     * top" is page height minus 1 inch. */
    {
        HPDF_REAL margin = 72.0f;   /* 1 inch */
        HPDF_REAL ox     = margin;
        HPDF_REAL oy_top = HPDF_Page_GetHeight (page) - margin;

        DrawSvg (page, svg, ox, oy_top, scale);
    }

    HPDF_SaveToFile (pdf, "svg_demo.pdf");
    printf ("wrote svg_demo.pdf (Letter; art %.2f x %.2f in at 1in,1in)\n",
            (double)(svg->width * scale / 72.0),
            (double)(svg->height * scale / 72.0));

    HPDF_Free (pdf);
    nsvgDelete (svg);
    return 0;
}
