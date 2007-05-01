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
 *  grayquantlow.c
 *                     
 *      Thresholding from 8 bpp to 1 bpp
 *
 *          Floyd-Steinberg dithering to binary
 *              void       ditherToBinaryLow()
 *              void       ditherToBinaryLineLow()
 *
 *          Simple (pixelwise) binarization
 *              void       thresholdToBinaryLow()
 *              void       thresholdToBinaryLineLow()
 *
 *          A slower version of Floyd-Steinberg dithering that uses LUTs
 *              void       ditherToBinaryLUTLow()
 *              void       ditherToBinaryLineLUTLow()
 *              l_int32    make8To1DitherTables()
 *
 *      Thresholding from 8 bpp to 2 bpp
 *
 *          Floyd-Steinberg-like dithering to 2 bpp
 *              void       ditherTo2bppLow()
 *              void       ditherTo2bppLineLow()
 *              l_int32    make8To2DitherTables()
 *
 *          Simple thresholding to 2 bpp
 *              void       thresholdTo2bppLow()
 *              void       thresholdTo2bppLineLow()
 *
 *      Thresholding from 8 bpp to 4 bpp
 *
 *          Simple thresholding to 4 bpp
 *              void       thresholdTo4bppLow()
 *              void       thresholdTo4bppLineLow()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "allheaders.h"


/*------------------------------------------------------------------*
 *             Binarization by Floyd-Steinberg Dithering            *
 *------------------------------------------------------------------*/
/*
 *  ditherToBinaryLow()
 *
 *  See comments in pixDitherToBinary() in binarize.c
 */
void
ditherToBinaryLow(l_uint32  *datad,
                  l_int32    w,
                  l_int32    h,
                  l_int32    wpld,
                  l_uint32  *datas,
                  l_int32    wpls,
                  l_uint32  *bufs1,
                  l_uint32  *bufs2,
                  l_int32    lowerclip,
                  l_int32    upperclip)
{
l_int32      i;
l_uint32    *lined;

        /* do all lines except last line */
    memcpy(bufs2, datas, 4 * wpls);  /* prime the buffer */
    for (i = 0; i < h - 1; i++) {
        memcpy(bufs1, bufs2, 4 * wpls);
        memcpy(bufs2, datas + (i + 1) * wpls, 4 * wpls);
        lined = datad + i * wpld;
        ditherToBinaryLineLow(lined, w, bufs1, bufs2, lowerclip, upperclip, 0);
    }

        /* do last line */
    memcpy(bufs1, bufs2, 4 * wpls);
    lined = datad + (h - 1) * wpld;
    ditherToBinaryLineLow(lined, w, bufs1, bufs2, lowerclip, upperclip, 1);
    return;
}


/*
 *  ditherToBinaryLineLow()
 *   
 *      Input:  lined  (ptr to beginning of dest line
 *              w   (width of image in pixels)
 *              bufs1 (buffer of current source line)
 *              bufs2 (buffer of next source line)
 *              lowerclip (lower clip distance to black)
 *              upperclip (upper clip distance to white)
 *              lastlineflag  (0 if not last dest line, 1 if last dest line)
 *      Return: void
 *
 *  Dispatches FS error diffusion dithering for
 *  a single line of the image.  If lastlineflag == 0,
 *  both source buffers are used; otherwise, only bufs1
 *  is used.  We use source buffers because the error
 *  is propagated into them, and we don't want to change
 *  the input src image. 
 *
 *  We break dithering out line by line to make it
 *  easier to combine functions like interpolative
 *  scaling and error diffusion dithering, as such a
 *  combination of operations obviates the need to
 *  generate a 2x grayscale image as an intermediary.
 */
void
ditherToBinaryLineLow(l_uint32  *lined,
                      l_int32    w,
                      l_uint32  *bufs1,
                      l_uint32  *bufs2,
                      l_int32    lowerclip,
                      l_int32    upperclip,
                      l_int32    lastlineflag)
{
l_int32   j;
l_int32   oval, eval;
l_uint8   fval1, fval2, rval, bval, dval;

    if (lastlineflag == 0) {
        for (j = 0; j < w - 1; j++) {
            oval = GET_DATA_BYTE(bufs1, j);
            if (oval > 127) {   /* binarize to OFF */
                if ((eval = 255 - oval) > upperclip) {
                        /* subtract from neighbors */
                    fval1 = (3 * eval) / 8;
                    fval2 = eval / 4;
                    rval = GET_DATA_BYTE(bufs1, j + 1);
                    rval = L_MAX(0, rval - fval1);
                    SET_DATA_BYTE(bufs1, j + 1, rval);
                    bval = GET_DATA_BYTE(bufs2, j);
                    bval = L_MAX(0, bval - fval1);
                    SET_DATA_BYTE(bufs2, j, bval);
                    dval = GET_DATA_BYTE(bufs2, j + 1);
                    dval = L_MAX(0, dval - fval2);
                    SET_DATA_BYTE(bufs2, j + 1, dval);
                }
            }
            else {   /* oval <= 127; binarize to ON  */
                SET_DATA_BIT(lined, j);   /* ON pixel */
                if (oval > lowerclip) {
                        /* add to neighbors */
                    fval1 = (3 * oval) / 8;
                    fval2 = oval / 4;
                    rval = GET_DATA_BYTE(bufs1, j + 1);
                    rval = L_MIN(255, rval + fval1);
                    SET_DATA_BYTE(bufs1, j + 1, rval);
                    bval = GET_DATA_BYTE(bufs2, j);
                    bval = L_MIN(255, bval + fval1);
                    SET_DATA_BYTE(bufs2, j, bval);
                    dval = GET_DATA_BYTE(bufs2, j + 1);
                    dval = L_MIN(255, dval + fval2);
                    SET_DATA_BYTE(bufs2, j + 1, dval);
                }
            }
        }

            /* do last column: j = w - 1 */
        oval = GET_DATA_BYTE(bufs1, j);
        if (oval > 127) {  /* binarize to OFF */
            if ((eval = 255 - oval) > upperclip) {
                    /* subtract from neighbors */
                fval1 = (3 * eval) / 8;
                bval = GET_DATA_BYTE(bufs2, j);
                bval = L_MAX(0, bval - fval1);
                SET_DATA_BYTE(bufs2, j, bval);
            }
        }
        else {  /*oval <= 127; binarize to ON */
            SET_DATA_BIT(lined, j);   /* ON pixel */
            if (oval > lowerclip) { 
                    /* add to neighbors */
                fval1 = (3 * oval) / 8;
                bval = GET_DATA_BYTE(bufs2, j);
                bval = L_MIN(255, bval + fval1);
                SET_DATA_BYTE(bufs2, j, bval);
            }
        }
    }
    else {   /* lastlineflag == 1 */
        for (j = 0; j < w - 1; j++) {
            oval = GET_DATA_BYTE(bufs1, j);
            if (oval > 127) {   /* binarize to OFF */
                if ((eval = 255 - oval) > upperclip) {
                        /* subtract from neighbors */
                    fval1 = (3 * eval) / 8;
                    rval = GET_DATA_BYTE(bufs1, j + 1);
                    rval = L_MAX(0, rval - fval1);
                    SET_DATA_BYTE(bufs1, j + 1, rval);
                }
            }
            else {   /* oval <= 127; binarize to ON  */
                SET_DATA_BIT(lined, j);   /* ON pixel */
                if (oval > lowerclip) { 
                        /* add to neighbors */
                    fval1 = (3 * oval) / 8;
                    rval = GET_DATA_BYTE(bufs1, j + 1);
                    rval = L_MIN(255, rval + fval1);
                    SET_DATA_BYTE(bufs1, j + 1, rval);
                }
            }
        }

            /* do last pixel: (i, j) = (h - 1, w - 1) */
        oval = GET_DATA_BYTE(bufs1, j);
        if (oval < 128)
            SET_DATA_BIT(lined, j);   /* ON pixel */
    }

    return;
}



/*------------------------------------------------------------------*
 *             Simple binarization with fixed threshold             *
 *------------------------------------------------------------------*/
/*
 *  thresholdToBinaryLow()
 *
 *  If the source pixel is less than thresh,
 *  the dest will be 1; otherwise, it will be 0
 */
void
thresholdToBinaryLow(l_uint32  *datad,
                     l_int32    w,
                     l_int32    h,
                     l_int32    wpld,
                     l_uint32  *datas,
                     l_int32    d,
                     l_int32    wpls,
                     l_int32    thresh)
{
l_int32    i;
l_uint32  *lines, *lined;

    for (i = 0; i < h; i++) {
        lines = datas + i * wpls;
        lined = datad + i * wpld;
        thresholdToBinaryLineLow(lined, w, lines, d, thresh);
    }
    return;
}


/*
 *  thresholdToBinaryLineLow()
 *
 */
void
thresholdToBinaryLineLow(l_uint32  *lined,
                         l_int32    w,
                         l_uint32  *lines,
                         l_int32    d,
                         l_int32    thresh)
{
l_int32  j, gval;

    PROCNAME("thresholdToBinaryLineLow");

    switch (d)
    {
    case 4:
        for (j = 0; j < w; j++) {
            gval = GET_DATA_QBIT(lines, j);
            if (gval < thresh) 
                SET_DATA_BIT(lined, j);
        }
        break;
    case 8:
        for (j = 0; j < w; j++) {
            gval = GET_DATA_BYTE(lines, j);
            if (gval < thresh) 
                SET_DATA_BIT(lined, j);
        }
        break;
    default:
        ERROR_VOID("src depth not 4 or 8 bpp", procName);
        break;
    }
    return;
}


/*---------------------------------------------------------------------*
 *    Alternate implementation of dithering that uses lookup tables.   *
 *    This is analogous to the method used in dithering to 2 bpp.      *
 *---------------------------------------------------------------------*/
/*!
 *  ditherToBinaryLUTLow()
 *
 *  Low-level function for doing Floyd-Steinberg error diffusion
 *  dithering from 8 bpp (datas) to 1 bpp (datad).  Two source
 *  line buffers, bufs1 and bufs2, are provided, along with three
 *  256-entry lookup tables: tabval gives the output pixel value,
 *  tab38 gives the extra (plus or minus) transferred to the pixels
 *  directly to the left and below, and tab14 gives the extra
 *  transferred to the diagonal below.  The choice of 3/8 and 1/4
 *  is traditional but arbitrary when you use a lookup table; the
 *  only constraint is that the sum is 1.  See other comments below.
 */
void
ditherToBinaryLUTLow(l_uint32  *datad,
                     l_int32    w,
                     l_int32    h,
                     l_int32    wpld,
                     l_uint32  *datas,
                     l_int32    wpls,
                     l_uint32  *bufs1,
                     l_uint32  *bufs2,
                     l_int32   *tabval,
                     l_int32   *tab38,
                     l_int32   *tab14)
{
l_int32      i;
l_uint32    *lined;

        /* do all lines except last line */
    memcpy(bufs2, datas, 4 * wpls);  /* prime the buffer */
    for (i = 0; i < h - 1; i++) {
        memcpy(bufs1, bufs2, 4 * wpls);
        memcpy(bufs2, datas + (i + 1) * wpls, 4 * wpls);
        lined = datad + i * wpld;
        ditherToBinaryLineLUTLow(lined, w, bufs1, bufs2,
                                 tabval, tab38, tab14, 0);
    }

        /* do last line */
    memcpy(bufs1, bufs2, 4 * wpls);
    lined = datad + (h - 1) * wpld;
    ditherToBinaryLineLUTLow(lined, w, bufs1, bufs2, tabval, tab38, tab14,  1);
    return;
}


/*!
 *  ditherToBinaryLineLUTLow()
 *   
 *      Input:  lined  (ptr to beginning of dest line
 *              w   (width of image in pixels)
 *              bufs1 (buffer of current source line)
 *              bufs2 (buffer of next source line)
 *              tabval (value to assign for current pixel)
 *              tab38 (excess value to give to neighboring 3/8 pixels)
 *              tab14 (excess value to give to neighboring 1/4 pixel)
 *              lastlineflag  (0 if not last dest line, 1 if last dest line)
 *      Return: void
 */
void
ditherToBinaryLineLUTLow(l_uint32  *lined,
                         l_int32    w,
                         l_uint32  *bufs1,
                         l_uint32  *bufs2,
                         l_int32   *tabval,
                         l_int32   *tab38,
                         l_int32   *tab14,
                         l_int32    lastlineflag)
{
l_int32  j;
l_int32  oval, tab38val, tab14val;
l_uint8  rval, bval, dval;

    if (lastlineflag == 0) {
        for (j = 0; j < w - 1; j++) {
            oval = GET_DATA_BYTE(bufs1, j);
            if (tabval[oval])
                SET_DATA_BIT(lined, j);
            rval = GET_DATA_BYTE(bufs1, j + 1);
            bval = GET_DATA_BYTE(bufs2, j);
            dval = GET_DATA_BYTE(bufs2, j + 1);
            tab38val = tab38[oval];
            if (tab38val == 0)
                continue;
            tab14val = tab14[oval];
            if (tab38val < 0) {
                rval = L_MAX(0, rval + tab38val);
                bval = L_MAX(0, bval + tab38val);
                dval = L_MAX(0, dval + tab14val);
            }
            else  {
                rval = L_MIN(255, rval + tab38val);
                bval = L_MIN(255, bval + tab38val);
                dval = L_MIN(255, dval + tab14val);
            }
            SET_DATA_BYTE(bufs1, j + 1, rval);
            SET_DATA_BYTE(bufs2, j, bval);
            SET_DATA_BYTE(bufs2, j + 1, dval);
        }

            /* do last column: j = w - 1 */
        oval = GET_DATA_BYTE(bufs1, j);
        if (tabval[oval])
            SET_DATA_BIT(lined, j);
        bval = GET_DATA_BYTE(bufs2, j);
        tab38val = tab38[oval];
        if (tab38val < 0) {
            bval = L_MAX(0, bval + tab38val);
            SET_DATA_BYTE(bufs2, j, bval);
        }
        else if (tab38val > 0 ) {
            bval = L_MIN(255, bval + tab38val);
            SET_DATA_BYTE(bufs2, j, bval);
        }
    }
    else {   /* lastlineflag == 1 */
        for (j = 0; j < w - 1; j++) {
            oval = GET_DATA_BYTE(bufs1, j);
            if (tabval[oval])
                SET_DATA_BIT(lined, j);
            rval = GET_DATA_BYTE(bufs1, j + 1);
            tab38val = tab38[oval];
            if (tab38val == 0)
                continue;
            if (tab38val < 0)
                rval = L_MAX(0, rval + tab38val);
            else
                rval = L_MIN(255, rval + tab38val);
            SET_DATA_BYTE(bufs1, j + 1, rval);
        }

            /* do last pixel: (i, j) = (h - 1, w - 1) */
        oval = GET_DATA_BYTE(bufs1, j);
        if (tabval[oval])
            SET_DATA_BIT(lined, j);
    }

    return;
}


/*!
 *  make8To1DitherTables()
 *
 *      Input: &tabval (value assigned to output pixel; 0 or 1)
 *             &tab38  (amount propagated to pixels left and below)
 *             &tab14  (amount propagated to pixel to left and down)
 *             lowerclip (values near 0 where the excess is not propagated)
 *             upperclip (values near 255 where the deficit is not propagated)
 *
 *      Return: 0 if OK, 1 on error
 */
l_int32
make8To1DitherTables(l_int32 **ptabval,
                     l_int32 **ptab38,
                     l_int32 **ptab14,
                     l_int32   lowerclip,
                     l_int32   upperclip)
{
l_int32   i;
l_int32  *tabval, *tab38, *tab14;

    PROCNAME("make8To1DitherTables");

    if (!ptabval || !ptab38 || !ptab14)
        return ERROR_INT("table ptrs not all defined", procName, 1);

        /* 3 lookup tables: 1-bit value, (3/8)excess, and (1/4)excess */
    if ((tabval = (l_int32 *)CALLOC(256, sizeof(l_int32))) == NULL)
        return ERROR_INT("tabval not made", procName, 1);
    if ((tab38 = (l_int32 *)CALLOC(256, sizeof(l_int32))) == NULL)
        return ERROR_INT("tab38 not made", procName, 1);
    if ((tab14 = (l_int32 *)CALLOC(256, sizeof(l_int32))) == NULL)
        return ERROR_INT("tab14 not made", procName, 1);
    *ptabval = tabval;
    *ptab38 = tab38;
    *ptab14 = tab14;

    for (i = 0; i < 256; i++) {
        if (i <= lowerclip) {
            tabval[i] = 1;
            tab38[i] = 0;
            tab14[i] = 0;
        }
        else if (i < 128) {
            tabval[i] = 1;
            tab38[i] = (3 * i + 4) / 8;
            tab14[i] = (i + 2) / 4;
        }
        else if (i < 255 - upperclip) {
            tabval[i] = 0;
            tab38[i] = (3 * (i - 255) + 4) / 8;
            tab14[i] = ((i - 255) + 2) / 4;
        }
        else {  /* i >= 255 - upperclip */
            tabval[i] = 0;
            tab38[i] = 0;
            tab14[i] = 0;
        }
    }

    return 0;
}


/*------------------------------------------------------------------*
 *                         Dithering to 2 bpp                       *
 *------------------------------------------------------------------*/
/*
 *  ditherTo2bppLow()
 *
 *  Low-level function for doing Floyd-Steinberg error diffusion
 *  dithering from 8 bpp (datas) to 2 bpp (datad).  Two source
 *  line buffers, bufs1 and bufs2, are provided, along with three
 *  256-entry lookup tables: tabval gives the output pixel value,
 *  tab38 gives the extra (plus or minus) transferred to the pixels
 *  directly to the left and below, and tab14 gives the extra
 *  transferred to the diagonal below.  The choice of 3/8 and 1/4
 *  is traditional but arbitrary when you use a lookup table; the
 *  only constraint is that the sum is 1.  See other comments
 *  below and in grayquant.c.
 */
void
ditherTo2bppLow(l_uint32  *datad,
                l_int32    w,
                l_int32    h,
                l_int32    wpld,
                l_uint32  *datas,
                l_int32    wpls,
                l_uint32  *bufs1,
                l_uint32  *bufs2,
                l_int32   *tabval,
                l_int32   *tab38,
                l_int32   *tab14)
{
l_int32      i;
l_uint32    *lined;

        /* do all lines except last line */
    memcpy(bufs2, datas, 4 * wpls);  /* prime the buffer */
    for (i = 0; i < h - 1; i++) {
        memcpy(bufs1, bufs2, 4 * wpls);
        memcpy(bufs2, datas + (i + 1) * wpls, 4 * wpls);
        lined = datad + i * wpld;
        ditherTo2bppLineLow(lined, w, bufs1, bufs2, tabval, tab38, tab14, 0);
    }

        /* do last line */
    memcpy(bufs1, bufs2, 4 * wpls);
    lined = datad + (h - 1) * wpld;
    ditherTo2bppLineLow(lined, w, bufs1, bufs2, tabval, tab38, tab14, 1);
    return;
}


/*
 *  ditherTo2bppLineLow()
 *   
 *      Input:  lined  (ptr to beginning of dest line
 *              w   (width of image in pixels)
 *              bufs1 (buffer of current source line)
 *              bufs2 (buffer of next source line)
 *              tabval (value to assign for current pixel)
 *              tab38 (excess value to give to neighboring 3/8 pixels)
 *              tab14 (excess value to give to neighboring 1/4 pixel)
 *              lastlineflag  (0 if not last dest line, 1 if last dest line)
 *      Return: void
 *
 *  Dispatches error diffusion dithering for
 *  a single line of the image.  If lastlineflag == 0,
 *  both source buffers are used; otherwise, only bufs1
 *  is used.  We use source buffers because the error
 *  is propagated into them, and we don't want to change
 *  the input src image. 
 *
 *  We break dithering out line by line to make it
 *  easier to combine functions like interpolative
 *  scaling and error diffusion dithering, as such a
 *  combination of operations obviates the need to
 *  generate a 2x grayscale image as an intermediary.
 */
void
ditherTo2bppLineLow(l_uint32  *lined,
                    l_int32    w,
                    l_uint32  *bufs1,
                    l_uint32  *bufs2,
                    l_int32   *tabval,
                    l_int32   *tab38,
                    l_int32   *tab14,
                    l_int32    lastlineflag)
{
l_int32  j;
l_int32  oval, tab38val, tab14val;
l_uint8  rval, bval, dval;

    if (lastlineflag == 0) {
        for (j = 0; j < w - 1; j++) {
            oval = GET_DATA_BYTE(bufs1, j);
            SET_DATA_DIBIT(lined, j, tabval[oval]);
            rval = GET_DATA_BYTE(bufs1, j + 1);
            bval = GET_DATA_BYTE(bufs2, j);
            dval = GET_DATA_BYTE(bufs2, j + 1);
            tab38val = tab38[oval];
            tab14val = tab14[oval];
            if (tab38val < 0) {
                rval = L_MAX(0, rval + tab38val);
                bval = L_MAX(0, bval + tab38val);
                dval = L_MAX(0, dval + tab14val);
            }
            else {
                rval = L_MIN(255, rval + tab38val);
                bval = L_MIN(255, bval + tab38val);
                dval = L_MIN(255, dval + tab14val);
            }
            SET_DATA_BYTE(bufs1, j + 1, rval);
            SET_DATA_BYTE(bufs2, j, bval);
            SET_DATA_BYTE(bufs2, j + 1, dval);
        }

            /* do last column: j = w - 1 */
        oval = GET_DATA_BYTE(bufs1, j);
        SET_DATA_DIBIT(lined, j, tabval[oval]);
        bval = GET_DATA_BYTE(bufs2, j);
        tab38val = tab38[oval];
        if (tab38val < 0)
            bval = L_MAX(0, bval + tab38val);
        else
            bval = L_MIN(255, bval + tab38val);
        SET_DATA_BYTE(bufs2, j, bval);
    }
    else {   /* lastlineflag == 1 */
        for (j = 0; j < w - 1; j++) {
            oval = GET_DATA_BYTE(bufs1, j);
            SET_DATA_DIBIT(lined, j, tabval[oval]);
            rval = GET_DATA_BYTE(bufs1, j + 1);
            tab38val = tab38[oval];
            if (tab38val < 0)
                rval = L_MAX(0, rval + tab38val);
            else
                rval = L_MIN(255, rval + tab38val);
            SET_DATA_BYTE(bufs1, j + 1, rval);
        }

            /* do last pixel: (i, j) = (h - 1, w - 1) */
        oval = GET_DATA_BYTE(bufs1, j);
        SET_DATA_DIBIT(lined, j, tabval[oval]);
    }

    return;
}


/*!
 *  make8To2DitherTables()
 *
 *      Input: &tabval (value assigned to output pixel; 0, 1, 2 or 3)
 *             &tab38  (amount propagated to pixels left and below)
 *             &tab14  (amount propagated to pixel to left and down)
 *             cliptoblack (values near 0 where the excess is not propagated)
 *             cliptowhite (values near 255 where the deficit is not propagated)
 *
 *      Return: 0 if OK, 1 on error
 */
l_int32
make8To2DitherTables(l_int32 **ptabval,
                     l_int32 **ptab38,
                     l_int32 **ptab14,
                     l_int32   cliptoblack,
                     l_int32   cliptowhite)
{
l_int32   i;
l_int32  *tabval, *tab38, *tab14;

    PROCNAME("make8To2DitherTables");

        /* 3 lookup tables: 2-bit value, (3/8)excess, and (1/4)excess */
    if ((tabval = (l_int32 *)CALLOC(256, sizeof(l_int32))) == NULL)
        return ERROR_INT("tabval not made", procName, 1);
    if ((tab38 = (l_int32 *)CALLOC(256, sizeof(l_int32))) == NULL)
        return ERROR_INT("tab38 not made", procName, 1);
    if ((tab14 = (l_int32 *)CALLOC(256, sizeof(l_int32))) == NULL)
        return ERROR_INT("tab14 not made", procName, 1);
    *ptabval = tabval;
    *ptab38 = tab38;
    *ptab14 = tab14;

    for (i = 0; i < 256; i++) {
        if (i <= cliptoblack) {
            tabval[i] = 0;
            tab38[i] = 0;
            tab14[i] = 0;
        }
        else if (i < 43) {
            tabval[i] = 0;
            tab38[i] = (3 * i + 4) / 8;
            tab14[i] = (i + 2) / 4;
        }
        else if (i < 85) {
            tabval[i] = 1;
            tab38[i] = (3 * (i - 85) - 4) / 8;
            tab14[i] = ((i - 85) - 2) / 4;
        }
        else if (i < 128) {
            tabval[i] = 1;
            tab38[i] = (3 * (i - 85) + 4) / 8;
            tab14[i] = ((i - 85) + 2) / 4;
        }
        else if (i < 170) {
            tabval[i] = 2;
            tab38[i] = (3 * (i - 170) - 4) / 8;
            tab14[i] = ((i - 170) - 2) / 4;
        }
        else if (i < 213) {
            tabval[i] = 2;
            tab38[i] = (3 * (i - 170) + 4) / 8;
            tab14[i] = ((i - 170) + 2) / 4;
        }
        else if (i < 255 - cliptowhite) {
            tabval[i] = 3;
            tab38[i] = (3 * (i - 255) - 4) / 8;
            tab14[i] = ((i - 255) - 2) / 4;
        }
        else {  /* i >= 255 - cliptowhite */
            tabval[i] = 3;
            tab38[i] = 0;
            tab14[i] = 0;
        }
    }

#if 0
    for (i = 0; i < 256; i++)
        fprintf(stderr, "tabval[%d] = %d, tab38[%d] = %d, tab14[%d] = %d\n",
                i, tabval[i], i, tab38[i], i, tab14[i]);
#endif

    return 0;
}


/*------------------------------------------------------------------*
 *                   Simple thresholding to 2 bpp                   *
 *------------------------------------------------------------------*/
/*
 *  thresholdTo2bppLow()
 *
 *  Low-level function for thresholding from 8 bpp (datas) to
 *  2 bpp (datad).  If tabval is defined, it uses the thresholds
 *  that are implicitly defined by the table, which is a 256-entry
 *  lookup table that gives the 2-bit output value for each 
 *  possible input.
 */
void
thresholdTo2bppLow(l_uint32  *datad,
                   l_int32    h,
                   l_int32    wpld,
                   l_uint32  *datas,
                   l_int32    wpls,
                   l_int32   *tab)
{
l_int32    i;
l_uint32  *lines, *lined;

    for (i = 0; i < h; i++) {
        lines = datas + i * wpls;
        lined = datad + i * wpld;
        thresholdTo2bppLineLow(lined, lines, wpls, tab);
    }
    return;
}


/*
 *  thresholdTo2bppLineLow()
 *
 *  Unroll the loop to the extent that for each 32 bit src word,
 *  representing four consecutive 8-bit pixels, we compose one byte
 *  of output consisiting of four 2-bit pixels.
 */
void
thresholdTo2bppLineLow(l_uint32  *lined,
                       l_uint32  *lines,
                       l_int32    wpls,
                       l_int32   *tab)
{
l_uint8   sval1, sval2, sval3, sval4, dval;
l_int32   j, k;

    for (j = 0; j < wpls; j++) {
        k = 4 * j;
        sval1 = GET_DATA_BYTE(lines, k);
        sval2 = GET_DATA_BYTE(lines, k + 1);
        sval3 = GET_DATA_BYTE(lines, k + 2);
        sval4 = GET_DATA_BYTE(lines, k + 3);
        dval = (tab[sval1] << 6) | (tab[sval2] << 4) |
               (tab[sval3] << 2) | tab[sval4];
        SET_DATA_BYTE(lined, j, dval);
    }
    return;
}


/*------------------------------------------------------------------*
 *                   Simple thresholding to 4 bpp                   *
 *------------------------------------------------------------------*/
/*
 *  thresholdTo4bppLow()
 *
 *  Low-level function for thresholding from 8 bpp (datas) to
 *  4 bpp (datad), using thresholds implicitly defined
 *  through the tabval, a 256-entry lookup table that gives
 *  a 4-bit output value for each possible input.
 */
void
thresholdTo4bppLow(l_uint32  *datad,
                   l_int32    h,
                   l_int32    wpld,
                   l_uint32  *datas,
                   l_int32    wpls,
                   l_int32   *tab)
{
l_int32    i;
l_uint32  *lines, *lined;

    for (i = 0; i < h; i++) {
        lines = datas + i * wpls;
        lined = datad + i * wpld;
        thresholdTo4bppLineLow(lined, lines, wpls, tab);
    }
    return;
}


/*
 *  thresholdTo4bppLineLow()
 *
 *  Unroll the loop to the extent that for each 32 bit src word,
 *  representing four consecutive 8-bit pixels, we compose two bytes
 *  of output consisiting of four 4-bit pixels.
 */
void
thresholdTo4bppLineLow(l_uint32  *lined,
                       l_uint32  *lines,
                       l_int32    wpls,
                       l_int32   *tab)
{
l_uint8   sval1, sval2, sval3, sval4;
l_uint16  dval;
l_int32   j, k;

    for (j = 0; j < wpls; j++) {
        k = 4 * j;
        sval1 = GET_DATA_BYTE(lines, k);
        sval2 = GET_DATA_BYTE(lines, k + 1);
        sval3 = GET_DATA_BYTE(lines, k + 2);
        sval4 = GET_DATA_BYTE(lines, k + 3);
        dval = (tab[sval1] << 12) | (tab[sval2] << 8) |
               (tab[sval3] << 4) | tab[sval4];
        SET_DATA_TWO_BYTES(lined, j, dval);
    }
    return;
}


