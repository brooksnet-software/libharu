# Image masks must not have a ColorSpace

A change to make in this fork: an image used as an image mask keeps the
`/ColorSpace` it was loaded with, which the PDF specification doesn't allow.
Adobe's viewers then draw nothing for the masked image.

## The problem

`HPDF_Image_SetMaskImage(image, mask)` makes `mask` the explicit mask of
`image`. It calls `HPDF_Image_SetMask(mask, HPDF_TRUE)`
(`src/hpdf_image.c`), which adds `/ImageMask true` to the mask, but the
`/ColorSpace` that `HPDF_Image_LoadRaw1BitImageFromMem` (or
`HPDF_LoadRawImageFromMem` with `HPDF_CS_DEVICE_GRAY`) put there stays.
The mask comes out as:

```
<< /Type /XObject /Subtype /Image
   /ColorSpace /DeviceGray          <- not allowed with /ImageMask true
   /Width 3248 /Height 2520 /BitsPerComponent 1
   /ImageMask true
   /Filter [ /CCITTFaxDecode ] ... >>
```

ISO 32000-1, 8.9.6.2 (Stencil Masking) and Table 89: for an image mask,
*ColorSpace shall not be specified*, and *BitsPerComponent* shall be 1.

How viewers treat it:

| Viewer | Result |
|---|---|
| Adobe Acrobat / Reader | the masked image isn't drawn at all |
| mupdf, Foxit, Ghostscript, pdf.js | drawn (the key is ignored) |

veraPDF (PDF/A-2b) doesn't report it, so PDF/A output can still be affected.

Found in ExcelliPrint 5.0: overlays and page segments (1-bit images drawn as
one pixel of their colour with the bits as the explicit mask) were missing in
Adobe Reader. ExcelliPrint works around it in `pdfgen/PdfBackend.cpp`
(`DrawMask`) by removing the key itself after `HPDF_Image_SetMaskImage`:

```c
HPDF_Dict_RemoveElement(mask, "ColorSpace");
```

## The change

In `src/hpdf_image.c`, `HPDF_Image_SetMask`: when an image becomes a mask,
remove its colour space.

```c
HPDF_STATUS
HPDF_Image_SetMask (HPDF_Image   image,
                    HPDF_BOOL    mask)
{
    HPDF_Boolean image_mask;

    if (!HPDF_Image_Validate (image))
        return HPDF_INVALID_IMAGE;

    if (mask && HPDF_Image_GetBitsPerComponent (image) != 1)
        return HPDF_SetError (image->error, HPDF_INVALID_BIT_PER_COMPONENT,
                0);

    image_mask = HPDF_Dict_GetItem (image, "ImageMask", HPDF_OCLASS_BOOLEAN);
    if (!image_mask) {
        HPDF_STATUS ret;
        image_mask = HPDF_Boolean_New (image->mmgr, HPDF_FALSE);

        if ((ret = HPDF_Dict_Add (image, "ImageMask", image_mask)) != HPDF_OK)
            return ret;
    }

    image_mask->value = mask;

    /* An image mask has no colour space (ISO 32000-1, 8.9.6.2) */
    if (mask)
        HPDF_Dict_RemoveElement (image, "ColorSpace");

    return HPDF_OK;
}
```

`HPDF_Dict_RemoveElement` returns `HPDF_DICT_ITEM_NOT_FOUND` without raising
an error when the key isn't there, so ignoring its result is safe.

### What else reads the colour space

Checked in this fork; none of these is affected:

- `HPDF_Image_SetColorMask` refuses an image that has `/ImageMask` before it
  looks at the colour space, so it never sees a mask.
- `HPDF_Image_AddSMask` wants a DeviceGray soft mask. A soft mask is a
  different thing from an image mask and never goes through
  `HPDF_Image_SetMask`.
- `HPDF_Image_GetColorSpace` returns NULL for a mask after the change, which
  is the truth.
- Nothing in libHaru calls `HPDF_Image_SetMask(image, HPDF_FALSE)` after
  `HPDF_TRUE`. If something did, the image would have no colour space; such a
  caller would have to add one back.

## Testing

1. Rebuild the library (see `BUILD-windows.md`, Step 2).
2. `demo/image_demo.c` uses `HPDF_Image_SetMaskImage`. Build and run it, then
   check the mask object in the output has `/ImageMask true` and no
   `/ColorSpace`, and the masked image shows in Adobe Reader.
3. In ExcelliPrint, rebuild `_pdfgen.exe` against the new `hpdf.lib` and
   render a page with an overlay or page segment (for example the rapids3
   capture). Adobe Reader should show the overlay, and Ghostscript's
   rendering should be unchanged.
4. Optionally, run veraPDF on a PDF/A file with a mask; it should still pass.

## Related fix: CCITT images without image compression

Found while testing the above. `HPDF_Image_LoadRaw1BitImageFromMem`
(`src/hpdf_image_ccitt.c`) always CCITT G4-encodes the data, but only added
`/Filter [ /CCITTFaxDecode ]` and `/DecodeParms` when the document had
`HPDF_COMP_IMAGE` on. With compression off, the stream was CCITT data with
no filter, so viewers decoded it as raw bits (garbage). The filter is now
always set. The same function also dereferenced `image` after a failed load;
it now returns NULL.

## After the change

The workaround in ExcelliPrint's `pdfgen/PdfBackend.cpp` (`DrawMask`, the
`HPDF_Dict_RemoveElement(mask, "ColorSpace")` line) is then redundant. It
does no harm and can be removed whenever convenient.

This could also go upstream (libharu/libharu on GitHub), if upstream's
`HPDF_Image_SetMask` still leaves the colour space (not checked).
