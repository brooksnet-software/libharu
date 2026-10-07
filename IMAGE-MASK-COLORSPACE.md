# Image masks must not have a ColorSpace

Fixed in this fork: an image used as an image mask kept the `/ColorSpace` it
was loaded with, which the PDF specification doesn't allow. Adobe's viewers
then draw nothing for the masked image.

## The problem

`HPDF_Image_SetMaskImage(image, mask)` makes `mask` the explicit mask of
`image`. It calls `HPDF_Image_SetMask(mask, HPDF_TRUE)`
(`src/hpdf_image.c`), which adds `/ImageMask true` to the mask, but the
`/ColorSpace` that `HPDF_Image_LoadRaw1BitImageFromMem` (or
`HPDF_LoadRawImageFromMem` with `HPDF_CS_DEVICE_GRAY`) put there stayed.
The mask came out as:

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

A typical case is a 1-bit image drawn in a single colour: a one-pixel image
of the colour, with the bits as its explicit mask. In Adobe Reader nothing
appeared.

## The change

In `src/hpdf_image.c`, `HPDF_Image_SetMask`: when an image becomes a mask,
its colour space is removed.

```c
    image_mask->value = mask;

    /* An image mask has no colour space (ISO 32000-1, 8.9.6.2) */
    if (mask)
        HPDF_Dict_RemoveElement (image, "ColorSpace");

    return HPDF_OK;
```

`HPDF_Dict_RemoveElement` returns `HPDF_DICT_ITEM_NOT_FOUND` without raising
an error when the key isn't there, so ignoring its result is safe. It doesn't
free indirect objects, so a shared colour space is left alone.

### What else reads the colour space

None of these is affected:

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
- A 1-bit indexed PNG used as a mask loses its palette. That is correct: the
  bits are then stencil samples (0 paints), and the palette had no meaning.

## Related fix: CCITT images without image compression

Found while testing the above. `HPDF_Image_LoadRaw1BitImageFromMem`
(`src/hpdf_image_ccitt.c`) always CCITT G4-encodes the data, but only added
`/Filter [ /CCITTFaxDecode ]` and `/DecodeParms` when the document had
`HPDF_COMP_IMAGE` on. With compression off, the stream was CCITT data with
no filter, so viewers decoded it as raw bits (garbage). The filter is now
always set. The same function also dereferenced `image` after a failed load;
it now returns NULL.

## Testing

1. Rebuild the library (see `BUILD-windows.md`, Step 2).
2. Make an RGB image and a 1-bit mask, once with `HPDF_LoadRawImageFromMem`
   (DeviceGray, 1 bit) and once with `HPDF_Image_LoadRaw1BitImageFromMem`,
   and join them with `HPDF_Image_SetMaskImage`. (`demo/image_demo.c` does
   the same with PNGs, but needs PNG support built in.)
3. Save with compression off and with `HPDF_COMP_IMAGE` on. In both, each
   mask object should have `/ImageMask true`, `/BitsPerComponent 1` and no
   `/ColorSpace`; the CCITT one should have `/Filter [ /CCITTFaxDecode ]`.
   Check with `mutool show file.pdf <obj>` or qpdf.
4. The masked images should show in Adobe Reader, and render the same as
   before in Ghostscript and mupdf.
5. Optionally, run veraPDF on a PDF/A file with a mask; it should still pass.

## Applications that worked around it

An application may already remove the key itself after
`HPDF_Image_SetMaskImage`:

```c
HPDF_Dict_RemoveElement(mask, "ColorSpace");
```

With this fix that line is redundant. It does no harm and can be removed
whenever convenient.

## Upstream

As of libHaru 2.4.6 (upstream `master`, March 2026), upstream has neither
fix. Its unmerged `uncompressed_1bit` branch (2023) addresses the CCITT
problem differently, by writing the bits uncompressed when compression is
off.
