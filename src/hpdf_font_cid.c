/*
 * << Haru Free PDF Library >> -- hpdf_font_cid.c
 *
 * URL: http://libharu.org
 *
 * Copyright (c) 1999-2006 Takeshi Kanno <takeshi_kanno@est.hi-ho.ne.jp>
 * Copyright (c) 2007-2009 Antony Dovgal <tony@daylessday.org>
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.
 * It is provided "as is" without express or implied warranty.
 *
 */

#include "hpdf_conf.h"
#include "hpdf_utils.h"
#include "hpdf_font.h"

static HPDF_Font
CIDFontType0_New (HPDF_Font parent,
                  HPDF_Xref xref);


static HPDF_Font
CIDFontType2_New (HPDF_Font parent,
                  HPDF_Xref xref);


static HPDF_TextWidth
TextWidth  (HPDF_Font         font,
            const HPDF_BYTE  *text,
            HPDF_UINT         len);


static HPDF_UINT
MeasureText  (HPDF_Font         font,
              const HPDF_BYTE  *text,
              HPDF_UINT         len,
              HPDF_REAL         width,
              HPDF_REAL         font_size,
              HPDF_REAL         char_space,
              HPDF_REAL         word_space,
              HPDF_BOOL         wordwrap,
              HPDF_REAL        *real_width);


static char*
UINT16ToHex  (char        *s,
              HPDF_UINT16  val,
              char        *eptr,
              HPDF_BYTE    width);

static char *
CidRangeToHex (char        *s,
	       HPDF_UINT16  from,
	       HPDF_UINT16  to,
	       char        *eptr);

static HPDF_Dict
CreateCMap  (HPDF_Encoder   encoder,
             HPDF_Xref      xref);


static void
OnFree_Func  (HPDF_Dict  obj);


static HPDF_STATUS
CIDFontType2_BeforeWrite_Func  (HPDF_Dict   obj);


/*--------------------------------------------------------------------------*/

HPDF_Font
HPDF_Type0Font_New  (HPDF_MMgr        mmgr,
                     HPDF_FontDef     fontdef,
                     HPDF_Encoder     encoder,
                     HPDF_Xref        xref)
{
    HPDF_Dict font;
    HPDF_FontAttr attr;
    HPDF_CMapEncoderAttr encoder_attr;
    HPDF_STATUS ret = 0;
    HPDF_Array descendant_fonts;

    HPDF_PTRACE ((" HPDF_Type0Font_New\n"));

    font = HPDF_Dict_New (mmgr);
    if (!font)
        return NULL;

    font->header.obj_class |= HPDF_OSUBCLASS_FONT;

    /* check whether the fontdef object and the encoder object is valid. */
    if (encoder->type != HPDF_ENCODER_TYPE_DOUBLE_BYTE) {
        HPDF_SetError(font->error, HPDF_INVALID_ENCODER_TYPE, 0);
        return NULL;
    }

    if (fontdef->type != HPDF_FONTDEF_TYPE_CID &&
        fontdef->type != HPDF_FONTDEF_TYPE_TRUETYPE) {
        HPDF_SetError(font->error, HPDF_INVALID_FONTDEF_TYPE, 0);
        return NULL;
    }

    attr = HPDF_GetMem (mmgr, sizeof(HPDF_FontAttr_Rec));
    if (!attr) {
        HPDF_Dict_Free (font);
        return NULL;
    }

    font->header.obj_class |= HPDF_OSUBCLASS_FONT;
    font->write_fn = NULL;
    font->free_fn = OnFree_Func;
    font->attr = attr;

    encoder_attr = (HPDF_CMapEncoderAttr)encoder->attr;

    HPDF_MemSet (attr, 0, sizeof(HPDF_FontAttr_Rec));

    attr->writing_mode = encoder_attr->writing_mode;
    attr->text_width_fn = TextWidth;
    attr->measure_text_fn = MeasureText;
    attr->fontdef = fontdef;
    attr->encoder = encoder;
    attr->xref = xref;

    if (HPDF_Xref_Add (xref, font) != HPDF_OK)
        return NULL;

    ret += HPDF_Dict_AddName (font, "Type", "Font");
    ret += HPDF_Dict_AddName (font, "BaseFont", fontdef->base_font);
    ret += HPDF_Dict_AddName (font, "Subtype", "Type0");

    if (fontdef->type == HPDF_FONTDEF_TYPE_CID) {
        ret += HPDF_Dict_AddName (font, "Encoding", encoder->name);
    } else {
        /*
	 * Handle the Unicode encoding, see hpdf_encoding_utf.c For some
	 * reason, xpdf-based readers cannot deal with our cmap but work
	 * fine when using the predefined "Identity-H"
	 * encoding. However, text selection does not work, unless we
	 * add a ToUnicode cmap. This CMap should also be "Identity",
	 * but that does not work -- specifying our cmap as a stream however
	 * does work. Who can understand that ?
	 */
        if (HPDF_StrCmp(encoder_attr->ordering, "Identity-H") == 0) {
	    ret += HPDF_Dict_AddName (font, "Encoding", "Identity-H");

	    if (encoder->encode_text_fn != NULL) {
		/* UTF-8 encoder: use the CID == GID scheme so that
		 * supplementary-plane characters can be addressed. The
		 * ToUnicode CMap is filled in at write time from the glyphs
		 * actually used (see CIDFontType2_BeforeWrite_Func). */
		attr->is_unicode_gid = HPDF_TRUE;
		attr->cmap_stream = HPDF_DictStream_New (mmgr, xref);
	    } else {
		attr->cmap_stream = CreateCMap (encoder, xref);
	    }

	    if (attr->cmap_stream) {
	        ret += HPDF_Dict_Add (font, "ToUnicode", attr->cmap_stream);
	    } else
	        return NULL;
	} else {
            attr->cmap_stream = CreateCMap (encoder, xref);

	    if (attr->cmap_stream) {
	        ret += HPDF_Dict_Add (font, "Encoding", attr->cmap_stream);
	    } else
	      return NULL;
	}
    }

    if (ret != HPDF_OK)
        return NULL;

    descendant_fonts = HPDF_Array_New (mmgr);
    if (!descendant_fonts)
        return NULL;

    if (HPDF_Dict_Add (font, "DescendantFonts", descendant_fonts) != HPDF_OK)
        return NULL;

    if (fontdef->type == HPDF_FONTDEF_TYPE_CID) {
        attr->descendant_font = CIDFontType0_New (font, xref);
        attr->type = HPDF_FONT_TYPE0_CID;
    } else {
        attr->descendant_font = CIDFontType2_New (font, xref);
        attr->type = HPDF_FONT_TYPE0_TT;
    }

    if (!attr->descendant_font)
        return NULL;
    else
        if (HPDF_Array_Add (descendant_fonts, attr->descendant_font) !=
                HPDF_OK)
            return NULL;

    return font;
}

static void
OnFree_Func  (HPDF_Dict  obj)
{
    HPDF_FontAttr attr = (HPDF_FontAttr)obj->attr;

    HPDF_PTRACE ((" HPDF_Type0Font_OnFree\n"));

    if (attr) {
        if (attr->gid_to_unicode)
            HPDF_FreeMem (obj->mmgr, attr->gid_to_unicode);

        HPDF_FreeMem (obj->mmgr, attr);
    }
}

static HPDF_Font
CIDFontType0_New (HPDF_Font parent, HPDF_Xref xref)
{
    HPDF_STATUS ret = HPDF_OK;
    HPDF_FontAttr attr = (HPDF_FontAttr)parent->attr;
    HPDF_FontDef fontdef = attr->fontdef;
    HPDF_CIDFontDefAttr fontdef_attr = (HPDF_CIDFontDefAttr)fontdef->attr;
    HPDF_Encoder encoder = attr->encoder;
    HPDF_CMapEncoderAttr encoder_attr =
                (HPDF_CMapEncoderAttr)encoder->attr;

    HPDF_UINT16 save_cid = 0;
    HPDF_Font font;
    HPDF_Array array;
    HPDF_Array sub_array = NULL;
    HPDF_UINT i;

    HPDF_Dict descriptor;
    HPDF_Dict cid_system_info;

    HPDF_PTRACE ((" HPDF_CIDFontType0_New\n"));

    font = HPDF_Dict_New (parent->mmgr);
    if (!font)
        return NULL;

    if (HPDF_Xref_Add (xref, font) != HPDF_OK)
        return NULL;

    ret += HPDF_Dict_AddName (font, "Type", "Font");
    ret += HPDF_Dict_AddName (font, "Subtype", "CIDFontType0");
    ret += HPDF_Dict_AddNumber (font, "DW", fontdef_attr->DW);
    ret += HPDF_Dict_AddName (font, "BaseFont", fontdef->base_font);
    if (ret != HPDF_OK)
        return NULL;

    /* add 'DW2' element */
    array = HPDF_Array_New (parent->mmgr);
    if (!array)
        return NULL;

    if (HPDF_Dict_Add (font, "DW2", array) != HPDF_OK)
        return NULL;

    ret += HPDF_Array_AddNumber (array, fontdef_attr->DW2[0]);
    ret += HPDF_Array_AddNumber (array, fontdef_attr->DW2[1]);

    if (ret != HPDF_OK)
        return NULL;

    /* add 'W' element */
    array = HPDF_Array_New (parent->mmgr);
    if (!array)
        return NULL;

    if (HPDF_Dict_Add (font, "W", array) != HPDF_OK)
        return NULL;

    /* Create W array. */
    for (i = 0; i< fontdef_attr->widths->count; i++) {
        HPDF_CID_Width *w =
                (HPDF_CID_Width *)HPDF_List_ItemAt (fontdef_attr->widths, i);

        if (w->cid != save_cid + 1 || !sub_array) {
            sub_array = HPDF_Array_New (parent->mmgr);
            if (!sub_array)
                return NULL;

            ret += HPDF_Array_AddNumber (array, w->cid);
            ret += HPDF_Array_Add (array, sub_array);
        }

        ret += HPDF_Array_AddNumber (sub_array, w->width);
        save_cid = w->cid;

        if (ret != HPDF_OK)
            return NULL;
    }

    /* create descriptor */
    descriptor = HPDF_Dict_New (parent->mmgr);
    if (!descriptor)
        return NULL;

    if (HPDF_Xref_Add (xref, descriptor) != HPDF_OK)
        return NULL;

    if (HPDF_Dict_Add (font, "FontDescriptor", descriptor) != HPDF_OK)
        return NULL;

    ret += HPDF_Dict_AddName (descriptor, "Type", "FontDescriptor");
    ret += HPDF_Dict_AddName (descriptor, "FontName", fontdef->base_font);
    ret += HPDF_Dict_AddNumber (descriptor, "Ascent", fontdef->ascent);
    ret += HPDF_Dict_AddNumber (descriptor, "Descent", fontdef->descent);
    ret += HPDF_Dict_AddNumber (descriptor, "CapHeight",
                fontdef->cap_height);
    ret += HPDF_Dict_AddNumber (descriptor, "MissingWidth",
                fontdef->missing_width);
    ret += HPDF_Dict_AddNumber (descriptor, "Flags", fontdef->flags);

    if (ret != HPDF_OK)
        return NULL;

    array = HPDF_Box_Array_New (parent->mmgr, fontdef->font_bbox);
    if (!array)
        return NULL;

    ret += HPDF_Dict_Add (descriptor, "FontBBox", array);
    ret += HPDF_Dict_AddNumber (descriptor, "ItalicAngle",
            fontdef->italic_angle);
    ret += HPDF_Dict_AddNumber (descriptor, "StemV", fontdef->stemv);

    if (ret != HPDF_OK)
        return NULL;

    /* create CIDSystemInfo dictionary */
    cid_system_info = HPDF_Dict_New (parent->mmgr);
    if (!cid_system_info)
        return NULL;

    if (HPDF_Dict_Add (font, "CIDSystemInfo", cid_system_info) != HPDF_OK)
        return NULL;

    ret += HPDF_Dict_Add (cid_system_info, "Registry",
            HPDF_String_New (parent->mmgr, encoder_attr->registry, NULL));
    ret += HPDF_Dict_Add (cid_system_info, "Ordering",
            HPDF_String_New (parent->mmgr, encoder_attr->ordering, NULL));
    ret += HPDF_Dict_AddNumber (cid_system_info, "Supplement",
            encoder_attr->suppliment);

    if (ret != HPDF_OK)
        return NULL;

    return font;
}

static HPDF_Font
CIDFontType2_New (HPDF_Font parent, HPDF_Xref xref)
{
    HPDF_STATUS ret = HPDF_OK;
    HPDF_FontAttr attr = (HPDF_FontAttr)parent->attr;
    HPDF_FontDef fontdef = attr->fontdef;
    HPDF_TTFontDefAttr fontdef_attr = (HPDF_TTFontDefAttr)fontdef->attr;
    HPDF_Encoder encoder = attr->encoder;
    HPDF_CMapEncoderAttr encoder_attr =
                (HPDF_CMapEncoderAttr)encoder->attr;

    HPDF_Font font;
    HPDF_Array array;
    HPDF_UINT i;
    HPDF_UINT16 *tmp_map = NULL;
    HPDF_Dict cid_system_info;

    HPDF_UINT16 max = 0;

    HPDF_PTRACE ((" HPDF_CIDFontType2_New\n"));

    font = HPDF_Dict_New (parent->mmgr);
    if (!font)
        return NULL;

    if (HPDF_Xref_Add (xref, font) != HPDF_OK)
        return NULL;

    parent->before_write_fn = CIDFontType2_BeforeWrite_Func;

    ret += HPDF_Dict_AddName (font, "Type", "Font");
    ret += HPDF_Dict_AddName (font, "Subtype", "CIDFontType2");
    ret += HPDF_Dict_AddNumber (font, "DW", fontdef->missing_width);
    if (ret != HPDF_OK)
        return NULL;

    /* add 'DW2' element */
    array = HPDF_Array_New (font->mmgr);
    if (!array)
        return NULL;

    if (HPDF_Dict_Add (font, "DW2", array) != HPDF_OK)
        return NULL;

    ret += HPDF_Array_AddNumber (array, (HPDF_INT32)(fontdef->font_bbox.bottom));
    ret += HPDF_Array_AddNumber (array, (HPDF_INT32)(fontdef->font_bbox.bottom -
                fontdef->font_bbox.top));

    if (ret != HPDF_OK)
        return NULL;

    if (attr->is_unicode_gid) {
        /* UTF-8 path: CID == GID. The CIDToGIDMap is therefore the identity
         * mapping, and the W array and ToUnicode CMap are generated at write
         * time (CIDFontType2_BeforeWrite_Func) from the glyphs actually used,
         * so that supplementary-plane glyphs (CID/GID > BMP code points) are
         * handled without a 65536-entry table. */
        if ((ret = HPDF_Dict_AddName (font, "CIDToGIDMap", "Identity"))
                != HPDF_OK)
            return NULL;

        /* create CIDSystemInfo dictionary */
        cid_system_info = HPDF_Dict_New (parent->mmgr);
        if (!cid_system_info)
            return NULL;

        if (HPDF_Dict_Add (font, "CIDSystemInfo", cid_system_info) != HPDF_OK)
            return NULL;

        ret += HPDF_Dict_Add (cid_system_info, "Registry",
                HPDF_String_New (parent->mmgr, encoder_attr->registry, NULL));
        ret += HPDF_Dict_Add (cid_system_info, "Ordering",
                HPDF_String_New (parent->mmgr, encoder_attr->ordering, NULL));
        ret += HPDF_Dict_AddNumber (cid_system_info, "Supplement",
                encoder_attr->suppliment);

        if (ret != HPDF_OK)
            return NULL;

        return font;
    }

    tmp_map = HPDF_GetMem (font->mmgr, sizeof(HPDF_UINT16) * 65536);
    if (!tmp_map)
        return NULL;

    HPDF_MemSet (tmp_map, 0, sizeof(HPDF_UINT16) * 65536);

    for (i = 0; i < 256; i++) {
        HPDF_UINT j;

        for (j = 0; j < 256; j++) {
	    if (encoder->to_unicode_fn == HPDF_CMapEncoder_ToUnicode) {
		HPDF_UINT16 cid = encoder_attr->cid_map[i][j];
		if (cid != 0) {
		    HPDF_UNICODE unicode = encoder_attr->unicode_map[i][j];
		    HPDF_UINT16 gid = HPDF_TTFontDef_GetGlyphid (fontdef,
								 unicode);
		    tmp_map[cid] = gid;
		    if (max < cid)
			max = cid;
		}
	    } else {
		HPDF_UNICODE unicode = (i << 8) | j;
		HPDF_UINT16 gid = HPDF_TTFontDef_GetGlyphid (fontdef,
							     unicode);
		tmp_map[unicode] = gid;
		if (max < unicode)
		    max = unicode;
	    }
	}
    }

    if (max > 0) {
        HPDF_INT16 dw = fontdef->missing_width;
        HPDF_UINT16 *ptmp_map = tmp_map;
        HPDF_Array tmp_array = NULL;

        /* add 'W' element */
        array = HPDF_Array_New (font->mmgr);
        if (!array) {
            HPDF_FreeMem (font->mmgr, tmp_map);
            return NULL;
        }

        if (HPDF_Dict_Add (font, "W", array) != HPDF_OK) {
            HPDF_FreeMem (font->mmgr, tmp_map);
            return NULL;
        }

        for (i = 0; i < max; i++, ptmp_map++) {
            HPDF_INT w = HPDF_TTFontDef_GetGidWidth (fontdef, *ptmp_map);

            if (w != dw) {
                if (!tmp_array) {
                    if (HPDF_Array_AddNumber (array, i) != HPDF_OK) {
                        HPDF_FreeMem (font->mmgr, tmp_map);
                        return NULL;
                    }

                    tmp_array = HPDF_Array_New (font->mmgr);
                    if (!tmp_array) {
                        HPDF_FreeMem (font->mmgr, tmp_map);
                        return NULL;
                    }

                    if (HPDF_Array_Add (array, tmp_array) != HPDF_OK) {
                        HPDF_FreeMem (font->mmgr, tmp_map);
                        return NULL;
                    }
                }

                if ((ret = HPDF_Array_AddNumber (tmp_array, w)) != HPDF_OK) {
                    HPDF_FreeMem (font->mmgr, tmp_map);
                    return NULL;
                }
            } else
                  tmp_array = NULL;
        }

        /* create "CIDToGIDMap" data */
        if (fontdef_attr->embedding) {
            HPDF_BYTE *gidmap;

            attr->map_stream = HPDF_DictStream_New (font->mmgr, xref);
            if (!attr->map_stream) {
                HPDF_FreeMem (font->mmgr, tmp_map);
                return NULL;
            }

            if (HPDF_Dict_Add (font, "CIDToGIDMap", attr->map_stream) != HPDF_OK) {
                HPDF_FreeMem (font->mmgr, tmp_map);
                return NULL;
            }

            gidmap = HPDF_GetMem (font->mmgr, (HPDF_UINT)max * 2);
            if (!gidmap) {
                HPDF_FreeMem (font->mmgr, tmp_map);
                return NULL;
            }

            for (i = 0; i < max; i++) {
                gidmap[i * 2]     = (HPDF_BYTE)(tmp_map[i] >> 8);
                gidmap[i * 2 + 1] = (HPDF_BYTE)tmp_map[i];
            }

            ret = HPDF_Stream_Write (attr->map_stream->stream, gidmap,
                            (HPDF_UINT)max * 2);
            HPDF_FreeMem (font->mmgr, gidmap);

            if (ret != HPDF_OK) {
                HPDF_FreeMem (font->mmgr, tmp_map);
                return NULL;
            }
        }
    } else {
        HPDF_FreeMem (font->mmgr, tmp_map);
        HPDF_SetError (font->error, HPDF_INVALID_FONTDEF_DATA, 0);
        return 0;
    }

    HPDF_FreeMem (font->mmgr, tmp_map);

    /* create CIDSystemInfo dictionary */
    cid_system_info = HPDF_Dict_New (parent->mmgr);
    if (!cid_system_info)
        return NULL;

    if (HPDF_Dict_Add (font, "CIDSystemInfo", cid_system_info) != HPDF_OK)
        return NULL;

    ret += HPDF_Dict_Add (cid_system_info, "Registry",
            HPDF_String_New (parent->mmgr, encoder_attr->registry, NULL));
    ret += HPDF_Dict_Add (cid_system_info, "Ordering",
            HPDF_String_New (parent->mmgr, encoder_attr->ordering, NULL));
    ret += HPDF_Dict_AddNumber (cid_system_info, "Supplement",
            encoder_attr->suppliment);

    if (ret != HPDF_OK)
        return NULL;

    return font;
}


/*
 * Encode UTF-8 text for the CID == GID scheme: each code point is mapped to
 * its glyph id via the font's cmap and written to the content stream as a
 * 2-byte (big-endian) code. The (gid, code point) pairing is recorded so a
 * ToUnicode CMap can be produced later. This is what lets supplementary-plane
 * characters be drawn -- a glyph id always fits in 16 bits even when its code
 * point does not.
 */
HPDF_STATUS
HPDF_Type0Font_WriteText  (HPDF_Font     font,
                           const char   *text,
                           HPDF_UINT     len,
                           HPDF_Stream   stream)
{
    HPDF_FontAttr attr = (HPDF_FontAttr)font->attr;
    HPDF_Encoder encoder = attr->encoder;
    HPDF_FontDef fontdef = attr->fontdef;
    HPDF_TTFontDefAttr def_attr = (HPDF_TTFontDefAttr)fontdef->attr;
    HPDF_ParseText_Rec parse_state;
    HPDF_BYTE buf[HPDF_TEXT_DEFAULT_LEN];
    HPDF_UINT buf_len = 0;
    HPDF_UINT i;
    HPDF_STATUS ret;

    HPDF_PTRACE ((" HPDF_Type0Font_WriteText\n"));

    if (!attr->gid_to_unicode) {
        attr->gid_to_unicode = HPDF_GetMem (font->mmgr,
                sizeof(HPDF_UNICODE) * def_attr->num_glyphs);
        if (!attr->gid_to_unicode)
            return HPDF_Error_GetCode (font->error);
        HPDF_MemSet (attr->gid_to_unicode, 0,
                sizeof(HPDF_UNICODE) * def_attr->num_glyphs);
    }

    HPDF_Encoder_SetParseText (encoder, &parse_state, (const HPDF_BYTE *)text,
            len);

    for (i = 0; i < len; i++) {
        HPDF_ByteType btype = HPDF_Encoder_ByteType (encoder, &parse_state);

        if (btype != HPDF_BYTE_TYPE_TRAIL) {
            HPDF_UNICODE cp = HPDF_Encoder_ToUnicode (encoder, 0);
            HPDF_UINT16 gid = HPDF_TTFontDef_GetGlyphid (fontdef, cp);

            /* mark the glyph (and any composite components) as used so that
             * the embedded subset includes it */
            HPDF_TTFontDef_GetCharWidth (fontdef, cp);

            if (gid != 0 && gid < def_attr->num_glyphs)
                attr->gid_to_unicode[gid] = cp;

            if (buf_len + 2 > HPDF_TEXT_DEFAULT_LEN) {
                if ((ret = HPDF_Stream_WriteBinary (stream, buf, buf_len, NULL))
                        != HPDF_OK)
                    return ret;
                buf_len = 0;
            }

            buf[buf_len++] = (HPDF_BYTE)(gid >> 8);
            buf[buf_len++] = (HPDF_BYTE)gid;
        }
    }

    if (buf_len > 0) {
        if ((ret = HPDF_Stream_WriteBinary (stream, buf, buf_len, NULL))
                != HPDF_OK)
            return ret;
    }

    return HPDF_OK;
}


static char*
AppendHex16  (char *p, HPDF_UINT16 v)
{
    static const char H[] = "0123456789ABCDEF";

    *p++ = H[(v >> 12) & 0xF];
    *p++ = H[(v >> 8) & 0xF];
    *p++ = H[(v >> 4) & 0xF];
    *p++ = H[v & 0xF];

    return p;
}


/* Build the 'W' (per-glyph width) array for the descendant CIDFont, keyed by
 * glyph id, covering only the glyphs actually used. */
static HPDF_STATUS
CreateGidWidths  (HPDF_Font      descendant,
                  HPDF_FontDef   fontdef,
                  HPDF_BYTE     *flgs,
                  HPDF_UINT      num_glyphs)
{
    HPDF_Array array;
    HPDF_Array sub_array = NULL;
    HPDF_INT16 dw = fontdef->missing_width;
    HPDF_UINT i;
    HPDF_STATUS ret = 0;

    array = HPDF_Array_New (descendant->mmgr);
    if (!array)
        return HPDF_Error_GetCode (descendant->error);

    if ((ret = HPDF_Dict_Add (descendant, "W", array)) != HPDF_OK)
        return ret;

    for (i = 0; i < num_glyphs; i++) {
        if (flgs[i]) {
            HPDF_INT16 w = HPDF_TTFontDef_GetGidWidth (fontdef, (HPDF_UINT16)i);

            if (w != dw) {
                if (!sub_array) {
                    ret += HPDF_Array_AddNumber (array, i);
                    sub_array = HPDF_Array_New (descendant->mmgr);
                    if (!sub_array)
                        return HPDF_Error_GetCode (descendant->error);
                    ret += HPDF_Array_Add (array, sub_array);
                }
                ret += HPDF_Array_AddNumber (sub_array, w);
            } else
                sub_array = NULL;
        } else
            sub_array = NULL;
    }

    return ret;
}


/* Build a ToUnicode CMap that maps each used glyph id (== CID) back to its
 * Unicode code point, emitting surrogate pairs for code points above U+FFFF
 * so that text extraction and search work for the whole Unicode range. */
static HPDF_STATUS
CreateToUnicodeCMap  (HPDF_Stream    stream,
                      HPDF_UNICODE  *gid_to_unicode,
                      HPDF_BYTE     *flgs,
                      HPDF_UINT      num_glyphs)
{
    HPDF_STATUS ret = 0;
    HPDF_UINT i;
    HPDF_UINT n_used = 0;
    HPDF_UINT block;
    char line[64];

    ret += HPDF_Stream_WriteStr (stream,
            "/CIDInit /ProcSet findresource begin\r\n"
            "12 dict begin\r\n"
            "begincmap\r\n"
            "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) "
            "/Supplement 0 >> def\r\n"
            "/CMapName /Adobe-Identity-UCS def\r\n"
            "/CMapType 2 def\r\n"
            "1 begincodespacerange\r\n<0000> <FFFF>\r\nendcodespacerange\r\n");

    if (gid_to_unicode) {
        for (i = 0; i < num_glyphs; i++)
            if (flgs[i] && gid_to_unicode[i] != 0)
                n_used++;
    }

    /* beginbfchar blocks are limited to 100 entries each */
    i = 0;
    block = 0;
    while (gid_to_unicode && block < n_used) {
        HPDF_UINT count = n_used - block;
        HPDF_UINT k;

        if (count > 100)
            count = 100;

        HPDF_IToA (line, count, line + sizeof(line) - 1);
        ret += HPDF_Stream_WriteStr (stream, line);
        ret += HPDF_Stream_WriteStr (stream, " beginbfchar\r\n");

        for (k = 0; k < count; ) {
            while (i < num_glyphs &&
                    !(flgs[i] && gid_to_unicode[i] != 0))
                i++;

            if (i >= num_glyphs)
                break;

            {
                HPDF_UNICODE cp = gid_to_unicode[i];
                char *p = line;

                *p++ = '<';
                p = AppendHex16 (p, (HPDF_UINT16)i);
                *p++ = '>';
                *p++ = ' ';
                *p++ = '<';

                if (cp > 0xFFFF) {
                    HPDF_UINT32 u = cp - 0x10000;
                    p = AppendHex16 (p, (HPDF_UINT16)(0xD800 + (u >> 10)));
                    p = AppendHex16 (p, (HPDF_UINT16)(0xDC00 + (u & 0x3FF)));
                } else {
                    p = AppendHex16 (p, (HPDF_UINT16)cp);
                }

                *p++ = '>';
                *p++ = '\r';
                *p++ = '\n';
                *p = 0;

                ret += HPDF_Stream_WriteStr (stream, line);
            }

            i++;
            k++;
        }

        ret += HPDF_Stream_WriteStr (stream, "endbfchar\r\n");
        block += count;
    }

    ret += HPDF_Stream_WriteStr (stream,
            "endcmap\r\n"
            "CMapName currentdict /CMap defineresource pop\r\n"
            "end\r\nend\r\n");

    return ret;
}


static HPDF_STATUS
CIDFontType2_BeforeWrite_Func  (HPDF_Dict obj)
{
    HPDF_FontAttr font_attr = (HPDF_FontAttr)obj->attr;
    HPDF_FontDef def = font_attr->fontdef;
    HPDF_TTFontDefAttr def_attr = (HPDF_TTFontDefAttr)def->attr;
    HPDF_STATUS ret = 0;

    HPDF_PTRACE ((" CIDFontType2_BeforeWrite_Func\n"));

    if (font_attr->map_stream)
        font_attr->map_stream->filter = obj->filter;

    if (font_attr->cmap_stream)
        font_attr->cmap_stream->filter = obj->filter;

    /* For the CID == GID (UTF-8) scheme, the per-glyph width array and the
     * ToUnicode CMap are generated here, once all text has been drawn and the
     * set of used glyphs is known. */
    if (font_attr->is_unicode_gid && !font_attr->unicode_meta_written) {
        if ((ret = CreateGidWidths (font_attr->descendant_font, def,
                def_attr->glyph_tbl.flgs, def_attr->num_glyphs)) != HPDF_OK)
            return ret;

        if (font_attr->cmap_stream) {
            if ((ret = CreateToUnicodeCMap (font_attr->cmap_stream->stream,
                    font_attr->gid_to_unicode, def_attr->glyph_tbl.flgs,
                    def_attr->num_glyphs)) != HPDF_OK)
                return ret;
        }

        font_attr->unicode_meta_written = HPDF_TRUE;
    }

    if (!font_attr->fontdef->descriptor) {
        HPDF_Dict descriptor = HPDF_Dict_New (obj->mmgr);
        HPDF_Array array;

        if (!descriptor)
            return HPDF_Error_GetCode (obj->error);

        if (def_attr->embedding) {
            HPDF_Dict font_data = HPDF_DictStream_New (obj->mmgr,
                    font_attr->xref);

            if (!font_data)
                return HPDF_Error_GetCode (obj->error);

            if (HPDF_TTFontDef_SaveFontData (font_attr->fontdef,
                font_data->stream) != HPDF_OK)
                return HPDF_Error_GetCode (obj->error);

            ret += HPDF_Dict_Add (descriptor, "FontFile2", font_data);
            ret += HPDF_Dict_AddNumber (font_data, "Length1",
                    def_attr->length1);
            ret += HPDF_Dict_AddNumber (font_data, "Length2", 0);
            ret += HPDF_Dict_AddNumber (font_data, "Length3", 0);

            font_data->filter = obj->filter;

            if (ret != HPDF_OK)
                return HPDF_Error_GetCode (obj->error);
        }

        ret += HPDF_Xref_Add (font_attr->xref, descriptor);
        ret += HPDF_Dict_AddName (descriptor, "Type", "FontDescriptor");
        ret += HPDF_Dict_AddNumber (descriptor, "Ascent", def->ascent);
        ret += HPDF_Dict_AddNumber (descriptor, "Descent", def->descent);
        ret += HPDF_Dict_AddNumber (descriptor, "CapHeight", def->cap_height);
        ret += HPDF_Dict_AddNumber (descriptor, "Flags", def->flags);

        array = HPDF_Box_Array_New (obj->mmgr, def->font_bbox);
        ret += HPDF_Dict_Add (descriptor, "FontBBox", array);

        ret += HPDF_Dict_AddName (descriptor, "FontName", def_attr->base_font);
        ret += HPDF_Dict_AddNumber (descriptor, "ItalicAngle",
                def->italic_angle);
        ret += HPDF_Dict_AddNumber (descriptor, "StemV", def->stemv);
        ret += HPDF_Dict_AddNumber (descriptor, "XHeight", def->x_height);

        if (ret != HPDF_OK)
            return HPDF_Error_GetCode (obj->error);

        font_attr->fontdef->descriptor = descriptor;
    }

    if ((ret = HPDF_Dict_AddName (obj, "BaseFont",
                def_attr->base_font)) != HPDF_OK)
        return ret;

    if ((ret = HPDF_Dict_AddName (font_attr->descendant_font, "BaseFont",
                def_attr->base_font)) != HPDF_OK)
        return ret;

    return HPDF_Dict_Add (font_attr->descendant_font, "FontDescriptor",
                font_attr->fontdef->descriptor);
}


static HPDF_TextWidth
TextWidth  (HPDF_Font         font,
            const HPDF_BYTE  *text,
            HPDF_UINT         len)
{
    HPDF_TextWidth tw = {0, 0, 0, 0};
    HPDF_FontAttr attr = (HPDF_FontAttr)font->attr;
    HPDF_ParseText_Rec  parse_state;
    HPDF_Encoder encoder = attr->encoder;
    HPDF_UINT i = 0;
    HPDF_INT dw2;
    HPDF_BYTE b = 0;

    HPDF_PTRACE ((" HPDF_Type0Font_TextWidth\n"));

    if (attr->fontdef->type == HPDF_FONTDEF_TYPE_CID) {
        HPDF_CIDFontDefAttr cid_fontdef_attr =
                    (HPDF_CIDFontDefAttr)attr->fontdef->attr;
        dw2 = cid_fontdef_attr->DW2[1];
    } else {
        dw2 = (HPDF_INT)(attr->fontdef->font_bbox.bottom -
                    attr->fontdef->font_bbox.top);
    }

    HPDF_Encoder_SetParseText (encoder, &parse_state, text, len);

    while (i < len) {
        HPDF_ByteType btype = (encoder->byte_type_fn)(encoder, &parse_state);
        HPDF_UINT16 cid;
        HPDF_UNICODE unicode;
        HPDF_UINT16 code;
        HPDF_UINT w = 0;

        b = *text++;
        code = b;

        if (btype == HPDF_BYTE_TYPE_LEAD) {
            code <<= 8;
            code = (HPDF_UINT16)(code + *text);
        }

        if (btype != HPDF_BYTE_TYPE_TRAIL) {
            if (attr->writing_mode == HPDF_WMODE_HORIZONTAL) {
                if (attr->fontdef->type == HPDF_FONTDEF_TYPE_CID) {
                    /* cid-based font */
                    cid = HPDF_CMapEncoder_ToCID (encoder, code);
                    w = HPDF_CIDFontDef_GetCIDWidth (attr->fontdef, cid);
                } else {
                    /* unicode-based font */
                    unicode = (encoder->to_unicode_fn)(encoder, code);
                    w = HPDF_TTFontDef_GetCharWidth (attr->fontdef, unicode);
                }
            } else {
                w = -dw2;
            }

            tw.numchars++;
        }

        if (HPDF_IS_WHITE_SPACE(code)) {
            tw.numwords++;
            tw.numspace++;
        }

        tw.width += w;
        i++;
    }

    /* 2006.08.19 add. */
    if (HPDF_IS_WHITE_SPACE(b))
        ; /* do nothing. */
    else
        tw.numwords++;

    return tw;
}


static HPDF_UINT
MeasureText  (HPDF_Font          font,
              const HPDF_BYTE   *text,
              HPDF_UINT          len,
              HPDF_REAL          width,
              HPDF_REAL          font_size,
              HPDF_REAL          char_space,
              HPDF_REAL          word_space,
              HPDF_BOOL          wordwrap,
              HPDF_REAL         *real_width)
{
    HPDF_REAL w = 0;
    HPDF_UINT tmp_len = 0;
    HPDF_UINT i;
    HPDF_FontAttr attr = (HPDF_FontAttr)font->attr;
    //HPDF_ByteType last_btype = HPDF_BYTE_TYPE_TRAIL;
    HPDF_Encoder encoder = attr->encoder;
    HPDF_ParseText_Rec  parse_state;
    HPDF_INT dw2;

    HPDF_PTRACE ((" HPDF_Type0Font_MeasureText\n"));

    if (attr->fontdef->type == HPDF_FONTDEF_TYPE_CID) {
        HPDF_CIDFontDefAttr cid_fontdef_attr =
                (HPDF_CIDFontDefAttr)attr->fontdef->attr;
        dw2 = cid_fontdef_attr->DW2[1];
    } else {
        dw2 = (HPDF_INT)(attr->fontdef->font_bbox.bottom -
                    attr->fontdef->font_bbox.top);
    }

    HPDF_Encoder_SetParseText (encoder, &parse_state, text, len);

    for (i = 0; i < len; i++) {
        HPDF_BYTE b = *text++;
        HPDF_BYTE b2 = *text;  /* next byte */
        HPDF_ByteType btype = HPDF_Encoder_ByteType (encoder, &parse_state);
        HPDF_UNICODE unicode;
        HPDF_UINT16 code = b;
        HPDF_UINT16 tmp_w = 0;

        if (btype == HPDF_BYTE_TYPE_LEAD) {
            code <<= 8;
            code = (HPDF_UINT16)(code + b2);
        }

        if (!wordwrap) {
            if (HPDF_IS_WHITE_SPACE(b)) {
                tmp_len = i + 1;
                if (real_width)
                    *real_width = w;
            } else if (btype == HPDF_BYTE_TYPE_SINGLE ||
                        btype == HPDF_BYTE_TYPE_LEAD) {
                tmp_len = i;
                if (real_width)
                    *real_width = w;
            }
        } else {
            if (HPDF_IS_WHITE_SPACE(b)) {
                tmp_len = i + 1;
                if (real_width)
                    *real_width = w;
            } /* else
			//Commenting this out fixes problem with HPDF_Text_Rect() splitting the words
            if (last_btype == HPDF_BYTE_TYPE_TRAIL ||
                    (btype == HPDF_BYTE_TYPE_LEAD &&
                    last_btype == HPDF_BYTE_TYPE_SINGLE)) {
                if (!HPDF_Encoder_CheckJWWLineHead(encoder, code)) {
                    tmp_len = i;
                    if (real_width)
                        *real_width = w;
                }
            }*/
        }

        if (HPDF_IS_WHITE_SPACE(b)) {
            w += word_space;
        }

        if (btype != HPDF_BYTE_TYPE_TRAIL) {
            if (attr->writing_mode == HPDF_WMODE_HORIZONTAL) {
                if (attr->fontdef->type == HPDF_FONTDEF_TYPE_CID) {
                    /* cid-based font */
                    HPDF_UINT16 cid = HPDF_CMapEncoder_ToCID (encoder, code);
                    tmp_w = HPDF_CIDFontDef_GetCIDWidth (attr->fontdef, cid);
                } else {
                    /* unicode-based font */
                    unicode = (encoder->to_unicode_fn)(encoder, code);
                    tmp_w = HPDF_TTFontDef_GetCharWidth (attr->fontdef,
                            unicode);
                }
            } else {
                tmp_w = (HPDF_UINT16)(-dw2);
            }

            if (i > 0)
                w += char_space;
        }

        w += (HPDF_REAL)((HPDF_DOUBLE)tmp_w * font_size / 1000);

        /* 2006.08.04 break when it encountered  line feed */
        if (w > width || b == 0x0A)
            return tmp_len;

        /*
        if (HPDF_IS_WHITE_SPACE(b))
            last_btype = HPDF_BYTE_TYPE_TRAIL;
        else
            last_btype = btype;
        */

    }

    /* all of text can be put in the specified width */
    if (real_width)
        *real_width = w;

    return len;
}



static char*
UINT16ToHex  (char        *s,
              HPDF_UINT16  val,
              char        *eptr,
              HPDF_BYTE    width)
{
    HPDF_BYTE b[2];
    HPDF_UINT16 val2;
    char c;

    if (eptr - s < 7)
        return s;

    /* align byte-order */
    HPDF_MemCpy (b, (HPDF_BYTE *)&val, 2);
    val2 = (HPDF_UINT16)((HPDF_UINT16)b[0] << 8 | (HPDF_UINT16)b[1]);

    HPDF_MemCpy (b, (HPDF_BYTE *)&val2, 2);

    *s++ = '<';

    /*
     * In principle a range of <00> - <1F> can now not be
     * distinguished from <0000> - <001F>..., this seems something
     * that is wrong with CID ranges. For the UCS-2 encoding we need
     * to add <0000> - <FFFF> and this cannot be <00> - <FFFF> (or at
     * least, that crashes Mac OSX Preview).
     */
    if (width == 2) {
        c = b[0] >> 4;
        if (c <= 9)
            c += 0x30;
        else
            c += 0x41 - 10;
        *s++ = c;

        c = (char)(b[0] & 0x0f);
        if (c <= 9)
            c += 0x30;
        else
            c += 0x41 - 10;
        *s++ = c;
    }

    c = (char)(b[1] >> 4);
    if (c <= 9)
        c += 0x30;
    else
        c += 0x41 - 10;
    *s++ = c;

    c = (char)(b[1] & 0x0f);
    if (c <= 9)
        c += 0x30;
    else
        c += 0x41 - 10;
    *s++ = c;

    *s++ = '>';
    *s = 0;

    return s;
}

static char*
CidRangeToHex  (char        *s,
                HPDF_UINT16  from,
                HPDF_UINT16  to,
                char        *eptr)
{
    HPDF_BYTE width = (to > 255) ? 2 : 1;
    char *pbuf;

    pbuf = UINT16ToHex (s, from, eptr, width);
    *pbuf++ = ' ';
    pbuf = UINT16ToHex (pbuf, to, eptr, width);

    return pbuf;
}

static HPDF_Dict
CreateCMap  (HPDF_Encoder   encoder,
             HPDF_Xref      xref)
{
    HPDF_STATUS ret = HPDF_OK;
    HPDF_Dict cmap = HPDF_DictStream_New (encoder->mmgr, xref);
    HPDF_CMapEncoderAttr attr = (HPDF_CMapEncoderAttr)encoder->attr;
    char buf[HPDF_TMP_BUF_SIZ];
    char *pbuf;
    char *eptr = buf + HPDF_TMP_BUF_SIZ - 1;
    HPDF_UINT i;
    HPDF_UINT phase, odd;
    HPDF_Dict sysinfo;

    if (!cmap)
        return NULL;

    ret += HPDF_Dict_AddName (cmap, "Type", "CMap");
    ret += HPDF_Dict_AddName (cmap, "CMapName", encoder->name);

    sysinfo = HPDF_Dict_New (encoder->mmgr);
    if (!sysinfo)
        return NULL;

    if (HPDF_Dict_Add (cmap, "CIDSystemInfo", sysinfo) != HPDF_OK)
        return NULL;

    ret += HPDF_Dict_Add (sysinfo, "Registry", HPDF_String_New (encoder->mmgr,
                    attr->registry, NULL));
    ret += HPDF_Dict_Add (sysinfo, "Ordering", HPDF_String_New (encoder->mmgr,
                    attr->ordering, NULL));
    ret += HPDF_Dict_AddNumber (sysinfo, "Supplement", attr->suppliment);
    ret += HPDF_Dict_AddNumber (cmap, "WMode",
                    (HPDF_UINT32)attr->writing_mode);

    /* create cmap data from encoding data */
    ret += HPDF_Stream_WriteStr (cmap->stream,
                "%!PS-Adobe-3.0 Resource-CMap\r\n");
    ret += HPDF_Stream_WriteStr (cmap->stream,
                "%%DocumentNeededResources: ProcSet (CIDInit)\r\n");
    ret += HPDF_Stream_WriteStr (cmap->stream,
                "%%IncludeResource: ProcSet (CIDInit)\r\n");

    pbuf = (char *)HPDF_StrCpy (buf, "%%BeginResource: CMap (", eptr);
    pbuf = (char *)HPDF_StrCpy (pbuf, encoder->name, eptr);
    HPDF_StrCpy (pbuf, ")\r\n", eptr);
    ret += HPDF_Stream_WriteStr (cmap->stream, buf);

    pbuf = (char *)HPDF_StrCpy (buf, "%%Title: (", eptr);
    pbuf = (char *)HPDF_StrCpy (pbuf, encoder->name, eptr);
    *pbuf++ = ' ';
    pbuf = (char *)HPDF_StrCpy (pbuf, attr->registry, eptr);
    *pbuf++ = ' ';
    pbuf = (char *)HPDF_StrCpy (pbuf, attr->ordering, eptr);
    *pbuf++ = ' ';
    pbuf = HPDF_IToA (pbuf, attr->suppliment, eptr);
    HPDF_StrCpy (pbuf, ")\r\n", eptr);
    ret += HPDF_Stream_WriteStr (cmap->stream, buf);

    ret += HPDF_Stream_WriteStr (cmap->stream, "%%Version: 1.0\r\n");
    ret += HPDF_Stream_WriteStr (cmap->stream, "%%EndComments\r\n");

    ret += HPDF_Stream_WriteStr (cmap->stream,
                "/CIDInit /ProcSet findresource begin\r\n\r\n");

    /* Adobe CMap and CIDFont Files Specification recommends to allocate
     * five more elements to this dictionary than existing elements.
     */
    ret += HPDF_Stream_WriteStr (cmap->stream, "12 dict begin\r\n\r\n");

    ret += HPDF_Stream_WriteStr (cmap->stream, "begincmap\r\n\r\n");
    ret += HPDF_Stream_WriteStr (cmap->stream,
                "/CIDSystemInfo 3 dict dup begin\r\n");

    pbuf = (char *)HPDF_StrCpy (buf, "  /Registry (", eptr);
    pbuf = (char *)HPDF_StrCpy (pbuf, attr->registry, eptr);
    HPDF_StrCpy (pbuf, ") def\r\n", eptr);
    ret += HPDF_Stream_WriteStr (cmap->stream, buf);

    pbuf = (char *)HPDF_StrCpy (buf, "  /Ordering (", eptr);
    pbuf = (char *)HPDF_StrCpy (pbuf, attr->ordering, eptr);
    HPDF_StrCpy (pbuf, ") def\r\n", eptr);
    ret += HPDF_Stream_WriteStr (cmap->stream, buf);

    pbuf = (char *)HPDF_StrCpy (buf, "  /Supplement ", eptr);
    pbuf = HPDF_IToA (pbuf, attr->suppliment, eptr);
    pbuf = (char *)HPDF_StrCpy (pbuf, " def\r\n", eptr);
    HPDF_StrCpy (pbuf, "end def\r\n\r\n", eptr);
    ret += HPDF_Stream_WriteStr (cmap->stream, buf);

    pbuf = (char *)HPDF_StrCpy (buf, "/CMapName /", eptr);
    pbuf = (char *)HPDF_StrCpy (pbuf, encoder->name, eptr);
    HPDF_StrCpy (pbuf, " def\r\n", eptr);
    ret += HPDF_Stream_WriteStr (cmap->stream, buf);

    ret += HPDF_Stream_WriteStr (cmap->stream, "/CMapVersion 1.0 def\r\n");
    ret += HPDF_Stream_WriteStr (cmap->stream, "/CMapType 1 def\r\n\r\n");

    if (attr->uid_offset >= 0) {
        pbuf = (char *)HPDF_StrCpy (buf, "/UIDOffset ", eptr);
        pbuf = HPDF_IToA (pbuf, attr->uid_offset, eptr);
        HPDF_StrCpy (pbuf, " def\r\n\r\n", eptr);
        ret += HPDF_Stream_WriteStr (cmap->stream, buf);
    }

    pbuf = (char *)HPDF_StrCpy (buf, "/XUID [", eptr);
    pbuf = HPDF_IToA (pbuf, attr->xuid[0], eptr);
    *pbuf++ = ' ';
    pbuf = HPDF_IToA (pbuf, attr->xuid[1], eptr);
    *pbuf++ = ' ';
    pbuf = HPDF_IToA (pbuf, attr->xuid[2], eptr);
    HPDF_StrCpy (pbuf, "] def\r\n\r\n", eptr);
    ret += HPDF_Stream_WriteStr (cmap->stream, buf);

    pbuf = (char *)HPDF_StrCpy (buf, "/WMode ", eptr);
    pbuf = HPDF_IToA (pbuf, (HPDF_UINT32)attr->writing_mode, eptr);
    HPDF_StrCpy (pbuf, " def\r\n\r\n", eptr);
    ret += HPDF_Stream_WriteStr (cmap->stream, buf);

    /* add code-space-range */
    pbuf = HPDF_IToA (buf, attr->code_space_range->count, eptr);
    HPDF_StrCpy (pbuf, " begincodespacerange\r\n", eptr);
    ret += HPDF_Stream_WriteStr (cmap->stream, buf);

    for (i = 0; i < attr->code_space_range->count; i++) {
        HPDF_CidRange_Rec *range = HPDF_List_ItemAt (attr->code_space_range,
                        i);

        pbuf = CidRangeToHex(buf, range->from, range->to, eptr);

        HPDF_StrCpy (pbuf, "\r\n", eptr);

        ret += HPDF_Stream_WriteStr (cmap->stream, buf);

        if (ret != HPDF_OK)
            return NULL;
    }

    HPDF_StrCpy (buf, "endcodespacerange\r\n\r\n", eptr);
    ret += HPDF_Stream_WriteStr (cmap->stream, buf);
    if (ret != HPDF_OK)
        return NULL;

    /* add not-def-range */
    pbuf = HPDF_IToA (buf, attr->notdef_range->count, eptr);
    HPDF_StrCpy (pbuf, " beginnotdefrange\r\n", eptr);
    ret += HPDF_Stream_WriteStr (cmap->stream, buf);

    for (i = 0; i < attr->notdef_range->count; i++) {
        HPDF_CidRange_Rec *range = HPDF_List_ItemAt (attr->notdef_range, i);

        pbuf = CidRangeToHex(buf, range->from, range->to, eptr);
        *pbuf++ = ' ';
        pbuf = HPDF_IToA (pbuf, range->cid, eptr);
        HPDF_StrCpy (pbuf, "\r\n", eptr);

        ret += HPDF_Stream_WriteStr (cmap->stream, buf);

        if (ret != HPDF_OK)
            return NULL;
    }

    HPDF_StrCpy (buf, "endnotdefrange\r\n\r\n", eptr);
    ret += HPDF_Stream_WriteStr (cmap->stream, buf);
    if (ret != HPDF_OK)
        return NULL;

    /* add cid-range */
    phase = attr->cmap_range->count / 100;
    odd = attr->cmap_range->count % 100;
    if (phase > 0)
        pbuf = HPDF_IToA (buf, 100, eptr);
    else
        pbuf = HPDF_IToA (buf, odd, eptr);
    HPDF_StrCpy (pbuf, " begincidrange\r\n", eptr);
    ret += HPDF_Stream_WriteStr (cmap->stream, buf);

    for (i = 0; i < attr->cmap_range->count; i++) {
        HPDF_CidRange_Rec *range = HPDF_List_ItemAt (attr->cmap_range, i);

        pbuf = CidRangeToHex(buf, range->from, range->to, eptr);
        *pbuf++ = ' ';
        pbuf = HPDF_IToA (pbuf, range->cid, eptr);
        HPDF_StrCpy (pbuf, "\r\n", eptr);

        ret += HPDF_Stream_WriteStr (cmap->stream, buf);

        if ((i + 1) %100 == 0) {
            phase--;
            pbuf = (char *)HPDF_StrCpy (buf, "endcidrange\r\n\r\n", eptr);

            if (phase > 0)
                pbuf = HPDF_IToA (pbuf, 100, eptr);
            else
                pbuf = HPDF_IToA (pbuf, odd, eptr);

            HPDF_StrCpy (pbuf, " begincidrange\r\n", eptr);

            ret += HPDF_Stream_WriteStr (cmap->stream, buf);
        }

        if (ret != HPDF_OK)
            return NULL;
    }

    if (odd > 0)
        pbuf = (char *)HPDF_StrCpy (buf, "endcidrange\r\n", eptr);

    pbuf = (char *)HPDF_StrCpy (pbuf, "endcmap\r\n", eptr);
    pbuf = (char *)HPDF_StrCpy (pbuf, "CMapName currentdict /CMap "
            "defineresource pop\r\n", eptr);
    pbuf = (char *)HPDF_StrCpy (pbuf, "end\r\n", eptr);
    pbuf = (char *)HPDF_StrCpy (pbuf, "end\r\n\r\n", eptr);
    pbuf = (char *)HPDF_StrCpy (pbuf, "%%EndResource\r\n", eptr);
    HPDF_StrCpy (pbuf, "%%EOF\r\n", eptr);
    ret += HPDF_Stream_WriteStr (cmap->stream, buf);

    if (ret != HPDF_OK)
        return NULL;

    return cmap;
}

