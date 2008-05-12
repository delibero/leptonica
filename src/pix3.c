/*====================================================================*
 -  Copyright (C) 2001 Leptonica.  All rights reserved.
 -  This software is distributed in the hope that it will be
 -  useful, but with NO WARRANTY OF ANY KIND.
 -  No author or distributor accepts responsibility to anyone for the
 -  consequences of using this software, or for whether it serves any
 -  particular purpose or works at all, unless he or she says so in
 -  writing.  Everyone is granted permission to copy, modify and
 -  redistribute this source code, for commercial or non-commercial
 -  purposes, with the following restrictions: (1) the origin of this
 -  source code must not be misrepresented; (2) modified versions must
 -  be plainly marked as such; and (3) this notice may not be removed
 -  or altered from any source or modified source distribution.
 *====================================================================*/

/*
 *  pix3.c
 *
 *    This file has these operations:
 *
 *      (1) Mask-directed operations
 *      (2) Full-image bit-logical operations
 *      (3) Pixel counting operations
 *      (4) Pixel histograms
 *      (5) Foreground/background estimation
 *      (6) Rectangle extraction
 *      (7) Mirrored tiling of a smaller image
 *
 *      Masked operations
 *           l_int32     pixSetMasked()
 *           l_int32     pixSetMaskedGeneral()
 *           l_int32     pixCombineMasked()
 *           l_int32     pixPaintThroughMask()
 *           l_int32     pixCombineThroughMask()
 *           PIX        *pixPaintSelfThroughMask()
 *
 *      One and two-image boolean operations on arbitrary depth images
 *           PIX        *pixInvert()
 *           PIX        *pixOr()
 *           PIX        *pixAnd()
 *           PIX        *pixXor()
 *           PIX        *pixSubtract()
 *
 *      Pixel counting
 *           l_int32     pixZero()
 *           l_int32     pixCountPixels()
 *           NUMA       *pixaCountPixels()
 *           l_int32     pixCountPixelsInRow()
 *           NUMA       *pixCountPixelsByRow()
 *           l_int32     pixThresholdPixels()
 *           l_int32    *makePixelSumTab8()
 *           l_int32    *makePixelCentroidTab8()
 *
 *      Pixel histogram, rank val, averaging and min/max
 *           NUMA       *pixGetGrayHistogram()
 *           NUMA       *pixGetGrayHistogramMasked()
 *           l_int32     pixGetColorHistogram()
 *           l_int32     pixGetColorHistogramMasked()
 *           l_int32     pixGetRankValueMaskedRGB()
 *           l_int32     pixGetRankValueMasked()
 *           l_int32     pixGetAverageMaskedRGB()
 *           l_int32     pixGetAverageMasked()
 *           l_int32     pixGetAverageTiledRGB()
 *           PIX        *pixGetAverageTiled()
 *           l_int32     pixGetExtremeValue()
 *
 *      Foreground/background estimation
 *           l_int32     pixThresholdForFgBg()
 *           l_int32     pixSplitDistributionFgBg()
 *
 *      Measurement of properties
 *           l_int32     pixFindAreaPerimRatio()
 *
 *      Extract rectangle
 *           PIX        *pixClipRectangle()
 *           PIX        *pixClipMasked()
 *
 *      Clip to foreground
 *           PIX        *pixClipToForeground()
 *
 *      Mirrored tiling
 *           PIX        *pixMirroredTiling()
 *
 *      Static helper function
 *           static l_int32  findTilePatchCenter()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "allheaders.h"

static l_int32 findTilePatchCenter(PIX *pixs, BOX *box, l_int32 dir,
                                   l_uint32 targdist, l_uint32 *pdist,
                                   l_int32 *pxc, l_int32 *pyc);

static const l_uint32 rmask32[] = {0x0,
    0x00000001, 0x00000003, 0x00000007, 0x0000000f,
    0x0000001f, 0x0000003f, 0x0000007f, 0x000000ff,
    0x000001ff, 0x000003ff, 0x000007ff, 0x00000fff,
    0x00001fff, 0x00003fff, 0x00007fff, 0x0000ffff,
    0x0001ffff, 0x0003ffff, 0x0007ffff, 0x000fffff,
    0x001fffff, 0x003fffff, 0x007fffff, 0x00ffffff,
    0x01ffffff, 0x03ffffff, 0x07ffffff, 0x0fffffff,
    0x1fffffff, 0x3fffffff, 0x7fffffff, 0xffffffff};


#ifndef  NO_CONSOLE_IO
#define   EQUAL_SIZE_WARNING      0
#endif  /* ~NO_CONSOLE_IO */


/*-------------------------------------------------------------*
 *                        Masked operations                    *
 *-------------------------------------------------------------*/
/*!
 *  pixSetMasked()
 *
 *      Input:  pixd (1, 2, 4, 8, 16 or 32 bpp; or colormapped)
 *              pixm (<optional> 1 bpp mask; no operation if NULL)
 *              val (value to set at each masked pixel)
 *      Return: 0 if OK; 1 on error
 *
 *  Notes:
 *      (1) In-place operation.  Calls pixSetMaskedCmap() for colormapped
 *          images.
 *      (2) If pixm == NULL, a warning is given.
 *      (3) It is an implicitly aligned operation, where the UL
 *          corners of pixd and pixm coincide.  A warning is
 *          issued if the two image sizes differ significantly,
 *          but the operation proceeds.
 *      (4) Each pixel in pixd that co-locates with an ON pixel
 *          in pixm is set to the specified input value.
 *          Other pixels in pixd are not changed.
 *      (5) You can visualize this as painting the color through
 *          the mask, as a stencil.
 *      (6) If you do not want to have the UL corners aligned,
 *          use the function pixSetMaskedGeneral(), which requires
 *          you to input the UL corner of pixm relative to pixd.
 *      (7) Implementation details: see comments in pixPaintThroughMask()
 *          for when we use rasterop to do the painting.
 */
l_int32
pixSetMasked(PIX      *pixd,
             PIX      *pixm,
             l_uint32  val)
{
l_int32    wd, hd, wm, hm, w, h, d, wpld, wplm;
l_int32    i, j, rval, gval, bval;
l_uint32  *datad, *datam, *lined, *linem;

    PROCNAME("pixSetMasked");

    if (!pixd)
        return ERROR_INT("pixd not defined", procName, 1);
    if (!pixm) {
        L_WARNING("no mask; nothing to do", procName);
        return 0;
    }
    if (pixGetColormap(pixd)) {
        rval = GET_DATA_BYTE(&val, COLOR_RED);
        gval = GET_DATA_BYTE(&val, COLOR_GREEN);
        bval = GET_DATA_BYTE(&val, COLOR_BLUE);
        return pixSetMaskedCmap(pixd, pixm, 0, 0, rval, gval, bval);
    }

    if (pixGetDepth(pixm) != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);
    d = pixGetDepth(pixd);
    if (d == 1)
        val &= 1;
    else if (d == 2)
        val &= 3;
    else if (d == 4)
        val &= 0x0f;
    else if (d == 8)
        val &= 0xff;
    else if (d == 16)
        val &= 0xffff;
    else if (d != 32)
        return ERROR_INT("pixd not 1, 2, 4, 8, 16 or 32 bpp", procName, 1);
    pixGetDimensions(pixm, &wm, &hm, NULL);

        /* If d == 1, use rasterop; it's about 25x faster */
    if (d == 1) {
        if (val == 0) {
            PIX *pixmi = pixInvert(NULL, pixm);
            pixRasterop(pixd, 0, 0, wm, hm, PIX_MASK, pixmi, 0, 0);
            pixDestroy(&pixmi);
        }
        else  /* val == 1 */
            pixRasterop(pixd, 0, 0, wm, hm, PIX_PAINT, pixm, 0, 0);
        return 0;
    }
    
        /* For d < 32, use rasterop for val == 0 (black); ~3x faster. */
    if (d < 32 && val == 0) {
        PIX *pixmd = pixUnpackBinary(pixm, d, 1);
        pixRasterop(pixd, 0, 0, wm, hm, PIX_MASK, pixmd, 0, 0);
        pixDestroy(&pixmd);
        return 0;
    }

        /* For d < 32, use rasterop for val == maxval (white); ~3x faster. */
    if (d < 32 && val == ((1 << d) - 1)) {
        PIX *pixmd = pixUnpackBinary(pixm, d, 0);
        pixRasterop(pixd, 0, 0, wm, hm, PIX_PAINT, pixmd, 0, 0);
        pixDestroy(&pixmd);
        return 0;
    }

    pixGetDimensions(pixd, &wd, &hd, &d);
    w = L_MIN(wd, wm);
    h = L_MIN(hd, hm);
    if (L_ABS(wd - wm) > 7 || L_ABS(hd - hm) > 7)  /* allow a small tolerance */
        L_WARNING("pixd and pixm sizes differ", procName);

    datad = pixGetData(pixd);
    datam = pixGetData(pixm);
    wpld = pixGetWpl(pixd);
    wplm = pixGetWpl(pixm);
    for (i = 0; i < h; i++) {
        lined = datad + i * wpld;
        linem = datam + i * wplm;
        for (j = 0; j < w; j++) {
            if (GET_DATA_BIT(linem, j)) {
                switch(d)
                {
                case 2:
                    SET_DATA_DIBIT(lined, j, val);
                    break;
                case 4:
                    SET_DATA_QBIT(lined, j, val);
                    break;
                case 8:
                    SET_DATA_BYTE(lined, j, val);
                    break;
                case 16:
                    SET_DATA_TWO_BYTES(lined, j, val);
                    break;
                case 32:
                    *(lined + j) = val;
                    break;
                default:
                    return ERROR_INT("shouldn't get here", procName, 1);
                }
            }
        }
    }

    return 0;
}


/*!
 *  pixSetMaskedGeneral()
 *
 *      Input:  pixd (8, 16 or 32 bpp)
 *              pixm (<optional> 1 bpp mask; no operation if null)
 *              val (value to set at each masked pixel)
 *              x, y (location of UL corner of pixm relative to pixd;
 *                    can be negative)
 *      Return: 0 if OK; 1 on error
 *
 *  Notes:
 *      (1) This is an in-place operation.
 *      (2) Alignment is explicit.  If you want the UL corners of
 *          the two images to be aligned, use pixSetMasked().
 *      (3) A typical use would be painting through the foreground
 *          of a small binary mask pixm, located somewhere on a
 *          larger pixd.  Other pixels in pixd are not changed.
 *      (4) You can visualize this as painting the color through
 *          the mask, as a stencil.
 *      (5) This uses rasterop to handle clipping and different depths of pixd.
 *      (6) If pixd has a colormap, you should call pixPaintThroughMask().
 *      (7) Why is this function here, if pixPaintThroughMask() does the
 *          same thing, and does it more generally?  I've retained it here
 *          to show how one can paint through a mask using only full
 *          image rasterops, rather than pixel peeking in pixm and poking
 *          in pixd.  It's somewhat baroque, but I found it amusing.
 */
l_int32
pixSetMaskedGeneral(PIX      *pixd,
                    PIX      *pixm,
                    l_uint32  val,
                    l_int32   x,
                    l_int32   y)
{
l_int32    wm, hm, d;
PIX       *pixmu, *pixc;

    PROCNAME("pixSetMaskedGeneral");

    if (!pixd)
        return ERROR_INT("pixd not defined", procName, 1);
    if (!pixm)  /* nothing to do */
        return 0;

    d = pixGetDepth(pixd);
    if (d != 8 && d != 16 && d != 32)
        return ERROR_INT("pixd not 8, 16 or 32 bpp", procName, 1);
    if (pixGetDepth(pixm) != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);

        /* Unpack binary to depth d, with inversion:  1 --> 0, 0 --> 0xff... */
    if ((pixmu = pixUnpackBinary(pixm, d, 1)) == NULL)
        return ERROR_INT("pixmu not made", procName, 1);

        /* Clear stenciled pixels in pixd */
    pixGetDimensions(pixm, &wm, &hm, NULL);
    pixRasterop(pixd, x, y, wm, hm, PIX_SRC & PIX_DST, pixmu, 0, 0);

        /* Generate image with requisite color */
    if ((pixc = pixCreateTemplate(pixmu)) == NULL)
        return ERROR_INT("pixc not made", procName, 1);
    pixSetAllArbitrary(pixc, val);

        /* Invert stencil mask, and paint color color into stencil */
    pixInvert(pixmu, pixmu);
    pixAnd(pixmu, pixmu, pixc);

        /* Finally, repaint stenciled pixels, with val, in pixd */
    pixRasterop(pixd, x, y, wm, hm, PIX_SRC | PIX_DST, pixmu, 0, 0);

    pixDestroy(&pixmu);
    pixDestroy(&pixc);
    return 0;
}


/*!
 *  pixCombineMasked()
 *
 *      Input:  pixd (8 or 32 bpp)
 *              pixs (8 or 32 bpp)
 *              pixm (<optional> 1 bpp mask; no operation if NULL)
 *      Return: 0 if OK; 1 on error
 *
 *  Notes:
 *      (1) This sets each pixel in pixd that co-locates with an ON
 *          pixel in pixm to the corresponding value of pixs.
 *      (2) Implementation: for 8 bpp selective masking, you might
 *          think that it would be faster to generate an 8 bpp
 *          version of pixm, using pixConvert1To8(pixm, 0, 255),
 *          and then use a general combine operation
 *               d = (d & ~m) | (s & m)
 *          on a word-by-word basis.  Not always.  The word-by-word
 *          combine takes a time that is independent of the mask data.
 *          If the mask is relatively sparse, the byte-check method
 *          is actually faster!
 */
l_int32
pixCombineMasked(PIX  *pixd,
                 PIX  *pixs,
                 PIX  *pixm)
{
l_uint8    val;
l_int32    d, w, wd, wm, h, hd, hm, wpld, wpls, wplm, i, j;
l_uint32  *datad, *datas, *datam, *lined, *lines, *linem;

    PROCNAME("pixCombineMasked");

    if (!pixd)
        return ERROR_INT("pixd not defined", procName, 1);
    if (!pixm)  /* nothing to do */
        return 0;
    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);

    d = pixGetDepth(pixd);
    if (d != 8 && d != 32)
        return ERROR_INT("pixd not 8 or 32 bpp", procName, 1);
    if (pixGetDepth(pixm) != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);
    if (!pixSizesEqual(pixd, pixs))
        return ERROR_INT("pixs and pixd sizes differ", procName, 1);

    pixGetDimensions(pixd, &wd, &hd, NULL);
    pixGetDimensions(pixm, &wm, &hm, NULL);
    w = L_MIN(wd, wm);
    h = L_MIN(hd, hm);
    datad = pixGetData(pixd);
    datas = pixGetData(pixs);
    datam = pixGetData(pixm);
    wpld = pixGetWpl(pixd);
    wpls = pixGetWpl(pixs);
    wplm = pixGetWpl(pixm);

    if (d == 8) {
        for (i = 0; i < h; i++) {
            lined = datad + i * wpld;
            lines = datas + i * wpls;
            linem = datam + i * wplm;
            for (j = 0; j < w; j++) {
                if (GET_DATA_BIT(linem, j)) {
                   val = GET_DATA_BYTE(lines, j);
                   SET_DATA_BYTE(lined, j, val);
                }
            }
        }
    }
    else {  /* d == 32 */
        for (i = 0; i < h; i++) {
            lined = datad + i * wpld;
            lines = datas + i * wpls;
            linem = datam + i * wplm;
            for (j = 0; j < w; j++) {
                if (GET_DATA_BIT(linem, j))
                   lined[j] = lines[j];
            }
        }
    }

    return 0;
}


/*!
 *  pixPaintThroughMask()
 *
 *      Input:  pixd (1, 2, 4, 8, 16 or 32 bpp; or colormapped)
 *              pixm (<optional> 1 bpp mask)
 *              x, y (origin of pixm relative to pixd; can be negative)
 *              val (pixel value to set at each masked pixel)
 *      Return: 0 if OK; 1 on error
 *
 *  Notes:
 *      (1) In-place operation.  Calls pixSetMaskedCmap() for colormapped
 *          images.
 *      (2) For 1, 2, 4, 8 and 16 bpp gray, we take the appropriate
 *          number of least significant bits of val.
 *      (3) If pixm == NULL, it's a no-op.
 *      (4) The mask origin is placed at (x,y) on pixd, and the
 *          operation is clipped to the intersection of rectangles.
 *      (5) For rgb, the components in val are in the canonical locations,
 *          with red in location COLOR_RED, etc.
 *      (6) Implementation detail 1:
 *          For painting with val == 0 or val == maxval, you can use rasterop.
 *          If val == 0, invert the mask so that it's 0 over the region
 *          into which you want to write, and use PIX_SRC & PIX_DST to
 *          clear those pixels.  To write with val = maxval (all 1's),
 *          use PIX_SRC | PIX_DST to set all bits under the mask.
 *      (7) Implementation detail 2:
 *          The rasterop trick can be used for depth > 1 as well.
 *          For val == 0, generate the mask for depth d from the binary
 *          mask using
 *              pixmd = pixUnpackBinary(pixm, d, 1);
 *          and use pixRasterop() with PIX_MASK.  For val == maxval,
 *              pixmd = pixUnpackBinary(pixm, d, 0);
 *          and use pixRasterop() with PIX_PAINT.
 *          But note that if d == 32 bpp, it is about 3x faster to use
 *          the general implementation (not pixRasterop()).
 *      (8) Implementation detail 3:
 *          It might be expected that the switch in the inner loop will
 *          cause large branching delays and should be avoided.
 *          This is not the case, because the entrance is always the
 *          same and the compiler can correctly predict the jump.
 */
l_int32
pixPaintThroughMask(PIX      *pixd,
                    PIX      *pixm,
                    l_int32   x,
                    l_int32   y,
                    l_uint32  val)
{
l_int32    d, w, h, wm, hm, wpl, wplm, i, j, rval, gval, bval;
l_uint32  *data, *datam, *line, *linem;

    PROCNAME("pixPaintThroughMask");

    if (!pixd)
        return ERROR_INT("pixd not defined", procName, 1);
    if (!pixm)  /* nothing to do */
        return 0;
    if (pixGetColormap(pixd)) {
        rval = GET_DATA_BYTE(&val, COLOR_RED);
        gval = GET_DATA_BYTE(&val, COLOR_GREEN);
        bval = GET_DATA_BYTE(&val, COLOR_BLUE);
        return pixSetMaskedCmap(pixd, pixm, x, y, rval, gval, bval);
    }

    if (pixGetDepth(pixm) != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);
    d = pixGetDepth(pixd);
    if (d == 1)
        val &= 1;
    else if (d == 2)
        val &= 3;
    else if (d == 4)
        val &= 0x0f;
    else if (d == 8)
        val &= 0xff;
    else if (d == 16)
        val &= 0xffff;
    else if (d != 32)
        return ERROR_INT("pixd not 1, 2, 4, 8, 16 or 32 bpp", procName, 1);
    pixGetDimensions(pixm, &wm, &hm, NULL);

        /* If d == 1, use rasterop; it's about 25x faster. */
    if (d == 1) {
        if (val == 0) {
            PIX *pixmi = pixInvert(NULL, pixm);
            pixRasterop(pixd, x, y, wm, hm, PIX_MASK, pixmi, 0, 0);
            pixDestroy(&pixmi);
        }
        else  /* val == 1 */
            pixRasterop(pixd, x, y, wm, hm, PIX_PAINT, pixm, 0, 0);
        return 0;
    }
    
        /* For d < 32, use rasterop if val == 0 (black); ~3x faster. */
    if (d < 32 && val == 0) {
        PIX *pixmd = pixUnpackBinary(pixm, d, 1);
        pixRasterop(pixd, x, y, wm, hm, PIX_MASK, pixmd, 0, 0);
        pixDestroy(&pixmd);
        return 0;
    }

        /* For d < 32, use rasterop if val == maxval (white); ~3x faster. */
    if (d < 32 && val == ((1 << d) - 1)) {
        PIX *pixmd = pixUnpackBinary(pixm, d, 0);
        pixRasterop(pixd, x, y, wm, hm, PIX_PAINT, pixmd, 0, 0);
        pixDestroy(&pixmd);
        return 0;
    }

        /* All other cases */
    pixGetDimensions(pixd, &w, &h, NULL);
    wpl = pixGetWpl(pixd);
    data = pixGetData(pixd);
    wplm = pixGetWpl(pixm);
    datam = pixGetData(pixm);
    for (i = 0; i < hm; i++) {
        if (y + i < 0 || y + i >= h) continue;
        line = data + (y + i) * wpl;
        linem = datam + i * wplm;
        for (j = 0; j < wm; j++) {
            if (x + j < 0 || x + j >= w) continue;
            if (GET_DATA_BIT(linem, j)) {
                switch (d)
                {
                case 2:
                    SET_DATA_DIBIT(line, x + j, val);
                    break;
                case 4:
                    SET_DATA_QBIT(line, x + j, val);
                    break;
                case 8:
                    SET_DATA_BYTE(line, x + j, val);
                    break;
                case 16:
                    SET_DATA_TWO_BYTES(line, x + j, val);
                    break;
                case 32:
                    *(line + x + j) = val;
                    break;
                default:
                    return ERROR_INT("shouldn't get here", procName, 1);
                }
            }
        }
    }

    return 0;
}
    

/*!
 *  pixCombineThroughMask()
 *
 *      Input:  pixd (8 bpp gray or 32 bpp rgb)
 *              pixs (8 bpp gray or 32 bpp rgb)
 *              pixm (<optional> 1 bpp mask)
 *              x, y (origin of pixs and pixm relative to pixd; can be negative)
 *      Return: 0 if OK; 1 on error
 *
 *  Notes:
 *      (1) In-place operation; pixd is changed.
 *      (2) This is a general version of pixCombinedMasked(), where
 *          the source and mask can be placed at the same (arbitrary)
 *          location relative to pixd.
 *      (3) pixs and pixd must be the same depth and not colormapped.
 *      (4) The UL corners of both pixs and pixm are aligned with
 *          the point (x, y) of pixd, and the operation is clipped to
 *          the intersection of all three images.
 *      (5) If pixm == NULL, it's a no-op.
 */
l_int32
pixCombineThroughMask(PIX      *pixd,
                      PIX      *pixs,
                      PIX      *pixm,
                      l_int32   x,
                      l_int32   y)
{
l_int32    d, w, h, ws, hs, ds, wm, hm, dm, wmin, hmin;
l_int32    wpl, wpls, wplm, i, j, val;
l_uint32  *data, *datas, *datam, *line, *lines, *linem;

    PROCNAME("pixCombineThroughMask");

    if (!pixm)  /* nothing to do */
        return 0;
    if (!pixd)
        return ERROR_INT("pixd not defined", procName, 1);
    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    pixGetDimensions(pixd, &w, &h, &d);
    pixGetDimensions(pixs, &ws, &hs, &ds);
    pixGetDimensions(pixm, &wm, &hm, &dm);
    if (d != ds)
        return ERROR_INT("pixs and pixd depths differ", procName, 1);
    if (dm != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);
    if (d != 8 && d != 32)
        return ERROR_INT("pixd not 8 or 32 bpp", procName, 1);
    if (pixGetColormap(pixd) || pixGetColormap(pixs))
        return ERROR_INT("pixs and/or pixd is cmapped", procName, 1);

    wpl = pixGetWpl(pixd);
    data = pixGetData(pixd);
    wpls = pixGetWpl(pixs);
    datas = pixGetData(pixs);
    wplm = pixGetWpl(pixm);
    datam = pixGetData(pixm);
    wmin = L_MIN(ws, wm);
    hmin = L_MIN(hs, hm);

    for (i = 0; i < hmin; i++) {
        if (y + i < 0 || y + i >= h) continue;
        line = data + (y + i) * wpl;
        lines = datas + i * wpls;
        linem = datam + i * wplm;
        for (j = 0; j < wmin; j++) {
            if (x + j < 0 || x + j >= w) continue;
            if (GET_DATA_BIT(linem, j)) {
                switch (d)
                {
                case 8:
                    val = GET_DATA_BYTE(lines, j);
                    SET_DATA_BYTE(line, x + j, val);
                    break;
                case 32:
                    *(line + x + j) = *(lines + j);
                    break;
                default:
                    return ERROR_INT("shouldn't get here", procName, 1);
                }
            }
        }
    }

    return 0;
}
    

/*!
 *  pixPaintSelfThroughMask()
 *
 *      Input:  pixd (8 bpp gray or 32 bpp rgb; not colormapped)
 *              pixm (1 bpp mask)
 *              x, y (origin of pixm relative to pixd; must not be negative)
 *              tilesize (requested size for tiling)
 *              searchdir (L_HORIZ, L_VERT)
 *      Return: 0 if OK; 1 on error
 *
 *  Notes:
 *      (1) In-place operation; pixd is changed.
 *      (2) If pixm == NULL, it's a no-op.
 *      (3) The mask origin is placed at (x,y) on pixd, and the
 *          operation is clipped to the intersection of pixd and the
 *          fg of the mask.
 *      (4) The tilesize is the the requested size for tiling.  The
 *          actual size for each c.c. will be bounded by the minimum
 *          dimension of the c.c. and the distance at which the tile
 *          center is located.
 *      (5) searchdir is the direction with respect to the b.b. of each
 *          mask component, from which the square patch is chosen and
 *          tiled onto the image, clipped by the mask component.
 *      (6) Specifically, a mirrored tiling, generated from pixd,
 *          is used to construct the pixels that are painted onto
 *          pixd through pixm.
 */
l_int32
pixPaintSelfThroughMask(PIX      *pixd,
                        PIX      *pixm,
                        l_int32   x,
                        l_int32   y,
                        l_int32   tilesize,
                        l_int32   searchdir)
{
l_int32   w, h, d, wm, hm, dm, i, n, xc, yc, bx, by, bw, bh;
l_int32   depth, cctilesize;
l_uint32  dist, minside, retval;
BOX      *box, *boxt;
BOXA     *boxa;
PIX      *pix, *pixf, *pixdf, *pixt, *pixc;
PIXA     *pixa;

    PROCNAME("pixPaintSelfThroughMask");

    if (!pixm)  /* nothing to do */
        return 0;
    if (!pixd)
        return ERROR_INT("pixd not defined", procName, 1);
    if (pixGetColormap(pixd) != NULL)
        return ERROR_INT("pixd has colormap", procName, 1);
    pixGetDimensions(pixd, &w, &h, &d);
    if (d != 8 && d != 32)
        return ERROR_INT("pixd not 8 or 32 bpp", procName, 1);
    pixGetDimensions(pixm, &wm, &hm, &dm);
    if (dm != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);
    if (x < 0 || y < 0)
        return ERROR_INT("x and y must be non-negative", procName, 1);
    if (tilesize < 1)
        return ERROR_INT("tilesize must be >= 1", procName, 1);
    if (searchdir != L_HORIZ && searchdir != L_VERT)
        return ERROR_INT("searchdir not in {L_HORIZ, L_VERT}", procName, 1);

        /* Embed mask in full sized mask */
    if (wm < w || hm < h) {
        pixf = pixCreate(w, h, 1);
        pixRasterop(pixf, x, y, wm, hm, PIX_SRC, pixm, 0, 0);
    }
    else
        pixf = pixClone(pixm);

        /* Get connected components of mask */
    boxa = pixConnComp(pixf, &pixa, 8);
    if ((n = pixaGetCount(pixa)) == 0) {
        L_WARNING("no fg in mask", procName);
        pixDestroy(&pixf);
        pixaDestroy(&pixa);
        boxaDestroy(&boxa);
        return 1;
    }

        /* Get distance function for the mask */
    pixInvert(pixf, pixf);
    depth = (tilesize < 256) ? 8 : 16;
    pixdf = pixDistanceFunction(pixf, 4, depth, L_BOUNDARY_BG);
    pixDestroy(&pixf);

        /* For each c.c., generate a representative tile for texturizing
         * and apply it through the mask.  The input 'tilesize' is the
         * requested value.  findTilePatchCenter() returns the distance
         * at which this patch can safely be found. */
    retval = 0;
    for (i = 0; i < n; i++) {
        pix = pixaGetPix(pixa, i, L_CLONE);
        box = pixaGetBox(pixa, i, L_CLONE);
        boxGetGeometry(box, &bx, &by, &bw, &bh);
	minside = L_MIN(bw, bh);

        findTilePatchCenter(pixdf, box, searchdir, L_MIN(minside, tilesize),
                            &dist, &xc, &yc);
        cctilesize = L_MIN(tilesize, dist);  /* for this c.c. */
        if (cctilesize < 1) {
            L_WARNING("region not found!", procName);
            pixDestroy(&pix);
            boxDestroy(&box);
            retval = 1;
            continue;
        }

            /* Extract the selected square from pixd, and generate 
             * an image the size of the b.b. of the c.c., which
             * is then painted through the c.c. mask.  */
        boxt = boxCreate(L_MAX(0, xc - dist / 2), L_MAX(0, yc - dist / 2),
                         cctilesize, cctilesize);
        pixt = pixClipRectangle(pixd, boxt, NULL);
        pixc = pixMirroredTiling(pixt, bw, bh);
        pixCombineThroughMask(pixd, pixc, pix, bx, by);
        pixDestroy(&pix);
        pixDestroy(&pixt);
        pixDestroy(&pixc);
        boxDestroy(&box);
        boxDestroy(&boxt);
    }

    pixDestroy(&pixdf);
    pixaDestroy(&pixa);
    boxaDestroy(&boxa);
    return retval;
}


/*-------------------------------------------------------------*
 *    One and two-image boolean ops on arbitrary depth images  *
 *-------------------------------------------------------------*/
/*!
 *  pixInvert()
 *
 *      Input:  pixd  (<optional>; this can be null, equal to pixs,
 *                     or different from pixs)
 *              pixs
 *      Return: pixd, or null on error
 *
 *  Notes:
 *      (1) This inverts pixs, for all pixel depths.
 *      (2) There are 3 cases:
 *           (a) pixd == null,   ~src --> new pixd
 *           (b) pixd == pixs,   ~src --> src  (in-place)
 *           (c) pixd != pixs,   ~src --> input pixd
 *      (3) For clarity, if the case is known, use these patterns:
 *           (a) pixd = pixInvert(NULL, pixs);
 *           (b) pixInvert(pixs, pixs);
 *           (c) pixInvert(pixd, pixs);
 */
PIX *
pixInvert(PIX  *pixd,
          PIX  *pixs)
{
    PROCNAME("pixInvert");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);

        /* Prepare pixd for in-place operation */
    if ((pixd = pixCopy(pixd, pixs)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);

    pixRasterop(pixd, 0, 0, pixGetWidth(pixd), pixGetHeight(pixd),
                PIX_NOT(PIX_DST), NULL, 0, 0);   /* invert pixd */

    return pixd;
}


/*!
 *  pixOr()
 *
 *      Input:  pixd  (<optional>; this can be null, equal to pixs1,
 *                     different from pixs1)
 *              pixs1 (can be == pixd)
 *              pixs2
 *      Return: pixd, or null on error
 *
 *  Notes:
 *      (1) This gives the union of two images with equal depth,
 *          aligning them to the the UL corner.  pixs1 and pixs2
 *          need not have the same width and height.
 *      (2) There are 3 cases:
 *            (a) pixd == null,   (src1 | src2) --> new pixd
 *            (b) pixd == pixs1,  (src1 | src2) --> src1  (in-place)
 *            (c) pixd != pixs1,  (src1 | src2) --> input pixd
 *      (3) For clarity, if the case is known, use these patterns:
 *            (a) pixd = pixOr(NULL, pixs1, pixs2);
 *            (b) pixOr(pixs1, pixs1, pixs2);
 *            (c) pixOr(pixd, pixs1, pixs2);
 *      (4) The size of the result is determined by pixs1.
 *      (5) Note carefully that the order of pixs1 and pixs2 only matters
 *          for the in-place case.  For in-place, you must have
 *          pixd == pixs1.  Setting pixd == pixs2 gives an incorrect
 *          result: the copy puts pixs1 image data in pixs2, and
 *          the rasterop is then between pixs2 and pixs2 (a no-op).
 */
PIX *
pixOr(PIX  *pixd,
      PIX  *pixs1,
      PIX  *pixs2)
{
    PROCNAME("pixOr");

    if (!pixs1)
        return (PIX *)ERROR_PTR("pixs1 not defined", procName, NULL);
    if (!pixs2)
        return (PIX *)ERROR_PTR("pixs2 not defined", procName, NULL);

#if  EQUAL_SIZE_WARNING
    if (!pixSizesEqual(pixs1, pixs2))
        L_WARNING("pixs1 and pixs2 not equal sizes", procName);
#endif  /* EQUAL_SIZE_WARNING */

        /* Prepare pixd to be a copy of pixs1 */
    if ((pixd = pixCopy(pixd, pixs1)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);

        /* src1 | src2 --> dest */
    pixRasterop(pixd, 0, 0, pixGetWidth(pixd), pixGetHeight(pixd),
                PIX_SRC | PIX_DST, pixs2, 0, 0);

    return pixd;
}


/*!
 *  pixAnd()
 *
 *      Input:  pixd  (<optional>; this can be null, equal to pixs1,
 *                     different from pixs1)
 *              pixs1 (can be == pixd)
 *              pixs2
 *      Return: pixd, or null on error
 *
 *  Notes:
 *      (1) This gives the intersection of two images with equal depth,
 *          aligning them to the the UL corner.  pixs1 and pixs2 
 *          need not have the same width and height.
 *      (2) There are 3 cases:
 *            (a) pixd == null,   (src1 & src2) --> new pixd
 *            (b) pixd == pixs1,  (src1 & src2) --> src1  (in-place)
 *            (c) pixd != pixs1,  (src1 & src2) --> input pixd
 *      (3) For clarity, if the case is known, use these patterns:
 *            (a) pixd = pixAnd(NULL, pixs1, pixs2);
 *            (b) pixAnd(pixs1, pixs1, pixs2);
 *            (c) pixAnd(pixd, pixs1, pixs2);
 *      (4) The size of the result is determined by pixs1.
 *      (5) Note carefully that the order of pixs1 and pixs2 only matters
 *          for the in-place case.  For in-place, you must have
 *          pixd == pixs1.  Setting pixd == pixs2 gives an incorrect
 *          result: the copy puts pixs1 image data in pixs2, and
 *          the rasterop is then between pixs2 and pixs2 (a no-op).
 */
PIX *
pixAnd(PIX  *pixd,
       PIX  *pixs1,
       PIX  *pixs2)
{
    PROCNAME("pixAnd");

    if (!pixs1)
        return (PIX *)ERROR_PTR("pixs1 not defined", procName, NULL);
    if (!pixs2)
        return (PIX *)ERROR_PTR("pixs2 not defined", procName, NULL);

#if  EQUAL_SIZE_WARNING
    if (!pixSizesEqual(pixs1, pixs2))
        L_WARNING("pixs1 and pixs2 not equal sizes", procName);
#endif  /* EQUAL_SIZE_WARNING */

        /* Prepare pixd to be a copy of pixs1 */
    if ((pixd = pixCopy(pixd, pixs1)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);

        /* src1 & src2 --> dest */
    pixRasterop(pixd, 0, 0, pixGetWidth(pixd), pixGetHeight(pixd),
                PIX_SRC & PIX_DST, pixs2, 0, 0);

    return pixd;
}


/*!
 *  pixXor()
 *
 *      Input:  pixd  (<optional>; this can be null, equal to pixs1,
 *                     different from pixs1)
 *              pixs1 (can be == pixd)
 *              pixs2
 *      Return: pixd, or null on error
 *
 *  Notes:
 *      (1) This gives the XOR of two images with equal depth,
 *          aligning them to the the UL corner.  pixs1 and pixs2
 *          need not have the same width and height.
 *      (2) There are 3 cases:
 *            (a) pixd == null,   (src1 ^ src2) --> new pixd
 *            (b) pixd == pixs1,  (src1 ^ src2) --> src1  (in-place)
 *            (c) pixd != pixs1,  (src1 ^ src2) --> input pixd
 *      (3) For clarity, if the case is known, use these patterns:
 *            (a) pixd = pixXor(NULL, pixs1, pixs2);
 *            (b) pixXor(pixs1, pixs1, pixs2);
 *            (c) pixXor(pixd, pixs1, pixs2);
 *      (4) The size of the result is determined by pixs1.
 *      (5) Note carefully that the order of pixs1 and pixs2 only matters
 *          for the in-place case.  For in-place, you must have
 *          pixd == pixs1.  Setting pixd == pixs2 gives an incorrect
 *          result: the copy puts pixs1 image data in pixs2, and
 *          the rasterop is then between pixs2 and pixs2 (a no-op).
 */
PIX *
pixXor(PIX  *pixd,
       PIX  *pixs1,
       PIX  *pixs2)
{
    PROCNAME("pixXor");

    if (!pixs1)
        return (PIX *)ERROR_PTR("pixs1 not defined", procName, NULL);
    if (!pixs2)
        return (PIX *)ERROR_PTR("pixs2 not defined", procName, NULL);

#if  EQUAL_SIZE_WARNING
    if (!pixSizesEqual(pixs1, pixs2))
        L_WARNING("pixs1 and pixs2 not equal sizes", procName);
#endif  /* EQUAL_SIZE_WARNING */

        /* Prepare pixd to be a copy of pixs1 */
    if ((pixd = pixCopy(pixd, pixs1)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);

        /* src1 ^ src2 --> dest */
    pixRasterop(pixd, 0, 0, pixGetWidth(pixd), pixGetHeight(pixd),
                PIX_SRC ^ PIX_DST, pixs2, 0, 0);

    return pixd;
}


/*!
 *  pixSubtract()
 *
 *      Input:  pixd  (<optional>; this can be null, equal to pixs1,
 *                     equal to pixs2, or different from both pixs1 and pixs2)
 *              pixs1 (can be == pixd)
 *              pixs2 (can be == pixd)
 *      Return: pixd, or null on error
 *
 *  Notes:
 *      (1) This gives the set subtraction of two images with equal depth,
 *          aligning them to the the UL corner.  pixs1 and pixs2
 *          need not have the same width and height.
 *      (2) Source pixs2 is always subtracted from source pixs1.
 *          The result is
 *                  pixs1 \ pixs2 = pixs1 & (~pixs2)
 *      (3) There are 4 cases:
 *            (a) pixd == null,   (src1 - src2) --> new pixd
 *            (b) pixd == pixs1,  (src1 - src2) --> src1  (in-place)
 *            (c) pixd == pixs2,  (src1 - src2) --> src2  (in-place)
 *            (d) pixd != pixs1 && pixd != pixs2),
 *                                 (src1 - src2) --> input pixd
 *      (4) For clarity, if the case is known, use these patterns:
 *            (a) pixd = pixSubtract(NULL, pixs1, pixs2);
 *            (b) pixSubtract(pixs1, pixs1, pixs2);
 *            (c) pixSubtract(pixs2, pixs1, pixs2);
 *            (d) pixSubtract(pixd, pixs1, pixs2);
 *      (5) The size of the result is determined by pixs1.
 */
PIX *
pixSubtract(PIX  *pixd,
            PIX  *pixs1,
            PIX  *pixs2)
{
l_int32  w, h;

    PROCNAME("pixSubtract");

    if (!pixs1)
        return (PIX *)ERROR_PTR("pixs1 not defined", procName, NULL);
    if (!pixs2)
        return (PIX *)ERROR_PTR("pixs2 not defined", procName, NULL);

#if  EQUAL_SIZE_WARNING
    if (!pixSizesEqual(pixs1, pixs2))
        L_WARNING("pixs1 and pixs2 not equal sizes", procName);
#endif  /* EQUAL_SIZE_WARNING */

    pixGetDimensions(pixs1, &w, &h, NULL);
    if (!pixd) {
        pixd = pixCopy(NULL, pixs1);
        pixRasterop(pixd, 0, 0, w, h, PIX_DST & PIX_NOT(PIX_SRC),
            pixs2, 0, 0);   /* src1 & (~src2)  */
    }
    else if (pixd == pixs1) {
        pixRasterop(pixd, 0, 0, w, h, PIX_DST & PIX_NOT(PIX_SRC),
            pixs2, 0, 0);   /* src1 & (~src2)  */
    }
    else if (pixd == pixs2) {
        pixRasterop(pixd, 0, 0, w, h, PIX_NOT(PIX_DST) & PIX_SRC,
            pixs1, 0, 0);   /* src1 & (~src2)  */
    }
    else  { /* pixd != pixs1 && pixd != pixs2 */
        pixCopy(pixd, pixs1);  /* sizes pixd to pixs1 if unequal */
        pixRasterop(pixd, 0, 0, w, h, PIX_DST & PIX_NOT(PIX_SRC),
            pixs2, 0, 0);   /* src1 & (~src2)  */
    }

    return pixd;
}


/*-------------------------------------------------------------*
 *                         Pixel counting                      *
 *-------------------------------------------------------------*/
/*!
 *  pixZero()
 *
 *      Input:  pix
 *              &empty  (<return> 1 if all bits in image are 0; 0 otherwise)
 *      Return: 0 if OK; 1 on error
 *
 *  Notes:
 *      (1) For a binary image, if there are no fg (black) pixels, empty = 1.
 *      (2) For a grayscale image, if all pixels are black (0), empty = 1.
 *      (3) For an RGB image, if all 4 components in every pixel is 0,
 *          empty = 1. 
 */
l_int32
pixZero(PIX      *pix,
        l_int32  *pempty)
{
l_int32    w, h, wpl, i, j, fullwords, endbits;
l_uint32   endmask;
l_uint32  *data, *line;

    PROCNAME("pixZero");

    if (!pix)
        return ERROR_INT("pix not defined", procName, 1);
    if (!pempty)
        return ERROR_INT("pempty not defined", procName, 1);

    w = pixGetWidth(pix) * pixGetDepth(pix);
    h = pixGetHeight(pix);
    wpl = pixGetWpl(pix);
    data = pixGetData(pix);

    fullwords = w / 32;
    endbits = w & 31;
    endmask = 0xffffffff << (32 - endbits);

    *pempty = 1;
    for (i = 0; i < h; i++) {
        line = data + wpl * i;
        for (j = 0; j < fullwords; j++)
            if (*line++) {
                *pempty = 0;
                return 0;
            }
        if (endbits) {
            if (*line & endmask) {
                *pempty = 0;
                return 0;
            }
        }
    }

    return 0;
}


/*!
 *  pixCountPixels()
 *
 *      Input:  binary pix
 *              &count (<return> count of ON pixels)
 *              tab8  (<optional> 8-bit pixel lookup table)
 *      Return: 0 if OK; 1 on error
 */
l_int32
pixCountPixels(PIX      *pix,
               l_int32  *pcount,
               l_int32  *tab8)
{
l_uint32   endmask;
l_int32    w, h, wpl, i, j;
l_int32    fullwords, endbits, sum;
l_int32   *tab;
l_uint32  *data;

    PROCNAME("pixCountPixels");

    if (!pix)
        return ERROR_INT("pix not defined", procName, 1);
    if (!pcount)
        return ERROR_INT("pcount not defined", procName, 1);
    if (pixGetDepth(pix) != 1)
        return ERROR_INT("pix not 1 bpp", procName, 1);

    *pcount = 0;

    if (!tab8)
        tab = makePixelSumTab8();
    else
        tab = tab8;

    w = pixGetWidth(pix);
    h = pixGetHeight(pix);
    wpl = pixGetWpl(pix);
    data = pixGetData(pix);

    fullwords = w >> 5;
    endbits = w & 31;
    endmask = 0xffffffff << (32 - endbits);

    sum = 0;
    for (i = 0; i < h; i++, data += wpl) {
        for (j = 0; j < fullwords; j++) {
            l_uint32 word = data[j];
            if (word) {
                sum += tab[word & 0xff] +
                       tab[(word >> 8) & 0xff] +
                       tab[(word >> 16) & 0xff] +
                       tab[(word >> 24) & 0xff];
            }
        }
        if (endbits) {
            l_uint32 word = data[j] & endmask;
            if (word) {
                sum += tab[word & 0xff] +
                       tab[(word >> 8) & 0xff] +
                       tab[(word >> 16) & 0xff] +
                       tab[(word >> 24) & 0xff];
            }
        }
    }
    *pcount = sum;

    if (!tab8)
        FREE(tab);
    return 0;
}


/*!
 *  pixaCountPixels()
 *
 *      Input:  pixa (array of binary pix)
 *      Return: na of ON pixels in each pix, or null on error
 */
NUMA *
pixaCountPixels(PIXA  *pixa)
{
l_int32   d, i, n, count;
l_int32  *tab;
NUMA     *na;
PIX      *pix;

    PROCNAME("pixaCountPixels");

    if (!pixa)
        return (NUMA *)ERROR_PTR("pix not defined", procName, NULL);

    if ((n = pixaGetCount(pixa)) == 0)
        return numaCreate(1);

    pix = pixaGetPix(pixa, 0, L_CLONE);
    d = pixGetDepth(pix);
    pixDestroy(&pix);
    if (d != 1)
        return (NUMA *)ERROR_PTR("pixa not 1 bpp", procName, NULL);

    tab = makePixelSumTab8();
    if ((na = numaCreate(n)) == NULL)
        return (NUMA *)ERROR_PTR("na not made", procName, NULL);
    for (i = 0; i < n; i++) {
        pix = pixaGetPix(pixa, i, L_CLONE);
        pixCountPixels(pix, &count, tab);
        numaAddNumber(na, count);
        pixDestroy(&pix);
    }
        
    FREE(tab);
    return na;
}


/*!
 *  pixCountPixelsInRow()
 *
 *      Input:  binary pix
 *              row number
 *              &count (<return> sum of ON pixels in raster line)
 *              tab8  (<optional> 8-bit pixel lookup table)
 *      Return: 0 if OK; 1 on error
 */
l_int32
pixCountPixelsInRow(PIX      *pix,
                    l_int32   row,
                    l_int32  *pcount,
                    l_int32  *tab8)
{
l_uint32   word, endmask;
l_int32    j, w, h, wpl;
l_int32    fullwords, endbits, sum;
l_int32   *tab;
l_uint32  *line;

    PROCNAME("pixCountPixelsInRow");

    if (!pix)
        return ERROR_INT("pix not defined", procName, 1);
    if (!pcount)
        return ERROR_INT("pcount not defined", procName, 1);
    if (pixGetDepth(pix) != 1)
        return ERROR_INT("pix not 1 bpp", procName, 1);

    *pcount = 0;

    w = pixGetWidth(pix);
    h = pixGetHeight(pix);
    if (row < 0 || row >= h)
        return ERROR_INT("row out of bounds", procName, 1);
    wpl = pixGetWpl(pix);
    line = pixGetData(pix) + row * wpl;

    fullwords = w >> 5;
    endbits = w & 31;
    endmask = 0xffffffff << (32 - endbits);

    if (!tab8)
        tab = makePixelSumTab8();
    else
        tab = tab8;

    sum = 0;
    for (j = 0; j < fullwords; j++) {
        word = line[j];
        if (word) {
            sum += tab[word & 0xff] +
                   tab[(word >> 8) & 0xff] +
                   tab[(word >> 16) & 0xff] +
                   tab[(word >> 24) & 0xff];
        }
    }
    if (endbits) {
        word = line[j] & endmask;
        if (word) {
            sum += tab[word & 0xff] +
                   tab[(word >> 8) & 0xff] +
                   tab[(word >> 16) & 0xff] +
                   tab[(word >> 24) & 0xff];
        }
    }
    *pcount = sum;

    if (!tab8)
        FREE(tab);
    return 0;
}


/*!
 *  pixCountPixelsByRow()
 *
 *      Input:  binary pix
 *              tab8  (<optional> 8-bit pixel lookup table)
 *      Return: na of counts, or null on error
 */
NUMA *
pixCountPixelsByRow(PIX      *pix,
                    l_int32  *tab8)
{
l_int32   w, h, i, count;
l_int32  *tab;
NUMA     *na;

    PROCNAME("pixCountPixelsByRow");

    if (!pix)
        return (NUMA *)ERROR_PTR("pix not defined", procName, NULL);
    if (pixGetDepth(pix) != 1)
        return (NUMA *)ERROR_PTR("pix not 1 bpp", procName, NULL);

    w = pixGetWidth(pix);
    h = pixGetHeight(pix);

    if (!tab8)
        tab = makePixelSumTab8();
    else
        tab = tab8;

    if ((na = numaCreate(h)) == NULL)
        return (NUMA *)ERROR_PTR("na not made", procName, NULL);

    for (i = 0; i < h; i++) {
        pixCountPixelsInRow(pix, i, &count, tab);
        numaAddNumber(na, count);
    }

    if (!tab8)
        FREE(tab);

    return na;
}


/*!
 *  pixThresholdPixels()
 *
 *      Input:  binary pix
 *              threshold
 *              &above (<return> 1 if above threshold;
 *                               0 if equal to or less than threshold)
 *              tab8  (<optional> 8-bit pixel lookup table)
 *      Return: 0 if OK; 1 on error
 *
 *  Notes:
 *      (1) This sums the ON pixels and returns immediately if the count
 *          goes above threshold.  It is therefore more efficient
 *          for matching images (by running this function on the xor of
 *          the 2 images) than using pixCountPixels(), which counts all
 *          pixels before returning.
 */
l_int32
pixThresholdPixels(PIX      *pix,
                   l_int32   thresh,
                   l_int32  *pabove,
                   l_int32  *tab8)
{
l_uint32   word, endmask;
l_int32   *tab;
l_int32    w, h, wpl, i, j;
l_int32    fullwords, endbits, sum;
l_uint32  *line, *data;

    PROCNAME("pixThresholdPixels");

    if (!pix)
        return ERROR_INT("pix not defined", procName, 1);
    if (pixGetDepth(pix) != 1)
        return ERROR_INT("pix not 1 bpp", procName, 1);
    if (!pabove)
        return ERROR_INT("pabove not defined", procName, 1);
    *pabove = 0;  /* init */

    if (!tab8)
        tab = makePixelSumTab8();
    else
        tab = tab8;

    w = pixGetWidth(pix);
    h = pixGetHeight(pix);
    wpl = pixGetWpl(pix);
    data = pixGetData(pix);

    fullwords = w >> 5;
    endbits = w & 31;
    endmask = 0xffffffff << (32 - endbits);

    sum = 0;
    for (i = 0; i < h; i++) {
        line = data + wpl * i;
        for (j = 0; j < fullwords; j++) {
            word = line[j];
            if (word) {
                sum += tab[word & 0xff] +
                       tab[(word >> 8) & 0xff] +
                       tab[(word >> 16) & 0xff] +
                       tab[(word >> 24) & 0xff];
            }
        }
        if (endbits) {
            word = line[j] & endmask;
            if (word) {
                sum += tab[word & 0xff] +
                       tab[(word >> 8) & 0xff] +
                       tab[(word >> 16) & 0xff] +
                       tab[(word >> 24) & 0xff];
            }
        }
        if (sum > thresh) {
            *pabove = 1;
            if (!tab8)
                FREE(tab);
            return 0;
        }
    }

    if (!tab8)
        FREE(tab);
    return 0;
}


/*!
 *  makePixelSumTab8()
 *
 *      Input:  void
 *      Return: table of 256 l_int32, or null on error
 *
 *  Notes:
 *      (1) This table of integers gives the number of 1 bits
 *          in the 8 bit index.
 */
l_int32 *
makePixelSumTab8(void)
{
l_uint8   byte;
l_int32   i;
l_int32  *tab;

    PROCNAME("makePixelSumTab8");

    if ((tab = (l_int32 *)CALLOC(256, sizeof(l_int32))) == NULL)
        return (l_int32 *)ERROR_PTR("tab not made", procName, NULL);

    for (i = 0; i < 256; i++) {
        byte = (l_uint8)i;
        tab[i] = (byte & 0x1) +
                 ((byte >> 1) & 0x1) +
                 ((byte >> 2) & 0x1) +
                 ((byte >> 3) & 0x1) +
                 ((byte >> 4) & 0x1) +
                 ((byte >> 5) & 0x1) +
                 ((byte >> 6) & 0x1) +
                 ((byte >> 7) & 0x1);
    }

    return tab;
}


/*!
 *  makePixelCentroidTab8()
 *
 *      Input:  void
 *      Return: table of 256 l_int32, or null on error
 *
 *  Notes:
 *      (1) This table of integers gives the centroid weight of the 1 bits
 *          in the 8 bit index.  In other words, if sumtab is obtained by
 *          makePixelSumTab8, and centroidtab is obtained by
 *          makePixelCentroidTab8, then, for 1 <= i <= 255,
 *          centroidtab[i] / (float)sumtab[i]
 *          is the centroid of the 1 bits in the 8-bit index i, where the
 *          MSB is considered to have position 0 and the LSB is considered
 *          to have position 7.
 */
l_int32 *
makePixelCentroidTab8(void)
{
l_int32   i;
l_int32  *tab;

    PROCNAME("makePixelCentroidTab8");

    if ((tab = (l_int32 *)CALLOC(256, sizeof(l_int32))) == NULL)
        return (l_int32 *)ERROR_PTR("tab not made", procName, NULL);

    tab[0] = 0;
    tab[1] = 7;
    for (i = 2; i < 4; i++) {
        tab[i] = tab[i - 2] + 6;
    }
    for (i = 4; i < 8; i++) {
        tab[i] = tab[i - 4] + 5;
    }
    for (i = 8; i < 16; i++) {
        tab[i] = tab[i - 8] + 4;
    }
    for (i = 16; i < 32; i++) {
        tab[i] = tab[i - 16] + 3;
    }
    for (i = 32; i < 64; i++) {
        tab[i] = tab[i - 32] + 2;
    }
    for (i = 64; i < 128; i++) {
        tab[i] = tab[i - 64] + 1;
    }
    for (i = 128; i < 256; i++) {
        tab[i] = tab[i - 128];
    }

    return tab;
}


/*------------------------------------------------------------------*
 *                  Pixel histogram and averaging                   *
 *------------------------------------------------------------------*/
/*!
 *  pixGetGrayHistogram()
 *
 *      Input:  pixs (1, 2, 4, 8, 16 bpp; can be colormapped)
 *              factor (subsampling factor; integer >= 1)
 *      Return: na (histogram), or null on error
 *
 *  Notes:
 *      (1) This generates a histogram of gray or cmapped pixels.
 *          The image must not be rgb.
 *      (2) If pixs does not have a colormap, the output histogram is
 *          of size 2^d, where d is the depth of pixs.
 *      (3) If pixs has has a colormap with color entries, the histogram
 *          generated is of the colormap indices, and is of size 2^d.
 *      (4) If pixs has a gray (r=g=b) colormap, the colormap is removed
 *          and a histogram of size 256 is generated for the resulting
 *          8 bpp gray image.
 *      (5) Set the subsampling factor > 1 to reduce the amount of computation.
 */
NUMA *
pixGetGrayHistogram(PIX     *pixs,
                    l_int32  factor)
{
l_int32     i, j, w, h, d, wpl, val, size, count, colorfound;
l_uint32   *data, *line;
l_float32  *array;
NUMA       *na;
PIX        *pixg;
PIXCMAP    *cmap;

    PROCNAME("pixGetGrayHistogram");

    if (!pixs)
        return (NUMA *)ERROR_PTR("pixs not defined", procName, NULL);
    d = pixGetDepth(pixs);
    if (d > 16)
        return (NUMA *)ERROR_PTR("depth not in {1,2,4,8,16}", procName, NULL);
    if (factor < 1)
        return (NUMA *)ERROR_PTR("sampling factor < 1", procName, NULL);

    if ((cmap = pixGetColormap(pixs)) != NULL)
        pixcmapHasColor(cmap, &colorfound);
    if (cmap && !colorfound)
        pixg = pixRemoveColormap(pixs, REMOVE_CMAP_TO_GRAYSCALE);
    else
        pixg = pixClone(pixs);

    pixGetDimensions(pixg, &w, &h, &d);
    size = 1 << d;
    if ((na = numaCreate(size)) == NULL)
        return (NUMA *)ERROR_PTR("na not made", procName, NULL);
    numaSetCount(na, size);  /* all initialized to 0.0 */
    array = numaGetFArray(na, L_NOCOPY);

    if (d == 1) {  /* special case */
        pixCountPixels(pixg, &count, NULL);
        array[0] = w * h - count;
        array[1] = count;
        pixDestroy(&pixg);
        return na;
    }

    wpl = pixGetWpl(pixg);
    data = pixGetData(pixg);
    for (i = 0; i < h; i += factor) {
        line = data + i * wpl;
        switch (d) 
        {
        case 2:
            for (j = 0; j < w; j += factor) {
                val = GET_DATA_DIBIT(line, j);
                array[val] += 1.0;
            }
            break;
        case 4:
            for (j = 0; j < w; j += factor) {
                val = GET_DATA_QBIT(line, j);
                array[val] += 1.0;
            }
            break;
        case 8:
            for (j = 0; j < w; j += factor) {
                val = GET_DATA_BYTE(line, j);
                array[val] += 1.0;
            }
            break;
        case 16:
            for (j = 0; j < w; j += factor) {
                val = GET_DATA_TWO_BYTES(line, j);
                array[val] += 1.0;
            }
            break;
        default:
            numaDestroy(&na);
            return (NUMA *)ERROR_PTR("illegal depth", procName, NULL);
        }
    }

    pixDestroy(&pixg);
    return na;
}


/*!
 *  pixGetGrayHistogramMasked()
 *
 *      Input:  pixs (8 bpp, or colormapped)
 *              pixm (<optional> 1 bpp mask over which histogram is
 *                    to be computed; use use all pixels if null)
 *              x, y (UL corner of pixm relative to the UL corner of pixs; 
 *                    can be < 0; these values are ignored if pixm is null)
 *              factor (subsampling factor; integer >= 1)
 *      Return: na (histogram), or null on error
 *
 *  Notes:
 *      (1) If pixs is cmapped, it is converted to 8 bpp gray.
 *      (2) This always returns a 256-value histogram of pixel values.
 *      (3) Set the subsampling factor > 1 to reduce the amount of computation.
 *      (4) Clipping of pixm (if it exists) to pixs is done in the inner loop.
 *      (5) Input x,y are ignored unless pixm exists.
 */
NUMA *
pixGetGrayHistogramMasked(PIX        *pixs,
                          PIX        *pixm,
                          l_int32     x,
                          l_int32     y,
                          l_int32     factor)
{
l_int32     i, j, w, h, wm, hm, dm, wplg, wplm, val;
l_uint32   *datag, *datam, *lineg, *linem;
l_float32  *array;
NUMA       *na;
PIX        *pixg;

    PROCNAME("pixGetGrayHistogramMasked");

    if (!pixm)
        return pixGetGrayHistogram(pixs, factor);

    if (!pixs)
        return (NUMA *)ERROR_PTR("pixs not defined", procName, NULL);
    if (pixGetDepth(pixs) != 8 && !pixGetColormap(pixs))
        return (NUMA *)ERROR_PTR("pixs neither 8 bpp nor colormapped",
                                 procName, NULL);
    pixGetDimensions(pixm, &wm, &hm, &dm);
    if (dm != 1)
        return (NUMA *)ERROR_PTR("pixm not 1 bpp", procName, NULL);
    if (factor < 1)
        return (NUMA *)ERROR_PTR("sampling factor < 1", procName, NULL);

    if ((na = numaCreate(256)) == NULL)
        return (NUMA *)ERROR_PTR("na not made", procName, NULL);
    numaSetCount(na, 256);  /* all initialized to 0.0 */
    array = numaGetFArray(na, L_NOCOPY);

    if (pixGetColormap(pixs))
        pixg = pixRemoveColormap(pixs, REMOVE_CMAP_TO_GRAYSCALE);
    else
        pixg = pixClone(pixs);
    pixGetDimensions(pixg, &w, &h, NULL);
    datag = pixGetData(pixg);
    wplg = pixGetWpl(pixg);
    datam = pixGetData(pixm);
    wplm = pixGetWpl(pixm);

        /* Generate the histogram */
    for (i = 0; i < hm; i += factor) {
        if (y + i < 0 || y + i >= h) continue;
        lineg = datag + (y + i) * wplg;
        linem = datam + i * wplm;
        for (j = 0; j < wm; j += factor) {
            if (x + j < 0 || x + j >= w) continue;
            if (GET_DATA_BIT(linem, j)) {
                val = GET_DATA_BYTE(lineg, x + j);
                array[val] += 1.0;
            }
        }
    }

    pixDestroy(&pixg);
    return na;
}


/*!
 *  pixGetColorHistogram()
 *
 *      Input:  pixs (rgb or colormapped)
 *              factor (subsampling factor; integer >= 1)
 *              &nar (<return> red histogram)
 *              &nag (<return> green histogram)
 *              &nab (<return> blue histogram)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) This generates a set of three 256 entry histograms,
 *          one for each color component (r,g,b).
 *      (2) Set the subsampling factor > 1 to reduce the amount of computation.
 */
l_int32
pixGetColorHistogram(PIX     *pixs,
                     l_int32  factor,
                     NUMA   **pnar,
                     NUMA   **pnag,
                     NUMA   **pnab)
{
l_int32     i, j, w, h, d, wpl, index, rval, gval, bval;
l_uint32    pixel;
l_uint32   *data, *line;
l_float32  *rarray, *garray, *barray;
NUMA       *nar, *nag, *nab;
PIXCMAP    *cmap;

    PROCNAME("pixGetColorHistogram");

    if (!pnar || !pnag || !pnab)
        return ERROR_INT("&nar, &nag, &nab not all defined", procName, 1);
    *pnar = *pnag = *pnab = NULL;
    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    pixGetDimensions(pixs, &w, &h, &d);
    cmap = pixGetColormap(pixs);
    if (cmap && (d != 2 && d != 4 && d != 8))
        return ERROR_INT("colormap and not 2, 4, or 8 bpp", procName, 1);
    if (!cmap && d != 32)
        return ERROR_INT("no colormap and not rgb", procName, 1);
    if (factor < 1)
        return ERROR_INT("sampling factor < 1", procName, 1);

        /* Set up the histogram arrays */
    nar = numaCreate(256);
    nag = numaCreate(256);
    nab = numaCreate(256);
    numaSetCount(nar, 256);
    numaSetCount(nag, 256);
    numaSetCount(nab, 256);
    rarray = numaGetFArray(nar, L_NOCOPY);
    garray = numaGetFArray(nag, L_NOCOPY);
    barray = numaGetFArray(nab, L_NOCOPY);
    *pnar = nar;
    *pnag = nag;
    *pnab = nab;

        /* Generate the color histograms */
    data = pixGetData(pixs);
    wpl = pixGetWpl(pixs);
    if (cmap) {
        for (i = 0; i < h; i += factor) {
            line = data + i * wpl;
            for (j = 0; j < w; j += factor) {
                if (d == 8)
                    index = GET_DATA_BYTE(line, j);
                else if (d == 4)
                    index = GET_DATA_QBIT(line, j);
                else   /* 2 bpp */
                    index = GET_DATA_DIBIT(line, j);
                pixcmapGetColor(cmap, index, &rval, &gval, &bval);
                rarray[rval] += 1.0;
                garray[gval] += 1.0;
                barray[bval] += 1.0;
            }
        }
    }
    else {  /* 32 bpp rgb */
        for (i = 0; i < h; i += factor) {
            line = data + i * wpl;
            for (j = 0; j < w; j += factor) {
                pixel = line[j];
                rval = (pixel >> 24);
                gval = (pixel >> 16) & 0xff;
                bval = (pixel >> 8) & 0xff;
                rarray[rval] += 1.0;
                garray[gval] += 1.0;
                barray[bval] += 1.0;
            }
        }
    }

    return 0;
}
            

/*!
 *  pixGetColorHistogramMasked()
 *
 *      Input:  pixs (32 bpp rgb, or colormapped)
 *              pixm (<optional> 1 bpp mask over which histogram is
 *                    to be computed; use use all pixels if null)
 *              x, y (UL corner of pixm relative to the UL corner of pixs; 
 *                    can be < 0; these values are ignored if pixm is null)
 *              factor (subsampling factor; integer >= 1)
 *              &nar (<return> red histogram)
 *              &nag (<return> green histogram)
 *              &nab (<return> blue histogram)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) This generates a set of three 256 entry histograms,
 *      (2) Set the subsampling factor > 1 to reduce the amount of computation.
 *      (3) Clipping of pixm (if it exists) to pixs is done in the inner loop.
 *      (4) Input x,y are ignored unless pixm exists.
 */
l_int32
pixGetColorHistogramMasked(PIX        *pixs,
                           PIX        *pixm,
                           l_int32     x,
                           l_int32     y,
                           l_int32     factor,
                           NUMA      **pnar,
                           NUMA      **pnag,
                           NUMA      **pnab)
{
l_int32     i, j, w, h, d, wm, hm, dm, wpls, wplm, index, rval, gval, bval;
l_uint32    pixel;
l_uint32   *datas, *datam, *lines, *linem;
l_float32  *rarray, *garray, *barray;
NUMA       *nar, *nag, *nab;
PIXCMAP    *cmap;

    PROCNAME("pixGetColorHistogramMasked");

    if (!pixm)
        return pixGetColorHistogram(pixs, factor, pnar, pnag, pnab);

    if (!pnar || !pnag || !pnab)
        return ERROR_INT("&nar, &nag, &nab not all defined", procName, 1);
    *pnar = *pnag = *pnab = NULL;
    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    pixGetDimensions(pixs, &w, &h, &d);
    cmap = pixGetColormap(pixs);
    if (cmap && (d != 2 && d != 4 && d != 8))
        return ERROR_INT("colormap and not 2, 4, or 8 bpp", procName, 1);
    if (!cmap && d != 32)
        return ERROR_INT("no colormap and not rgb", procName, 1);
    pixGetDimensions(pixm, &wm, &hm, &dm);
    if (dm != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);
    if (factor < 1)
        return ERROR_INT("sampling factor < 1", procName, 1);

        /* Set up the histogram arrays */
    nar = numaCreate(256);
    nag = numaCreate(256);
    nab = numaCreate(256);
    numaSetCount(nar, 256);
    numaSetCount(nag, 256);
    numaSetCount(nab, 256);
    rarray = numaGetFArray(nar, L_NOCOPY);
    garray = numaGetFArray(nag, L_NOCOPY);
    barray = numaGetFArray(nab, L_NOCOPY);
    *pnar = nar;
    *pnag = nag;
    *pnab = nab;

        /* Generate the color histograms */
    datas = pixGetData(pixs);
    wpls = pixGetWpl(pixs);
    datam = pixGetData(pixm);
    wplm = pixGetWpl(pixm);
    if (cmap) {
        for (i = 0; i < hm; i += factor) {
            if (y + i < 0 || y + i >= h) continue;
            lines = datas + (y + i) * wpls;
            linem = datam + i * wplm;
            for (j = 0; j < wm; j += factor) {
                if (x + j < 0 || x + j >= w) continue;
                if (GET_DATA_BIT(linem, j)) {
                    if (d == 8)
                        index = GET_DATA_BYTE(lines, x + j);
                    else if (d == 4)
                        index = GET_DATA_QBIT(lines, x + j);
                    else   /* 2 bpp */
                        index = GET_DATA_DIBIT(lines, x + j);
                    pixcmapGetColor(cmap, index, &rval, &gval, &bval);
                    rarray[rval] += 1.0;
                    garray[gval] += 1.0;
                    barray[bval] += 1.0;
                }
            }
        }
    }
    else {  /* 32 bpp rgb */
        for (i = 0; i < hm; i += factor) {
            if (y + i < 0 || y + i >= h) continue;
            lines = datas + (y + i) * wpls;
            linem = datam + i * wplm;
            for (j = 0; j < wm; j += factor) {
                if (x + j < 0 || x + j >= w) continue;
                if (GET_DATA_BIT(linem, j)) {
                    pixel = lines[x + j];
                    rval = (pixel >> 24);
                    gval = (pixel >> 16) & 0xff;
                    bval = (pixel >> 8) & 0xff;
                    rarray[rval] += 1.0;
                    garray[gval] += 1.0;
                    barray[bval] += 1.0;
                }
            }
        }
    }

    return 0;
}


/*!
 *  pixGetRankValueMaskedRGB()
 *
 *      Input:  pixs (32 bpp)
 *              pixm (<optional> 1 bpp mask over which rank val is to be taken;
 *                    use all pixels if null)
 *              x, y (UL corner of pixm relative to the UL corner of pixs; 
 *                    can be < 0; these values are ignored if pixm is null)
 *              factor (subsampling factor; integer >= 1)
 *              rank (between 0.0 and 1.0; 1.0 is brightest, 0.0 is darkest)
 *              &rval (<optional return> red component val for to input rank)
 *              &gval (<optional return> green component val for to input rank)
 *              &bval (<optional return> blue component val for to input rank)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) Computes the rank component values of pixels in pixs that
 *          are under the fg of the optional mask.  If the mask is null, it
 *          computes the average of the pixels in pixs.
 *      (2) Set the subsampling factor > 1 to reduce the amount of
 *          computation.
 *      (4) Input x,y are ignored unless pixm exists.
 *      (5) The rank must be in [0.0 ... 1.0], where the brightest pixel
 *          has rank 1.0.  For the median pixel value, use 0.5.
 */
l_int32
pixGetRankValueMaskedRGB(PIX        *pixs,
                         PIX        *pixm,
                         l_int32     x,
                         l_int32     y,
                         l_int32     factor,
                         l_float32   rank,
                         l_float32  *prval,
                         l_float32  *pgval,
                         l_float32  *pbval)
{
l_float32  scale;
PIX       *pixmt, *pixt;

    PROCNAME("pixGetRankValueMaskedRGB");

    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    if (pixGetDepth(pixs) != 32)
        return ERROR_INT("pixs not 32 bpp", procName, 1);
    if (pixm && pixGetDepth(pixm) != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);
    if (factor < 1)
        return ERROR_INT("sampling factor < 1", procName, 1);
    if (rank < 0.0 || rank > 1.0)
        return ERROR_INT("rank not in [0.0 ... 1.0]", procName, 1);
    if (!prval && !pgval && !pbval)
        return ERROR_INT("no results requested", procName, 1);

    pixmt = NULL;
    if (pixm) {
        scale = 1.0 / (l_float32)factor;
        pixmt = pixScale(pixm, scale, scale);
    }
    if (prval) {
        pixt = pixScaleRGBToGrayFast(pixs, factor, COLOR_RED);
        pixGetRankValueMasked(pixt, pixmt, x / factor, y / factor,
                              factor, rank, prval, NULL);
        pixDestroy(&pixt);
    }
    if (pgval) {
        pixt = pixScaleRGBToGrayFast(pixs, factor, COLOR_GREEN);
        pixGetRankValueMasked(pixt, pixmt, x / factor, y / factor,
                              factor, rank, pgval, NULL);
        pixDestroy(&pixt);
    }
    if (pbval) {
        pixt = pixScaleRGBToGrayFast(pixs, factor, COLOR_BLUE);
        pixGetRankValueMasked(pixt, pixmt, x / factor, y / factor,
                              factor, rank, pbval, NULL);
        pixDestroy(&pixt);
    }
    pixDestroy(&pixmt);
    return 0;
}


/*!
 *  pixGetRankValueMasked()
 *
 *      Input:  pixs (8 bpp, or colormapped)
 *              pixm (<optional> 1 bpp mask over which rank val is to be taken;
 *                    use all pixels if null)
 *              x, y (UL corner of pixm relative to the UL corner of pixs; 
 *                    can be < 0; these values are ignored if pixm is null)
 *              factor (subsampling factor; integer >= 1)
 *              rank (between 0.0 and 1.0; 1.0 is brightest, 0.0 is darkest)
 *              &val (<return> pixel value corresponding to input rank)
 *              &na (<optional return> of histogram)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) Computes the rank value of pixels in pixs that are under
 *          the fg of the optional mask.  If the mask is null, it
 *          computes the average of the pixels in pixs.
 *      (2) Set the subsampling factor > 1 to reduce the amount of
 *          computation.
 *      (3) Clipping of pixm (if it exists) to pixs is done in the inner loop.
 *      (4) Input x,y are ignored unless pixm exists.
 *      (5) The rank must be in [0.0 ... 1.0], where the brightest pixel
 *          has rank 1.0.  For the median pixel value, use 0.5.
 *      (6) The histogram can optionally be returned, so that other rank
 *          values can be extracted without recomputing the histogram.
 *          In that case, just use
 *              numaHistogramGetValFromRank(na, 0, 1, rank, &val);
 *          on the returned Numa for additional rank values.
 */
l_int32
pixGetRankValueMasked(PIX        *pixs,
                      PIX        *pixm,
                      l_int32     x,
                      l_int32     y,
                      l_int32     factor,
                      l_float32   rank,
                      l_float32  *pval,
                      NUMA      **pna)
{
NUMA  *na;

    PROCNAME("pixGetRankValueMasked");

    if (pna)
        *pna = NULL;
    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    if (pixGetDepth(pixs) != 8 && !pixGetColormap(pixs))
        return ERROR_INT("pixs neither 8 bpp nor colormapped", procName, 1);
    if (pixm && pixGetDepth(pixm) != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);
    if (factor < 1)
        return ERROR_INT("sampling factor < 1", procName, 1);
    if (rank < 0.0 || rank > 1.0)
        return ERROR_INT("rank not in [0.0 ... 1.0]", procName, 1);
    if (!pval)
        return ERROR_INT("&val not defined", procName, 1);
    *pval = 0.0;  /* init */

    if ((na = pixGetGrayHistogramMasked(pixs, pixm, x, y, factor)) == NULL)
        return ERROR_INT("na not made", procName, 1);
    numaHistogramGetValFromRank(na, 0, 1, rank, pval);
    if (pna)
        *pna = na;
    else
        numaDestroy(&na);

    return 0;
}


/*!
 *  pixGetAverageMaskedRGB()
 *
 *      Input:  pixs (32 bpp, or colormapped)
 *              pixm (<optional> 1 bpp mask over which average is to be taken;
 *                    use all pixels if null)
 *              x, y (UL corner of pixm relative to the UL corner of pixs; 
 *                    can be < 0)
 *              factor (subsampling factor; >= 1)
 *              type (L_MEAN_ABSVAL, L_ROOT_MEAN_SQUARE,
 *                    L_STANDARD_DEVIATION, L_VARIANCE)
 *              &rval (<return optional> measured red value of given 'type')
 *              &gval (<return optional> measured green value of given 'type')
 *              &bval (<return optional> measured blue value of given 'type')
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) For usage, see pixGetAverageMasked().
 *      (2) If there is a colormap, it is removed before the 8 bpp
 *          component images are extracted.
 */
l_int32
pixGetAverageMaskedRGB(PIX        *pixs,
                       PIX        *pixm,
                       l_int32     x,
                       l_int32     y,
                       l_int32     factor,
                       l_int32     type,
                       l_float32  *prval,
                       l_float32  *pgval,
                       l_float32  *pbval)
{
l_int32   color;
PIX      *pixt;
PIXCMAP  *cmap;

    PROCNAME("pixGetAverageMaskedRGB");

    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    color = 0;
    cmap = pixGetColormap(pixs);
    if (pixGetDepth(pixs) != 32 && !cmap)
        return ERROR_INT("pixs neither 32 bpp nor colormapped", procName, 1);
    if (pixm && pixGetDepth(pixm) != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);
    if (factor < 1)
        return ERROR_INT("subsampling factor < 1", procName, 1);
    if (type != L_MEAN_ABSVAL && type != L_ROOT_MEAN_SQUARE &&
        type != L_STANDARD_DEVIATION && type != L_VARIANCE)
        return ERROR_INT("invalid measure type", procName, 1);
    if (!prval && !pgval && !pbval)
        return ERROR_INT("no values requested", procName, 1);

    if (prval) {
        if (cmap)
            pixt = pixGetRGBComponentCmap(pixs, COLOR_RED);
        else
            pixt = pixGetRGBComponent(pixs, COLOR_RED);
        pixGetAverageMasked(pixt, pixm, x, y, factor, type, prval);
        pixDestroy(&pixt);
    }
    if (pgval) {
        if (cmap)
            pixt = pixGetRGBComponentCmap(pixs, COLOR_GREEN);
        else
            pixt = pixGetRGBComponent(pixs, COLOR_GREEN);
        pixGetAverageMasked(pixt, pixm, x, y, factor, type, pgval);
        pixDestroy(&pixt);
    }
    if (pbval) {
        if (cmap)
            pixt = pixGetRGBComponentCmap(pixs, COLOR_BLUE);
        else
            pixt = pixGetRGBComponent(pixs, COLOR_BLUE);
        pixGetAverageMasked(pixt, pixm, x, y, factor, type, pbval);
        pixDestroy(&pixt);
    }

    return 0;
}


/*!
 *  pixGetAverageMasked()
 *
 *      Input:  pixs (8 bpp, or colormapped)
 *              pixm (<optional> 1 bpp mask over which average is to be taken;
 *                    use all pixels if null)
 *              x, y (UL corner of pixm relative to the UL corner of pixs; 
 *                    can be < 0)
 *              factor (subsampling factor; >= 1)
 *              type (L_MEAN_ABSVAL, L_ROOT_MEAN_SQUARE,
 *                    L_STANDARD_DEVIATION, L_VARIANCE)
 *              &val (<return> measured value of given 'type')
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) Use L_MEAN_ABSVAL to get the average value of pixels in pixs
 *          that are under the fg of the optional mask.  If the mask
 *          is null, it finds the average of the pixels in pixs.
 *      (2) Likewise, use L_ROOT_MEAN_SQUARE to get the rms value of
 *          pixels in pixs, either masked or not; L_STANDARD_DEVIATION
 *          to get the standard deviation from the mean of the pixels;
 *          L_VARIANCE to get the average squared difference from the
 *          expected value.  The variance is the square of the stdev.
 *          For the standard deviation, we use
 *              sqrt(<(<x> - x)>^2) = sqrt(<x^2> - <x>^2)
 *      (3) Set the subsampling factor > 1 to reduce the amount of
 *          computation.
 *      (4) Clipping of pixm (if it exists) to pixs is done in the inner loop.
 *      (5) Input x,y are ignored unless pixm exists.
 */
l_int32
pixGetAverageMasked(PIX        *pixs,
                    PIX        *pixm,
                    l_int32     x,
                    l_int32     y,
                    l_int32     factor,
                    l_int32     type,
                    l_float32  *pval)
{
l_int32    i, j, w, h, wm, hm, wplg, wplm, val, count;
l_uint32  *datag, *datam, *lineg, *linem;
l_float64  sumave, summs, ave, meansq, var;
PIX       *pixg;

    PROCNAME("pixGetAverageMasked");

    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    if (pixGetDepth(pixs) != 8 && !pixGetColormap(pixs))
        return ERROR_INT("pixs neither 8 bpp nor colormapped", procName, 1);
    if (pixm && pixGetDepth(pixm) != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);
    if (factor < 1)
        return ERROR_INT("subsampling factor < 1", procName, 1);
    if (type != L_MEAN_ABSVAL && type != L_ROOT_MEAN_SQUARE &&
        type != L_STANDARD_DEVIATION && type != L_VARIANCE)
        return ERROR_INT("invalid measure type", procName, 1);
    if (!pval)
        return ERROR_INT("&val not defined", procName, 1);
    *pval = 0.0;  /* init */

    if (pixGetColormap(pixs))
        pixg = pixRemoveColormap(pixs, REMOVE_CMAP_TO_GRAYSCALE);
    else
        pixg = pixClone(pixs);
    pixGetDimensions(pixg, &w, &h, NULL);
    datag = pixGetData(pixg);
    wplg = pixGetWpl(pixg);

    sumave = summs = 0.0;
    count = 0;
    if (!pixm) {
        for (i = 0; i < h; i += factor) {
            lineg = datag + i * wplg;
            for (j = 0; j < w; j += factor) {
                val = GET_DATA_BYTE(lineg, j);
                if (type != L_ROOT_MEAN_SQUARE)
                    sumave += val;
                if (type != L_MEAN_ABSVAL)
                    summs += val * val;
                count++;
            }
        }
    }
    else {
        pixGetDimensions(pixm, &wm, &hm, NULL);
        datam = pixGetData(pixm);
        wplm = pixGetWpl(pixm);
        for (i = 0; i < hm; i += factor) {
            if (y + i < 0 || y + i >= h) continue;
            lineg = datag + (y + i) * wplg;
            linem = datam + i * wplm;
            for (j = 0; j < wm; j += factor) {
                if (x + j < 0 || x + j >= w) continue;
                if (GET_DATA_BIT(linem, j)) {
                    val = GET_DATA_BYTE(lineg, x + j);
                    if (type != L_ROOT_MEAN_SQUARE)
                        sumave += val;
                    if (type != L_MEAN_ABSVAL)
                        summs += val * val;
                    count++;
                }
            }
        }
    }

    pixDestroy(&pixg);
    if (count == 0)
        return ERROR_INT("no pixels sampled", procName, 1);
    ave = sumave / (l_float64)count;
    meansq = summs / (l_float64)count;
    var = meansq - ave * ave;
    if (type == L_MEAN_ABSVAL)
        *pval = (l_float32)ave;
    else if (type == L_ROOT_MEAN_SQUARE)
        *pval = (l_float32)sqrt(meansq);
    else if (type == L_STANDARD_DEVIATION)
        *pval = (l_float32)sqrt(var);
    else  /* type == L_VARIANCE */
        *pval = (l_float32)var;

    return 0;
}


/*!
 *  pixGetAverageTiledRGB()
 *
 *      Input:  pixs (32 bpp, or colormapped)
 *              sx, sy (tile size; must be at least 2 x 2)
 *              type (L_MEAN_ABSVAL, L_ROOT_MEAN_SQUARE, L_STANDARD_DEVIATION)
 *              &pixr (<optional return> tiled 'average' of red component)
 *              &pixg (<optional return> tiled 'average' of green component)
 *              &pixb (<optional return> tiled 'average' of blue component)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) For usage, see pixGetAverageTiled().
 *      (2) If there is a colormap, it is removed before the 8 bpp
 *          component images are extracted.
 */
l_int32
pixGetAverageTiledRGB(PIX     *pixs,
                      l_int32  sx,
                      l_int32  sy,
                      l_int32  type,
                      PIX    **ppixr,
                      PIX    **ppixg,
                      PIX    **ppixb)
{
PIX      *pixt;
PIXCMAP  *cmap;

    PROCNAME("pixGetAverageTiledRGB");

    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    cmap = pixGetColormap(pixs);
    if (pixGetDepth(pixs) != 32 && !cmap)
        return ERROR_INT("pixs neither 32 bpp nor colormapped", procName, 1);
    if (sx < 2 || sy < 2)
        return ERROR_INT("sx and sy not both > 1", procName, 1);
    if (type != L_MEAN_ABSVAL && type != L_ROOT_MEAN_SQUARE &&
        type != L_STANDARD_DEVIATION)
        return ERROR_INT("invalid measure type", procName, 1);
    if (!ppixr && !ppixg && !ppixb)
        return ERROR_INT("no returned data requested", procName, 1);

    if (ppixr) {
        if (cmap)
            pixt = pixGetRGBComponentCmap(pixs, COLOR_RED);
        else
            pixt = pixGetRGBComponent(pixs, COLOR_RED);
        *ppixr = pixGetAverageTiled(pixt, sx, sy, type);
        pixDestroy(&pixt);
    }
    if (ppixg) {
        if (cmap)
            pixt = pixGetRGBComponentCmap(pixs, COLOR_GREEN);
        else
            pixt = pixGetRGBComponent(pixs, COLOR_GREEN);
        *ppixg = pixGetAverageTiled(pixt, sx, sy, type);
        pixDestroy(&pixt);
    }
    if (ppixb) {
        if (cmap)
            pixt = pixGetRGBComponentCmap(pixs, COLOR_BLUE);
        else
            pixt = pixGetRGBComponent(pixs, COLOR_BLUE);
        *ppixb = pixGetAverageTiled(pixt, sx, sy, type);
        pixDestroy(&pixt);
    }

    return 0;
}


/*!
 *  pixGetAverageTiled()
 *
 *      Input:  pixs (8 bpp, or colormapped)
 *              sx, sy (tile size; must be at least 2 x 2)
 *              type (L_MEAN_ABSVAL, L_ROOT_MEAN_SQUARE, L_STANDARD_DEVIATION)
 *      Return: pixd (average values in each tile), or null on error
 *
 *  Notes:
 *      (1) Only computes for tiles that are entirely contained in pixs.
 *      (2) Use L_MEAN_ABSVAL to get the average abs value within the tile;
 *          L_ROOT_MEAN_SQUARE to get the rms value within each tile;
 *          L_STANDARD_DEVIATION to get the standard dev. from the average
 *          within each tile.
 *      (3) If colormapped, converts to 8 bpp gray.
 */
PIX *
pixGetAverageTiled(PIX     *pixs,
                   l_int32  sx,
                   l_int32  sy,
                   l_int32  type)
{
l_int32    i, j, k, m, w, h, wd, hd, d, pos, wplt, wpld, valt;
l_uint32  *datat, *datad, *linet, *lined, *startt;
l_float64  sumave, summs, ave, meansq, normfact;
PIX       *pixt, *pixd;

    PROCNAME("pixGetAverageTiled");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    pixGetDimensions(pixs, &w, &h, &d);
    if (d != 8 && !pixGetColormap(pixs))
        return (PIX *)ERROR_PTR("pixs not 8 bpp or cmapped", procName, NULL);
    if (sx < 2 || sy < 2)
        return (PIX *)ERROR_PTR("sx and sy not both > 1", procName, NULL);
    wd = w / sx;
    hd = h / sy;
    if (wd < 1 || hd < 1)
        return (PIX *)ERROR_PTR("wd or hd == 0", procName, NULL);
    if (type != L_MEAN_ABSVAL && type != L_ROOT_MEAN_SQUARE &&
        type != L_STANDARD_DEVIATION)
        return (PIX *)ERROR_PTR("invalid measure type", procName, NULL);

    pixt = pixRemoveColormap(pixs, REMOVE_CMAP_TO_GRAYSCALE);
    pixd = pixCreate(wd, hd, 8);
    datat = pixGetData(pixt);
    wplt = pixGetWpl(pixt);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);
    normfact = 1. / (l_float64)(sx * sy);
    for (i = 0; i < hd; i++) {
        lined = datad + i * wpld;
        linet = datat + i * sy * wplt;
        for (j = 0; j < wd; j++) {
            if (type == L_MEAN_ABSVAL || type == L_STANDARD_DEVIATION) {
                sumave = 0.0;
                for (k = 0; k < sy; k++) {
                    startt = linet + k * wplt;
                    for (m = 0; m < sx; m++) {
                        pos = j * sx + m;
                        valt = GET_DATA_BYTE(startt, pos);
                        sumave += valt;
                    }
                }
                ave = normfact * sumave;
            }
            if (type == L_ROOT_MEAN_SQUARE || type == L_STANDARD_DEVIATION) {
                summs = 0.0;
                for (k = 0; k < sy; k++) {
                    startt = linet + k * wplt;
                    for (m = 0; m < sx; m++) {
                        pos = j * sx + m;
                        valt = GET_DATA_BYTE(startt, pos);
                        summs += valt * valt;
                    }
                }
                meansq = normfact * summs;
            }
            if (type == L_MEAN_ABSVAL)
                valt = (l_int32)(ave + 0.5);
            else if (type == L_ROOT_MEAN_SQUARE)
                valt = (l_int32)(sqrt(meansq) + 0.5);
            else  /* type == L_STANDARD_DEVIATION */
                valt = (l_int32)(sqrt(meansq - ave * ave) + 0.5);
            SET_DATA_BYTE(lined, j, valt);
        }
    }

    pixDestroy(&pixt);
    return pixd;
}


/*!
 *  pixGetExtremeValue()
 *
 *      Input:  pixs (8 bpp grayscale, 32 bpp rgb, or colormapped)
 *              factor (subsampling factor; >= 1; ignored if colormapped)
 *              type (L_CHOOSE_MIN or L_CHOOSE_MAX)
 *              &rval (<optional return> red component)
 *              &gval (<optional return> green component)
 *              &bval (<optional return> blue component)
 *              &grayval (<optional return> min gray value)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) If pixs is grayscale, the result is returned in &grayval.
 *          Otherwise, if there is a colormap or d == 32,
 *          each requested color component is returned.  At least
 *          one color component (address) must be input.
 */
l_int32
pixGetExtremeValue(PIX      *pixs,
                   l_int32   factor,
                   l_int32   type,
                   l_int32  *prval,
                   l_int32  *pgval,
                   l_int32  *pbval,
                   l_int32  *pgrayval)
{
l_int32    i, j, w, h, d, wpl;
l_int32    val, extval, rval, gval, bval, extrval, extgval, extbval;
l_uint32   pixel;
l_uint32  *data, *line;
PIXCMAP   *cmap;

    PROCNAME("pixGetExtremeValue");

    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);

    cmap = pixGetColormap(pixs);
    if (cmap)
        return pixcmapGetExtremeValue(cmap, type, prval, pgval, pbval);

    pixGetDimensions(pixs, &w, &h, &d);
    if (type != L_CHOOSE_MIN && type != L_CHOOSE_MAX)
        return ERROR_INT("invalid type", procName, 1);
    if (factor < 1)
        return ERROR_INT("subsampling factor < 1", procName, 1);
    if (d != 8 && d != 32)
        return ERROR_INT("pixs not 8 or 32 bpp", procName, 1);
    if (d == 8 && !pgrayval)
        return ERROR_INT("can't return result in grayval", procName, 1);
    if (d == 32 && !prval && !pgval && !pbval)
        return ERROR_INT("can't return result in r/g/b-val", procName, 1);

    data = pixGetData(pixs);
    wpl = pixGetWpl(pixs);
    if (d == 8) {
        if (type == L_CHOOSE_MIN)
            extval = 100000;
        else  /* get max */
            extval = 0;

        for (i = 0; i < h; i += factor) {
            line = data + i * wpl;
            for (j = 0; j < w; j += factor) {
                val = GET_DATA_BYTE(line, j);
                if ((type == L_CHOOSE_MIN && val < extval) ||
                    (type == L_CHOOSE_MAX && val > extval))
                    extval = val;
            }
        }
        *pgrayval = extval;
        return 0;
    }

        /* 32 bpp rgb */
    if (type == L_CHOOSE_MIN) {
        extrval = 100000;
        extgval = 100000;
        extbval = 100000;
    }
    else {
        extrval = 0;
        extgval = 0;
        extbval = 0;
    }
    for (i = 0; i < h; i += factor) {
        line = data + i * wpl;
        for (j = 0; j < w; j += factor) {
            pixel = line[j];
            if (prval) {
                rval = pixel >> 24;
                if ((type == L_CHOOSE_MIN && rval < extrval) ||
                    (type == L_CHOOSE_MAX && rval > extrval))
                    extrval = rval;
            }
            if (pgval) {
                gval = (pixel >> 16) & 0xff;
                if ((type == L_CHOOSE_MIN && gval < extgval) ||
                    (type == L_CHOOSE_MAX && gval > extgval))
                    extgval = gval;
            }
            if (pbval) {
                bval = (pixel >> 8) & 0xff;
                if ((type == L_CHOOSE_MIN && bval < extbval) ||
                    (type == L_CHOOSE_MAX && bval > extbval))
                    extbval = bval;
            }
        }
    }
    if (prval) *prval = extrval;
    if (pgval) *pgval = extgval;
    if (pbval) *pbval = extbval;
    return 0;
}


/*-------------------------------------------------------------*
 *              Foreground/background estimation               *
 *-------------------------------------------------------------*/
/*!
 *  pixThresholdForFgBg()
 *
 *      Input:  pixs (any depth; cmapped ok)
 *              factor (subsampling factor; integer >= 1)
 *              thresh (threshold for generating foreground mask)
 *              &fgval (<optional return> average foreground value)
 *              &bgval (<optional return> average background value)
 *      Return: 0 if OK, 1 on error
 */
l_int32
pixThresholdForFgBg(PIX      *pixs,
                    l_int32   factor,
                    l_int32   thresh,
                    l_int32  *pfgval,
                    l_int32  *pbgval)
{
l_float32  fval;
PIX       *pixg, *pixm;

    PROCNAME("pixThresholdForFgBg");

    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);

        /* Generate a subsampled 8 bpp version and a mask over the fg */
    pixg = pixConvertTo8BySampling(pixs, factor, 0);
    pixm = pixThresholdToBinary(pixg, thresh);

    if (pfgval) {
        pixGetAverageMasked(pixg, pixm, 0, 0, 1, L_MEAN_ABSVAL, &fval);
        *pfgval = (l_int32)(fval + 0.5);
    }

    if (pbgval) {
        pixInvert(pixm, pixm);
        pixGetAverageMasked(pixg, pixm, 0, 0, 1, L_MEAN_ABSVAL, &fval);
        *pbgval = (l_int32)(fval + 0.5);
    }

    pixDestroy(&pixg);
    pixDestroy(&pixm);
    return 0;
}


/*!
 *  pixSplitDistributionFgBg()
 *
 *      Input:  pixs (any depth; cmapped ok)
 *              estfract (estimated fraction of low-valued (fg) pixels)
 *              factor (subsampling factor; integer >= 1)
 *              &thresh (<optional return> best threshold for separating)
 *              &fgval (<optional return> average foreground value)
 *              &bgval (<optional return> average background value)
 *      Return: 0 if OK, 1 on error
 */
l_int32
pixSplitDistributionFgBg(PIX       *pixs,
                         l_float32  estfract,
                         l_int32    factor,
                         l_int32   *pthresh,
                         l_int32   *pfgval,
                         l_int32   *pbgval)
{
l_int32    thresh;
l_float32  avefg, avebg;
NUMA      *na;
PIX       *pixg;

    PROCNAME("pixSplitDistributionFgBg");

    if (pthresh) *pthresh = 0;
    if (pfgval) *pfgval = 0;
    if (pbgval) *pbgval = 0;
    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);

        /* Generate a subsampled 8 bpp version */
    pixg = pixConvertTo8BySampling(pixs, factor, 0);

        /* Make the fg/bg estimates */
    na = pixGetGrayHistogram(pixg, 1);
    numaSplitDistribution(na, estfract, &thresh, &avefg, &avebg, NULL, NULL, 0);

    if (pthresh) *pthresh = thresh;
    if (pfgval) *pfgval = (l_int32)(avefg + 0.5);
    if (pbgval) *pbgval = (l_int32)(avebg + 0.5);

    pixDestroy(&pixg);
    numaDestroy(&na);
    return 0;
}


/*-------------------------------------------------------------*
 *                 Measurement of properties                   *
 *-------------------------------------------------------------*/
/*!
 *  pixFindAreaPerimRatio()
 *
 *      Input:  pixs (1 bpp)
 *              tab (<optional> pixel sum table, can be NULL)
 *              &fract (<return> area/perimeter ratio)
 *      Return: 0 if OK, 1 on error
 */
l_int32
pixFindAreaPerimRatio(PIX        *pixs,
                      l_int32    *tab,
                      l_float32  *pfract)
{
l_int32  *tab8;
l_int32   nin, nbound;
PIX      *pixt;

    PROCNAME("pixFindAreaPerimRatio");

    if (!pfract)
        return ERROR_INT("&fract not defined", procName, 1);
    *pfract = 0.0;
    if (!pixs || pixGetDepth(pixs) != 1)
        return ERROR_INT("pixs not defined or not 1 bpp", procName, 1);

    if (!tab)
        tab8 = makePixelSumTab8();
    else
        tab8 = tab;

    pixt = pixErodeBrick(NULL, pixs, 3, 3);
    pixCountPixels(pixt, &nin, tab8);
    pixXor(pixt, pixt, pixs);
    pixCountPixels(pixt, &nbound, tab8);
    *pfract = (l_float32)nin / (l_float32)nbound;

    if (!tab)
        FREE(tab8);
    pixDestroy(&pixt);
    return 0;
}


/*-------------------------------------------------------------*
 *                Extract rectangular region                   *
 *-------------------------------------------------------------*/
/*!
 *  pixClipRectangle()
 *
 *      Input:  pixs
 *              box  (requested clipping region; const)
 *              &boxc (<optional return> actual box of clipped region)
 *      Return: clipped pix, or null on error or if rectangle
 *              doesn't intersect pixs
 *
 *  Notes:
 *
 *  This should be simple, but there are choices to be made.
 *  The box is defined relative to the pix coordinates.  However,
 *  if the box is not contained within the pix, we have two choices:
 *
 *      (1) clip the box to the pix
 *      (2) make a new pix equal to the full box dimensions,
 *          but let rasterop do the clipping and positioning
 *          of the src with respect to the dest
 *
 *  Choice (2) immediately brings up the problem of what pixel values
 *  to use that were not taken from the src.  For example, on a grayscale
 *  image, do you want the pixels not taken from the src to be black
 *  or white or something else?  To implement choice 2, one needs to
 *  specify the color of these extra pixels.
 *
 *  So we adopt (1), and clip the box first, if necessary,
 *  before making the dest pix and doing the rasterop.  But there
 *  is another issue to consider.  If you want to paste the
 *  clipped pix back into pixs, it must be properly aligned, and
 *  it is necessary to use the clipped box for alignment.
 *  Accordingly, this function has a third (optional) argument, which is
 *  the input box clipped to the src pix.
 */
PIX *
pixClipRectangle(PIX   *pixs,
                 BOX   *box,
                 BOX  **pboxc)
{
l_int32  w, h, d, bx, by, bw, bh;
BOX     *boxc;
PIX     *pixd;

    PROCNAME("pixClipRectangle");

    if (pboxc)
        *pboxc = NULL;
    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    if (!box)
        return (PIX *)ERROR_PTR("box not defined", procName, NULL);

        /* Clip the input box to the pix */
    pixGetDimensions(pixs, &w, &h, &d);
    if ((boxc = boxClipToRectangle(box, w, h)) == NULL) {
        L_WARNING("box doesn't overlap pix", procName);
        return NULL;
    }
    boxGetGeometry(boxc, &bx, &by, &bw, &bh);

        /* Extract the block */
    if ((pixd = pixCreate(bw, bh, d)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    pixCopyResolution(pixd, pixs);
    pixCopyColormap(pixd, pixs);
    pixRasterop(pixd, 0, 0, bw, bh, PIX_SRC, pixs, bx, by);

    if (pboxc)
        *pboxc = boxc;
    else
        boxDestroy(&boxc);

    return pixd;
}


/*!
 *  pixClipMasked()
 *
 *      Input:  pixs (1, 2, 4, 8, 16, 32 bpp; colormap ok)
 *              pixm  (clipping mask, 1 bpp)
 *              x, y (origin of clipping mask relative to pixs)
 *              outval (val to use for pixels that are outside the mask)
 *      Return: pixd, (clipped pix) or null on error or if pixm doesn't
 *              intersect pixs
 *
 *  Notes:
 *      (1) If pixs has a colormap, it is preserved in pixd.
 *      (2) The depth of pixd is the same as that of pixs.
 *      (3) If the depth of pixs is 1, use @outval = 0 for white background
 *          and 1 for black; otherwise, use the max value for white
 *          and 0 for black.  If pixs has a colormap, the max value for
 *          @outval is 0xffffffff; otherwise, it is 2^d - 1.
 *      (4) When using 1 bpp pixs, this is a simple clip and
 *          blend operation.  For example, if both pix1 and pix2 are
 *          black text on white background, and you want to OR the
 *          fg on the two images, let pixm be the inverse of pix2.
 *          Then the operation takes all of pix1 that's in the bg of
 *          pix2, and for the remainder (which are the pixels
 *          corresponding to the fg of the pix2), paint them black
 *          (1) in pix1.  The function call looks like
 *             pixClipMasked(pix2, pixInvert(pix1, pix1), x, y, 1);
 */
PIX *
pixClipMasked(PIX      *pixs,
              PIX      *pixm,
              l_int32   x,
              l_int32   y,
              l_uint32  outval)
{
l_int32   wm, hm, d, index, rval, gval, bval;
l_uint32  pixel;
BOX      *box;
PIX      *pixmi, *pixd;
PIXCMAP  *cmap;

    PROCNAME("pixClipMasked");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    if (!pixm || pixGetDepth(pixm) != 1)
        return (PIX *)ERROR_PTR("pixm undefined or not 1 bpp", procName, NULL);

        /* Clip out the region specified by pixm and (x,y) */
    pixGetDimensions(pixm, &wm, &hm, NULL);
    box = boxCreate(x, y, wm, hm);
    pixd = pixClipRectangle(pixs, box, NULL);

        /* Paint 'outval' (or something close to it if cmapped) through
         * the pixels not masked by pixm */
    cmap = pixGetColormap(pixd);
    pixmi = pixInvert(NULL, pixm);
    d = pixGetDepth(pixd);
    if (cmap) {
        rval = GET_DATA_BYTE(&outval, COLOR_RED);
        gval = GET_DATA_BYTE(&outval, COLOR_GREEN);
        bval = GET_DATA_BYTE(&outval, COLOR_BLUE);
        pixcmapGetNearestIndex(cmap, rval, gval, bval, &index);
	pixcmapGetColor(cmap, index, &rval, &gval, &bval);
	composeRGBPixel(rval, gval, bval, &pixel);
        pixPaintThroughMask(pixd, pixmi, 0, 0, pixel);
    }
    else
        pixPaintThroughMask(pixd, pixmi, 0, 0, outval);

    boxDestroy(&box);
    pixDestroy(&pixmi);
    return pixd;
}


/*-------------------------------------------------------------*
 *              Extract min rectangle with ON pixels           *
 *-------------------------------------------------------------*/
/*!
 *  pixClipToForeground()
 *
 *      Input:  pixs (1 bpp)
 *              &pixd  (<optional return> clipped pix returned)
 *              &box   (<optional return> bounding box)
 *      Return: 0 if OK; 1 on error or if there are no fg pixels
 *
 *  Notes:
 *      (1) At least one of {&pixd, &box} must be specified.
 *      (2) If there are no fg pixels, the returned ptrs are null.
 */
l_int32
pixClipToForeground(PIX   *pixs,
                    PIX  **ppixd,
                    BOX  **pbox)
{
l_int32    w, h, d, wpl, nfullwords, extra, i, j;
l_int32    minx, miny, maxx, maxy;
l_uint32   result, mask;
l_uint32  *data, *line;
BOX       *box;

    PROCNAME("pixClipToForeground");

    if (!ppixd && !pbox)
        return ERROR_INT("neither &pixd nor &pbox defined", procName, 1);
    if (ppixd)
        *ppixd = NULL;
    if (pbox)
        *pbox = NULL;
    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    if ((d = pixGetDepth(pixs)) != 1)
        return ERROR_INT("pixs not binary", procName, 1);

    pixGetDimensions(pixs, &w, &h, NULL);
    nfullwords = w / 32;
    extra = w & 31;
    mask = ~rmask32[32 - extra];
    wpl = pixGetWpl(pixs);
    data = pixGetData(pixs);

    result = 0;
    for (i = 0, miny = 0; i < h; i++, miny++) {
        line = data + i * wpl;
        for (j = 0; j < nfullwords; j++)
            result |= line[j];
        if (extra)
            result |= (line[j] & mask);
        if (result)
            break;
    }
    if (miny == h)  /* no ON pixels */
        return 1;

    result = 0;
    for (i = h - 1, maxy = h - 1; i >= 0; i--, maxy--) {
        line = data + i * wpl;
        for (j = 0; j < nfullwords; j++)
            result |= line[j];
        if (extra)
            result |= (line[j] & mask);
        if (result)
            break;
    }

    minx = 0;
    for (j = 0, minx = 0; j < w; j++, minx++) {
        for (i = 0; i < h; i++) {
            line = data + i * wpl;
            if (GET_DATA_BIT(line, j))
                goto minx_found;
        }
    }

minx_found:
    for (j = w - 1, maxx = w - 1; j >= 0; j--, maxx--) {
        for (i = 0; i < h; i++) {
            line = data + i * wpl;
            if (GET_DATA_BIT(line, j))
                goto maxx_found;
        }
    }

maxx_found:
    box = boxCreate(minx, miny, maxx - minx + 1, maxy - miny + 1);

    if (ppixd)
        *ppixd = pixClipRectangle(pixs, box, NULL);
    if (pbox)
        *pbox = box;
    else
        boxDestroy(&box);

    return 0;
}


/*-------------------------------------------------------------*
 *              Mirrored tiling of a smaller image             *
 *-------------------------------------------------------------*/
/*!
 *  pixMirroredTiling()
 *
 *      Input:  pixs (8 or 32 bpp, small tile; to be replicated)
 *              w, h (dimensions of output pix)
 *      Return: pixd (usually larger pix, mirror-tiled with pixs),
 *              or null on error
 *
 *  Notes:
 *      (1) This uses mirrored tiling, where each row alternates
 *          with LR flips and every column alternates with TB
 *          flips, such that the result is a tiling with identical
 *          2 x 2 tiles, each of which is composed of these transforms:
 *                  -----------------
 *                  | 1    |  LR    |
 *                  -----------------
 *                  | TB   |  LR/TB |
 *                  -----------------
 */
PIX *
pixMirroredTiling(PIX     *pixs,
                  l_int32  w,
                  l_int32  h)
{
l_int32   wt, ht, d, i, j, nx, ny;
PIX      *pixd, *pixsfx, *pixsfy, *pixsfxy, *pix;

    PROCNAME("pixMirroredTiling");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    pixGetDimensions(pixs, &wt, &ht, &d);
    if (wt <= 0 || ht <= 0)
        return (PIX *)ERROR_PTR("pixs size illegal", procName, NULL);
    if (d != 8 && d != 32)
        return (PIX *)ERROR_PTR("depth not 32 bpp", procName, NULL);
    if ((pixd = pixCreate(w, h, d)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);

    nx = (w + wt - 1) / wt;
    ny = (h + ht - 1) / ht;
    pixsfx = pixFlipLR(NULL, pixs);
    pixsfy = pixFlipTB(NULL, pixs);
    pixsfxy = pixFlipTB(NULL, pixsfx);
    for (i = 0; i < ny; i++) {
        for (j = 0; j < nx; j++) {
            pix = pixs;
            if ((i & 1) && !(j & 1))
                pix = pixsfy;
            else if (!(i & 1) && (j & 1))
                pix = pixsfx;
            else if ((i & 1) && (j & 1))
                pix = pixsfxy;
            pixRasterop(pixd, j * wt, i * ht, wt, ht, PIX_SRC, pix, 0, 0);
        }
    }

    pixDestroy(&pixsfx);
    pixDestroy(&pixsfy);
    pixDestroy(&pixsfxy);
    return pixd;
}


/*!
 *  findTilePatchCenter()
 *
 *      Input:  pixs (8 or 16 bpp; distance function of a binary mask)
 *              box (region of pixs to search around)
 *              searchdir (L_HORIZ or L_VERT; direction to search)
 *              targdist (desired distance of selected patch center from fg)
 *              &dist (<return> actual distance of selected location)
 *              &xc, &yc (<return> location of selected patch center)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) This looks for a patch of non-masked image, that is outside
 *          but near the input box.  The input pixs is a distance
 *          function giving the distance from the fg in a binary mask.
 *      (2) The target distance implicitly specifies a desired size
 *          for the patch.  The location of the center of the patch,
 *          and the actual distance from fg are returned.
 *      (3) If the target distance is larger than 255, a 16-bit distance
 *          transform is input.
 *      (4) It is assured that a square centered at (xc, yc) and of
 *          size 'dist' will not intersect with the fg of the binary
 *          mask that was used to generate pixs.
 */
static l_int32
findTilePatchCenter(PIX       *pixs,
                    BOX       *box,
                    l_int32    searchdir,
                    l_uint32   targdist,
                    l_uint32  *pdist,
                    l_int32   *pxc,
                    l_int32   *pyc)
{
l_int32   w, h, bx, by, bw, bh, left, right, top, bot, i, j;
l_uint32  val, maxval;

    PROCNAME("findTilePatchCenter");

    if (!pdist || !pxc || !pyc)
        return ERROR_INT("&pdist, &pxc, &pyc not all defined", procName, 1);
    *pdist = *pxc = *pyc = 0;
    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    if (!box)
        return ERROR_INT("box not defined", procName, 1);

    pixGetDimensions(pixs, &w, &h, NULL);
    boxGetGeometry(box, &bx, &by, &bw, &bh);

    if (searchdir == L_HORIZ) {
        left = bx;   /* distance to left of box */
        right = w - bx - bw + 1;   /* distance to right of box */
        maxval = 0;
        if (left > right) {  /* search to left */
            for (j = bx - 1; j >= 0; j--) {
                for (i = by; i < by + bh; i++) {
                    pixGetPixel(pixs, j, i, &val);
                    if (val > maxval) {
                        maxval = val;
                        *pxc = j;
                        *pyc = i;
                        *pdist = val;
                        if (val >= targdist)
                            return 0;
                    }
                }
            }
        }
        else {  /* search to right */
            for (j = bx + bw; j < w; j++) {
                for (i = by; i < by + bh; i++) {
                    pixGetPixel(pixs, j, i, &val);
                    if (val > maxval) {
                        maxval = val;
                        *pxc = j;
                        *pyc = i;
                        *pdist = val;
                        if (val >= targdist)
                            return 0;
                    }
                }
            }
        }
    }
    else {  /* searchdir == L_VERT */
        top = by;    /* distance above box */
        bot = h - by - bh + 1;   /* distance below box */
        maxval = 0;
        if (top > bot) {  /* search above */
            for (i = by - 1; i >= 0; i--) {
                for (j = bx; j < bx + bw; j++) {
                    pixGetPixel(pixs, j, i, &val);
                    if (val > maxval) {
                        maxval = val;
                        *pxc = j;
                        *pyc = i;
                        *pdist = val;
                        if (val >= targdist)
                            return 0;
                    }
                }
            }
        }
        else {  /* search below */
            for (i = by + bh; i < h; i++) {
                for (j = bx; j < bx + bw; j++) {
                    pixGetPixel(pixs, j, i, &val);
                    if (val > maxval) {
                        maxval = val;
                        *pxc = j;
                        *pyc = i;
                        *pdist = val;
                        if (val >= targdist)
                            return 0;
                    }
                }
            }
        }
    }


    pixGetPixel(pixs, *pxc, *pyc, pdist);
    return 0;
}



