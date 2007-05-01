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
 *      (5) Rectangle extraction
 *      (6) Foreground extraction on binary image
 *
 *      Masked operations
 *           l_int32     pixSetMasked()
 *           l_int32     pixSetMaskedGeneral()
 *           l_int32     pixCombineMasked()
 *           l_int32     pixPaintThroughMask()
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
 *
 *      Pixel histogram, rank val, and averaging
 *           NUMA       *pixGetHistogram()
 *           NUMA       *pixGetHistogramMasked()
 *           l_int32     pixGetRankValMasked()
 *           l_int32     pixGetAverageMasked()
 *           l_int32     pixGetRMSMasked()
 *
 *      Extract rectangle
 *           PIX        *pixClipRectangle()
 *
 *      Clip to foreground
 *           PIX        *pixClipToForeground()
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "allheaders.h"

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
 *      Input:  pixd (8, 16 or 32 bpp)
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
 */
l_int32
pixSetMasked(PIX      *pixd,
             PIX      *pixm,
             l_uint32  val)
{
l_int32    wd, hd, wm, hm, w, h, d, wpld, wplm;
l_int32    i, j, bitval, rval, gval, bval;
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

    pixGetDimensions(pixd, &wd, &hd, &d);
    pixGetDimensions(pixm, &wm, &hm, NULL);
    if (d != 8 && d != 16 && d != 32)
        return ERROR_INT("pixd not 8, 16 or 32 bpp", procName, 1);
    if (pixGetDepth(pixm) != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);
    w = L_MIN(wd, wm);
    h = L_MIN(hd, hm);
    if (L_ABS(wd - wm) > 7 || L_ABS(hd - hm) > 7)  /* allow a small tolerance */
        L_WARNING("pixd and pixm sizes differ", procName);

    datad = pixGetData(pixd);
    datam = pixGetData(pixm);
    wpld = pixGetWpl(pixd);
    wplm = pixGetWpl(pixm);
    if (d == 8) {
        val &= 0xff;
        for (i = 0; i < h; i++) {
            lined = datad + i * wpld;
            linem = datam + i * wplm;
            for (j = 0; j < w; j++) {
                bitval = GET_DATA_BIT(linem, j);
                if (bitval)
                    SET_DATA_BYTE(lined, j, val);
            }
        }
    }
    else if (d == 16) {
        val &= 0xffff;
        for (i = 0; i < h; i++) {
            lined = datad + i * wpld;
            linem = datam + i * wplm;
            for (j = 0; j < w; j++) {
                bitval = GET_DATA_BIT(linem, j);
                if (bitval)
                    SET_DATA_TWO_BYTES(lined, j, val);
            }
        }
    }
    else {  /*  d == 32 */
        for (i = 0; i < h; i++) {
            lined = datad + i * wpld;
            linem = datam + i * wplm;
            for (j = 0; j < w; j++) {
                bitval = GET_DATA_BIT(linem, j);
                if (bitval)
                    *(lined + j) = val;
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
 *      Input:  pixd (8 or 32 bpp)
 *              pixm (<optional> 1 bpp mask)
 *              x, y (origin of pixm relative to pixd; can be negative)
 *              val (1 byte or rgb color to set at each masked pixel)
 *      Return: 0 if OK; 1 on error
 *
 *  Notes:
 *      (1) In-place operation.  Calls pixSetMaskedCmap() for colormapped
 *          images.
 *      (2) For 8 bpp gray, we take the LSB of the color
 *      (3) If pixm == NULL, it's a no-op.
 *      (4) The mask origin is placed at (x,y) on pixd, and the
 *          operation is clipped to the intersection of rectangles.
 *      (5) For rgb, the components in val are in the canonical locations,
 *          with red in location COLOR_RED, etc.
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
    d = pixGetDepth(pixd);
    if (d != 8 && d != 32)
        return ERROR_INT("pixd not 8 or 32 bpp", procName, 1);
    if (pixGetDepth(pixm) != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);

    pixGetDimensions(pixd, &w, &h, NULL);
    wpl = pixGetWpl(pixd);
    data = pixGetData(pixd);
    pixGetDimensions(pixm, &wm, &hm, NULL);
    wplm = pixGetWpl(pixm);
    datam = pixGetData(pixm);

    if (d == 8)
        val = val & 0xff;
    for (i = 0; i < hm; i++) {
        if (y + i < 0 || y + i >= h) continue;
        line = data + (y + i) * wpl;
        linem = datam + i * wplm;
        for (j = 0; j < wm; j++) {
            if (x + j < 0 || x + j >= w) continue;
            if (GET_DATA_BIT(linem, j)) {
                switch (d)
                {
                case 8:
                    SET_DATA_BYTE(line, x + j, val);
                    break;
                case 32:
                    *(line + x + j) = val;
                    break;
                default:
                    return ERROR_INT("d not 8 or 32 bpp", procName, 1);
                }
            }
        }
    }

    return 0;
}
    

/*-------------------------------------------------------------*
 *    One and two-image boolean ops on arbitrary depth images  *
 *-------------------------------------------------------------*/
/*!
 *  pixInvert()
 *
 *      Input:  pixd  (<optional> destination: this can be null, 
 *                     equal to pixs, or different from pixs)
 *              pixs
 *      Return: pixd always
 *
 *  Notes:
 *      (1) This inverts pixs, for all pixel depths.
 *      (2) There are 3 cases:
 *              if pixd == null,   ~src --> new pixd
 *              if pixd == pixs,   ~src --> src  (in-place)
 *              if pixd != pixs,   ~src --> input pixd
 */
PIX *
pixInvert(PIX  *pixd,
          PIX  *pixs)
{
    PROCNAME("pixInvert");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, pixd);

    if (pixs != pixd) {
        if ((pixd = pixCopy(pixd, pixs)) == NULL)
            return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    }

    pixRasterop(pixd, 0, 0, pixGetWidth(pixd), pixGetHeight(pixd),
                PIX_NOT(PIX_DST), NULL, 0, 0);   /* invert pixd */

    return pixd;
}


/*!
 *  pixOr()
 *
 *      Input:  pixd  (<optional> destination: this can be null, 
 *                     equal to pixs1, or different from pixs1)
 *              pixs1 (can be == to pixd)
 *              pixs2 
 *      Return: pixd always
 *
 *  Notes:
 *      (1) This gives the union of two images with equal depth,
 *          aligning them to the the UL corner.
 *      (2) There are 3 cases:
 *            if pixd == null,   (src1 | src2) --> new pixd
 *            if pixd == pixs1,  (src1 | src2) --> src1  (in-place)
 *            if pixd != pixs1,  (src1 | src2) --> input pixd
 *      (3) The size of the result is determined by pixs1.
 */
PIX *
pixOr(PIX  *pixd,
      PIX  *pixs1,
      PIX  *pixs2)
{
    PROCNAME("pixOr");

    if (!pixs1)
        return (PIX *)ERROR_PTR("pixs1 not defined", procName, pixd);
    if (!pixs2)
        return (PIX *)ERROR_PTR("pixs2 not defined", procName, pixd);
    if (pixs1 == pixs2)
        return (PIX *)ERROR_PTR("pixs1 and pixs2 must differ", procName, pixd);
    if (pixGetDepth(pixs1) != pixGetDepth(pixs2))
        return (PIX *)ERROR_PTR("depths not the same", procName, pixd);

#if  EQUAL_SIZE_WARNING
    if (!pixSizesEqual(pixs1, pixs2))
        L_WARNING("pixs1 and pixs2 not equal sizes", procName);
#endif  /* EQUAL_SIZE_WARNING */

    if (pixs1 != pixd) {
        if ((pixd = pixCopy(pixd, pixs1)) == NULL)
            return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    }

        /* src1 | src2 --> dest */
    pixRasterop(pixd, 0, 0, pixGetWidth(pixd), pixGetHeight(pixd),
                PIX_SRC | PIX_DST, pixs2, 0, 0);

    return pixd;
}


/*!
 *  pixAnd()
 *
 *      Input:  pixd  (<optional> destination: this can be null, 
 *                     equal to pixs1, or different from pixs1)
 *              pixs1 (can be == to pixd)
 *              pixs2 
 *      Return: pixd always
 *
 *  Notes:
 *      (1) This gives the intersection of two images with equal depth,
 *          aligning them to the the UL corner.
 *      (2) There are 3 cases:
 *            if pixd == null,   (src1 & src2) --> new pixd
 *            if pixd == pixs1,  (src1 & src2) --> src1  (in-place)
 *            if pixd != pixs1,  (src1 & src2) --> input pixd
 *      (3) The size of the result is determined by pixs1.
 */
PIX *
pixAnd(PIX  *pixd,
       PIX  *pixs1,
       PIX  *pixs2)
{
    PROCNAME("pixAnd");

    if (!pixs1)
        return (PIX *)ERROR_PTR("pixs1 not defined", procName, pixd);
    if (!pixs2)
        return (PIX *)ERROR_PTR("pixs2 not defined", procName, pixd);
    if (pixs1 == pixs2)
        return (PIX *)ERROR_PTR("pixs1 and pixs2 must differ", procName, pixd);
    if (pixGetDepth(pixs1) != pixGetDepth(pixs2))
        return (PIX *)ERROR_PTR("depths not the same", procName, pixd);

#if  EQUAL_SIZE_WARNING
    if (!pixSizesEqual(pixs1, pixs2))
        L_WARNING("pixs1 and pixs2 not equal sizes", procName);
#endif  /* EQUAL_SIZE_WARNING */

    if (pixs1 != pixd) {
        if ((pixd = pixCopy(pixd, pixs1)) == NULL)
            return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    }

        /* src1 & src2 --> dest */
    pixRasterop(pixd, 0, 0, pixGetWidth(pixd), pixGetHeight(pixd),
                PIX_SRC & PIX_DST, pixs2, 0, 0);

    return pixd;
}


/*!
 *  pixXor()
 *
 *      Input:  pixd  (<optional> destination: this can be null, 
 *                     equal to pixs1, or different from pixs1)
 *              pixs1 (can be == to pixd)
 *              pixs2 
 *      Return: pixd always
 *
 *  Notes:
 *      (1) This gives the XOR of two images with equal depth,
 *          aligning them to the the UL corner.
 *      (2) There are 3 cases:
 *            if pixd == null,   (src1 ^ src2) --> new pixd
 *            if pixd == pixs1,  (src1 ^ src2) --> src1  (in-place)
 *            if pixd != pixs1,  (src1 ^ src2) --> input pixd
 *      (3) The size of the result is determined by pixs1.
 */
PIX *
pixXor(PIX  *pixd,
       PIX  *pixs1,
       PIX  *pixs2)
{
    PROCNAME("pixXor");

    if (!pixs1)
        return (PIX *)ERROR_PTR("pixs1 not defined", procName, pixd);
    if (!pixs2)
        return (PIX *)ERROR_PTR("pixs2 not defined", procName, pixd);
    if (pixs1 == pixs2)
        return (PIX *)ERROR_PTR("pixs1 and pixs2 must differ", procName, pixd);
    if (pixGetDepth(pixs1) != pixGetDepth(pixs2))
        return (PIX *)ERROR_PTR("depths not the same", procName, pixd);

#if  EQUAL_SIZE_WARNING
    if (!pixSizesEqual(pixs1, pixs2))
        L_WARNING("pixs1 and pixs2 not equal sizes", procName);
#endif  /* EQUAL_SIZE_WARNING */

    if (pixs1 != pixd) {
        if ((pixd = pixCopy(pixd, pixs1)) == NULL)
            return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    }

        /* src1 ^ src2 --> dest */
    pixRasterop(pixd, 0, 0, pixGetWidth(pixd), pixGetHeight(pixd),
                PIX_SRC ^ PIX_DST, pixs2, 0, 0);

    return pixd;
}


/*!
 *  pixSubtract()
 *
 *      Input:  pixd  (<optional> destination: this can be null, 
 *                     equal to pixs1, equal to pixs2, or different
 *                     from both pixs1 and pixs2)
 *              pixs1 (can be == to pixd)
 *              pixs2 (can be == to pixd)
 *      Return: pixd always
 *
 *  Notes:
 *      (1) This gives the set subtraction of two images with equal depth,
 *          aligning them to the the UL corner.
 *      (2) Source pixs2 is always subtracted from source pixs1.
 *          The result is
 *                  pixs1 \ pixs2 = pixs1 & (~pixs2)
 *      (3) There are 4 cases.  The result can go to a new dest,
 *          in-place to either pixs1 or pixs2, or to an existing input dest:
 *              if pixd == null,   (src1 - src2) --> new pixd
 *              if pixd == pixs1,  (src1 - src2) --> src1  (in-place)
 *              if pixd == pixs2,  (src1 - src2) --> src2  (in-place)
 *              if pixd != pixs1 && pixd != pixs2),
 *                                 (src1 - src2) --> input pixd
 *      (4) If pixd == NULL, the size of the result is determined by pixs1.
 */
PIX *
pixSubtract(PIX  *pixd,
            PIX  *pixs1,
            PIX  *pixs2)
{
l_int32  w, h;

    PROCNAME("pixSubtract");

    if (!pixs1)
        return (PIX *)ERROR_PTR("pixs1 not defined", procName, pixd);
    if (!pixs2)
        return (PIX *)ERROR_PTR("pixs2 not defined", procName, pixd);
    if (pixs1 == pixs2)
        return (PIX *)ERROR_PTR("pixs1 and pixs2 must differ", procName, pixd);
    if (pixGetDepth(pixs1) != pixGetDepth(pixs2))
        return (PIX *)ERROR_PTR("depths not the same", procName, pixd);

#if  EQUAL_SIZE_WARNING
    if (!pixSizesEqual(pixs1, pixs2))
        L_WARNING("pixs1 and pixs2 not equal sizes", procName);
#endif  /* EQUAL_SIZE_WARNING */

    w = pixGetWidth(pixs1);
    h = pixGetHeight(pixs1);

    if (!pixd) {
        if ((pixd = pixCopy(NULL, pixs1)) == NULL)
            return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
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
        if (pixGetDepth(pixd) != 1)
            return (PIX *)ERROR_PTR("pixd not binary", procName, pixd);
#if  EQUAL_SIZE_WARNING
        if (!pixSizesEqual(pixd, pixs1))
            L_WARNING("pixd and pixs1 not equal sizes", procName);
#endif  /* EQUAL_SIZE_WARNING */
        pixRasterop(pixd, 0, 0, w, h, PIX_SRC, pixs1, 0, 0);   /* copy */
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


/*------------------------------------------------------------------*
 *                  Pixel histogram and averaging                   *
 *------------------------------------------------------------------*/
/*!
 *  pixGetHistogram()
 *
 *      Input:  pixs (1, 2, 4, 8, 16 bpp; can be colormapped)
 *      Return: na (histogram), or null on error
 *
 *  Notes:
 *      (1) The output histogram is of minimal size to express
 *          the range of pixel values in pixs.
 */
NUMA *
pixGetHistogram(PIX  *pixs)
{
l_int32     i, j, w, h, d, wpl, val, size, count;
l_uint32   *data, *line;
l_float32  *array;
NUMA       *na;
PIX        *pixg;

    PROCNAME("pixGetHistogram");

    if (!pixs)
        return (NUMA *)ERROR_PTR("pixs not defined", procName, NULL);
    d = pixGetDepth(pixs);
    if (d > 16)
        return (NUMA *)ERROR_PTR("depth not in {1,2,4,8,16}", procName, NULL);

    if (pixGetColormap(pixs))
        pixg = pixRemoveColormap(pixs, REMOVE_CMAP_BASED_ON_SRC);
    else
        pixg = pixClone(pixs);

    pixGetDimensions(pixg, &w, &h, &d);
    size = 1 << d;
    if ((na = numaCreate(size)) == NULL)
        return (NUMA *)ERROR_PTR("na not made", procName, NULL);
    na->n = size;  /* fake storage of zeroes */
    array = na->array;  /* don't do this at home */

    if (d == 1) {  /* special case */
        pixCountPixels(pixg, &count, NULL);
        array[0] = w * h - count;
        array[1] = count;
        pixDestroy(&pixg);
        return na;
    }

    wpl = pixGetWpl(pixg);
    data = pixGetData(pixg);
    for (i = 0; i < h; i++) {
        line = data + i * wpl;
        switch (d) 
        {
        case 2:
            for (j = 0; j < w; j++) {
                val = GET_DATA_DIBIT(line, j);
                array[val] += 1.0;
            }
            break;
        case 4:
            for (j = 0; j < w; j++) {
                val = GET_DATA_QBIT(line, j);
                array[val] += 1.0;
            }
            break;
        case 8:
            for (j = 0; j < w; j++) {
                val = GET_DATA_BYTE(line, j);
                array[val] += 1.0;
            }
            break;
        case 16:
            for (j = 0; j < w; j++) {
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
 *  pixGetHistogramMasked()
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
 *      (2) Always returns a 256-value histogram of pixel values.
 *      (3) Set the subsampling factor > 1 to reduce the amount of computation.
 *      (4) Clipping of pixm (if it exists) to pixs is done in the inner loop.
 *      (5) Input x,y are ignored unless pixm exists.
 */
NUMA *
pixGetHistogramMasked(PIX        *pixs,
                      PIX        *pixm,
                      l_int32     x,
                      l_int32     y,
                      l_int32     factor)
{
l_int32     i, j, w, h, wm, hm, wplg, wplm, val;
l_uint32   *datag, *datam, *lineg, *linem;
l_float32  *array;
NUMA       *na;
PIX        *pixg;

    PROCNAME("pixGetHistogramMasked");

    if (!pixs)
        return (NUMA *)ERROR_PTR("pixs not defined", procName, NULL);
    if (pixGetDepth(pixs) != 8 && !pixGetColormap(pixs))
        return (NUMA *)ERROR_PTR("pixs neither 8 bpp nor colormapped",
                                 procName, NULL);
    if (pixm && pixGetDepth(pixm) != 1)
        return (NUMA *)ERROR_PTR("pixm not 1 bpp", procName, NULL);
    if (factor < 1)
        return (NUMA *)ERROR_PTR("sampling factor < 1", procName, NULL);

    if ((na = numaCreate(256)) == NULL)
        return (NUMA *)ERROR_PTR("na not made", procName, NULL);
    na->n = 256;  /* fake storage of zeroes */
    array = na->array;  /* don't do this at home */

    if (pixGetColormap(pixs))
        pixg = pixRemoveColormap(pixs, REMOVE_CMAP_TO_GRAYSCALE);
    else
        pixg = pixClone(pixs);
    pixGetDimensions(pixg, &w, &h, NULL);
    datag = pixGetData(pixg);
    wplg = pixGetWpl(pixg);

        /* Generate the histogram */
    if (!pixm) {
        for (i = 0; i < h; i += factor) {
            lineg = datag + i * wplg;
            for (j = 0; j < w; j += factor) {
                val = GET_DATA_BYTE(lineg, j);
                array[val] += 1.0;
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
                    array[val] += 1.0;
                }
            }
        }
    }

    pixDestroy(&pixg);
    return na;
}


/*!
 *  pixGetRankValMasked()
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
pixGetRankValMasked(PIX        *pixs,
                    PIX        *pixm,
                    l_int32     x,
                    l_int32     y,
                    l_int32     factor,
                    l_float32   rank,
                    l_float32  *pval,
                    NUMA      **pna)
{
NUMA  *na;

    PROCNAME("pixGetRankValMasked");

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

    if ((na = pixGetHistogramMasked(pixs, pixm, x, y, factor)) == NULL)
        return ERROR_INT("na not made", procName, 1);
    numaHistogramGetValFromRank(na, 0, 1, rank, pval);
    if (pna)
        *pna = na;
    else
        numaDestroy(&na);

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
 *              &val (<return> average value)
 *      Return: 0 if OK, 1 on error
 *  Notes:
 *      (1) Computes the average value of pixels in pixs that are under
 *          the fg of the optional mask.  If the mask is null, it
 *          computes the average of the pixels in pixs.
 *      (2) Set the subsampling factor > 1 to reduce the amount of
 *          computation.
 *      (3) Clipping of pixm (if it exists) to pixs is done in the inner loop.
 *      (4) Input x,y are ignored unless pixm exists.
 */
l_int32
pixGetAverageMasked(PIX        *pixs,
                    PIX        *pixm,
                    l_int32     x,
                    l_int32     y,
                    l_int32     factor,
                    l_float32  *pval)
{
l_int32    i, j, w, h, wm, hm, wplg, wplm, sum, count;
l_uint32  *datag, *datam, *lineg, *linem;
PIX       *pixg;

    PROCNAME("pixGetAverageMasked");

    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    if (pixGetDepth(pixs) != 8 && !pixGetColormap(pixs))
        return ERROR_INT("pixs neither 8 bpp nor colormapped", procName, 1);
    if (pixm && pixGetDepth(pixm) != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);
    if (factor < 1)
        return ERROR_INT("sampling factor < 1", procName, 1);
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

    sum = count = 0;
    if (!pixm) {
        for (i = 0; i < h; i += factor) {
            lineg = datag + i * wplg;
            for (j = 0; j < w; j += factor) {
                sum += GET_DATA_BYTE(lineg, j);
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
                    sum += GET_DATA_BYTE(lineg, x + j);
                    count++;
                }
            }
        }
    }

    pixDestroy(&pixg);
    if (count == 0)
        return ERROR_INT("no pixels sampled", procName, 1);
    *pval = (l_float32)sum / (l_float32)count;

    return 0;
}


/*!
 *  pixGetRMSMasked()
 *
 *      Input:  pixs (8 bpp, or colormapped)
 *              pixm (<optional> 1 bpp mask over which average is to be taken;
 *                    use all pixels if null)
 *              x, y (UL corner of pixm relative to the UL corner of pixs; 
 *                    can be < 0)
 *              factor (subsampling factor; >= 1)
 *              &val (<return> RMS value)
 *      Return: 0 if OK, 1 on error
 *  Notes:
 *      (1) Computes the RMS (root mean squared) value of pixels in pixs
 *          that are under the fg of the optional mask.  If the mask
 *          is null, it computes the RMS of the pixels in pixs.
 *      (2) Set the subsampling factor > 1 to reduce the amount of
 *          computation.
 *      (3) Clipping of pixm (if it exists) to pixs is done in the inner loop.
 *      (4) Input x,y are ignored unless pixm exists.
 */
l_int32
pixGetRMSMasked(PIX        *pixs,
                PIX        *pixm,
                l_int32     x,
                l_int32     y,
                l_int32     factor,
                l_float32  *pval)
{
l_int32    i, j, w, h, wm, hm, wplg, wplm, val, count;
l_float32  sum;
l_uint32  *datag, *datam, *lineg, *linem;
PIX       *pixg;

    PROCNAME("pixGetRMSMasked");

    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    if (pixGetDepth(pixs) != 8 && !pixGetColormap(pixs))
        return ERROR_INT("pixs neither 8 bpp nor colormapped", procName, 1);
    if (pixm && pixGetDepth(pixm) != 1)
        return ERROR_INT("pixm not 1 bpp", procName, 1);
    if (factor < 1)
        return ERROR_INT("sampling factor < 1", procName, 1);
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

    sum = 0.0;
    count = 0;
    if (!pixm) {
        for (i = 0; i < h; i += factor) {
            lineg = datag + i * wplg;
            for (j = 0; j < w; j += factor) {
                val = GET_DATA_BYTE(lineg, j);
                sum += val * val;
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
                    sum += val * val;
                    count++;
                }
            }
        }
    }

    pixDestroy(&pixg);
    if (count == 0)
        return ERROR_INT("no pixels sampled", procName, 1);
    *pval = (l_float32)sqrt(sum / (l_float32)count);

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
 *  This should be simple.  Yet it is not, and there are choices to
 *  be made.
 *
 *  The box is defined relative to the pix coordinates.  However,
 *  if the box exceeds the pix boundaries, we have two choices:
 *
 *      (1) clip the box to the pix
 *      (2) make a new pix equal to the full box dimensions,
 *          but let rasterop do the clipping and positioning
 *          of the src with respect to the dest
 *
 *  Choice (2) immediately brings up the problem of what pixel values
 *  to use that were not taken from the src!  For example, on a grayscale
 *  image, do you want the pixels not taken from the src to be black
 *  or white or something else?  To implement choice 2, one needs to
 *  specify the color of these extra pixels.
 *
 *  So we adopt (1), and clip the box first, if necessary,
 *  before making the dest pix and doing the rasterop.  But there
 *  are still problems to consider.
 *
 *  First, imagine that the box has y < 0, so that some of it is
 *  above the src.  If you clip a piece of the image using this box,
 *  you get a Pix with a height less than the box height.  The trouble
 *  comes when you then paste the Pix back in using the same box:
 *  it will be shifted up, and clipped to the top of the dest Pix,
 *  thus losing pixels at the top!  Remember that we are first clipping
 *  the box to the src, and then extracting the pix using the clipped
 *  box.  So to prevent the shift on replacement, it is necessary to
 *  use the clipped box!
 *
 *  Accordingly, this function has a third (optional) argument, which is
 *  the input box clipped to the src pix.
 *
 *  Now, imagine that the box extends past the bottom of the pix:
 *       box->y  >  pixGetHeight(pixs) - 1
 *  This will not cause any trouble on replacement using the
 *  original box, because the piece clipped out will go back
 *  in the same place when replaced.
 *
 *  We're not finished!  Here's a different use.
 *
 *  Suppose you want to clip a small pix (pix2) to a
 *  large one (pix1), and to preserve the alignment for some later operation.
 *  (For example, see blend.c).  The aligment of the two images is
 *  typically given by the origin of the smaller pix2 at (x, y)
 *  relative to the origin of the larger pix1.  Here, the "box" you
 *  use to clip pix2 is actually pix1 (properly translated), and
 *  it is defined by:
 *       box->x = -x
 *       box->y = -y
 *       box->w = pixGetWidth(pix1)
 *       box->h = pixGetHeight(pix1)
 *
 *  Consider again the two cases:
 *     (1) pix2 overhangs pix1 at the bottom, where
 *            y + pixGetHeight(pix2) > pixGetHeight(pix1)
 *         Then the lower part of pix2 is clipped, and
 *         it is properly placed with its origin at (x, y)
 *     (2) pix2 overhangs pix1 at the top, where
 *            y < 0
 *         Then the upper part of the pix2 is clipped, and it is
 *         properly placed with its origin at (x, y = 0)
 *
 *  So the general prescription for this use is:
 *     clipping: boxCreate(-x, -y, pixGetWidth(pix1), pixGetHeight(pix1))
 *     placement: origin (x, y) of pix2 is at:
 *          (L_MAX(0, x),  L_MAX(0, y))
 */
PIX *
pixClipRectangle(PIX   *pixs,
                 BOX   *box,
                 BOX  **pboxc)
{
l_int32  w, h, overw, overh, d;
BOX     *boxc;
PIX     *pixd;

    PROCNAME("pixClipRectangle");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    if (!box)
        return (PIX *)ERROR_PTR("box not defined", procName, NULL);
    if (pboxc)
        *pboxc = NULL;

    pixGetDimensions(pixs, &w, &h, NULL);
    boxc = boxCopy(box);

        /* Clip boxc if necessary */
    if (boxc->x < 0) {
        boxc->w += boxc->x;  /* decrease the width */
        boxc->x = 0;
    }
    overw = boxc->x + boxc->w - w;
    if (overw > 0)
        boxc->w -= overw;  /* decrease the width */
    if (boxc->y < 0) {
        boxc->h += boxc->y;  /* decrease the height */
        boxc->y = 0;
    }
    overh = boxc->y + boxc->h - h;
    if (overh > 0)
        boxc->h -= overh;  /* decrease the height */

        /* Check: any pixels in the box? */
    if (boxc->w == 0 || boxc->h == 0) {  /* box outside of pix */
        boxDestroy(&boxc);
        L_WARNING("box doesn't overlap pix", procName);
        return NULL;
    }

        /* Now, we are guaranteed that boxc fits within pixs,
         * so that all pixels in pixd are written by the rasterop. */
    d = pixGetDepth(pixs);
    if ((pixd = pixCreate(boxc->w, boxc->h, d)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    pixCopyResolution(pixd, pixs);
    pixCopyColormap(pixd, pixs);
    pixRasterop(pixd, 0, 0, boxc->w, boxc->h, PIX_SRC, pixs, boxc->x, boxc->y);

    if (pboxc)
        *pboxc = boxc;
    else
        boxDestroy(&boxc);

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

    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    if ((d = pixGetDepth(pixs)) != 1)
        return ERROR_INT("pixs not binary", procName, 1);
    if (!ppixd && !pbox)
        return ERROR_INT("neither &pixd nor &pbox defined", procName, 1);

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
    if (miny == h) {  /* no ON pixels */
        if (ppixd)
            *ppixd = NULL;
        if (pbox)
            *pbox = NULL;
        return 1;
    }

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

