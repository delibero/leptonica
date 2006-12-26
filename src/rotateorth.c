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
 *  rotateorth.c
 *
 *      180-degree rotation
 *            PIX     *pixRotate180()
 *
 *      90-degree rotation (both directions)
 *            PIX     *pixRotate90()
 *
 *      Left-right flip
 *            PIX     *pixFlipLR()
 *
 *      Top-bottom flip
 *            PIX     *pixFlipTB()
 */

#include <stdio.h>
#include <stdlib.h>
#include "allheaders.h"


/*!
 *  pixRotate180()
 *
 *      Input:  pixd  (<optional>; can be equal to pixs)
 *              pixs
 *      Return: pixd
 *
 *  Notes:
 *      (1) This does a 180 rotation of the image about the center,
 *          which is equivalent to a left-right flip about a vertical
 *          line through the image center, followed by a top-bottom
 *          flip about a horizontal line through the image center.
 *      (2) There are 3 cases for input:
 *          (i) pixd = NULL (creates a new pixd)
 *          (ii) pixd == pixs (in-place operation)
 *          (iii) pixd defined but != pixs (puts result in pixd, by first
 *                copying to pixd and then doing an in-place operation)
 *      (3) For case (iii), pixd and pixs must be the same size and depth
 */
PIX *
pixRotate180(PIX  *pixd,
             PIX  *pixs)
{
l_int32  d;

    PROCNAME("pixRotate180");

    if (!pixs)
	return (PIX *)ERROR_PTR("pixs not defined", procName, pixd);
    d = pixGetDepth(pixs);
    if (d != 1 && d != 2 && d != 4 && d != 8 && d != 16 && d != 32)
	return (PIX *)ERROR_PTR("pixs not in {1,2,4,8,16,32} bpp",
                                procName, pixd);

    if (pixs != pixd) {
	if ((pixd = pixCopy(pixd, pixs)) == NULL)
	    return (PIX *)ERROR_PTR("copy fail", procName, pixd);
    }

    pixFlipLR(pixd, pixd);
    pixFlipTB(pixd, pixd);
    return pixd;
}
    

/*!
 *  pixRotate90()
 *
 *      Input:  pixs
 *              direction (1 = clockwise,  -1 = counter-clockwise)
 *      Return: pixd, or null on error
 *
 *  Notes:
 *      (1) This does a 90 degree rotation of the image about the center,
 *          either cw or ccw, returning a new pix.
 *      (2) The direction must be either 1 (cw) or -1 (ccw).
 */
PIX *
pixRotate90(PIX     *pixs,
            l_int32  direction)
{
l_int32    wd, hd, d, wpls, wpld;
l_uint32  *datas, *datad;
PIX       *pixd;

    PROCNAME("pixRotate90");

    if (!pixs)
	return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    d = pixGetDepth(pixs);
    if (d != 1 && d != 2 && d != 4 && d != 8 && d != 16 && d != 32)
	return (PIX *)ERROR_PTR("pixs not in {1,2,4,8,16,32} bpp",
                                procName, NULL);
    if (direction != 1 && direction != -1)
	return (PIX *)ERROR_PTR("invalid direction", procName, NULL);

    hd = pixGetWidth(pixs);
    wd = pixGetHeight(pixs);
    if ((pixd = pixCreate(wd, hd, d)) == NULL)
	return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    pixCopyColormap(pixd, pixs);
    pixCopyResolution(pixd, pixs);
    pixCopyInputFormat(pixd, pixs);

    datas = pixGetData(pixs);
    wpls = pixGetWpl(pixs);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);

    rotate90Low(datad, wd, hd, d, wpld, datas, wpls, direction);

    return pixd;
}


/*!
 *  pixFlipLR()
 *
 *      Input:  pixd  (<optional>; can be equal to pixs)
 *              pixs
 *      Return: pixd
 *
 *  Notes:
 *      (1) This does a left-right flip of the image, which is
 *          equivalent to a rotation out of the plane about a
 *          vertical line through the image center.
 *      (2) There are 3 cases for input:
 *          (i) pixd = NULL (creates a new pixd)
 *          (ii) pixd == pixs (in-place operation)
 *          (iii) pixd defined but != pixs (puts result in pixd, by first
 *                copying to pixd and then doing an in-place operation)
 *      (3) For case (iii), pixd and pixs must be the same size and depth
 */
PIX *
pixFlipLR(PIX  *pixd,
          PIX  *pixs)
{
l_uint8   *tab;
l_int32    w, h, d, wpld;
l_uint32  *datad, *buffer;

    PROCNAME("pixFlipLR");

    if (!pixs)
	return (PIX *)ERROR_PTR("pixs not defined", procName, pixd);
    d = pixGetDepth(pixs);
    if (d != 1 && d != 2 && d != 4 && d != 8 && d != 16 && d != 32)
	return (PIX *)ERROR_PTR("pixs not in {1,2,4,8,16,32} bpp",
                                procName, pixd);

    if (pixs != pixd) {
	if ((pixd = pixCopy(pixd, pixs)) == NULL)
	    return (PIX *)ERROR_PTR("copy fail", procName, pixd);
    }

    w = pixGetWidth(pixd);
    h = pixGetHeight(pixd);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);

    switch (d)
    {
    case 1:
        if ((tab = makeReverseByteTab1()) == NULL)
  	    return (PIX *)ERROR_PTR("tab not made", procName, pixd);
        break;
    case 2:
        if ((tab = makeReverseByteTab2()) == NULL)
  	    return (PIX *)ERROR_PTR("tab not made", procName, pixd);
        break;
    case 4:
        if ((tab = makeReverseByteTab4()) == NULL)
  	    return (PIX *)ERROR_PTR("tab not made", procName, pixd);
        break;
    default:
        tab = NULL;
        break;
    }

    if ((buffer = (l_uint32 *)CALLOC(wpld, sizeof(l_uint32))) == NULL)
	return (PIX *)ERROR_PTR("buffer not made", procName, pixd);

    flipLRLow(datad, w, h, d, wpld, tab, buffer);

    FREE(buffer);
    if (tab) FREE(tab);
    return pixd;
}


/*!
 *  pixFlipTB()
 *
 *      Input:  pixd  (<optional>; can be equal to pixs)
 *              pixs
 *      Return: pixd
 *
 *  Notes:
 *      (1) This does a top-bottom flip of the image, which is
 *          equivalent to a rotation out of the plane about a
 *          horizontal line through the image center.
 *      (2) There are 3 cases for input:
 *          (i) pixd = NULL (creates a new pixd)
 *          (ii) pixd == pixs (in-place operation)
 *          (iii) pixd defined but != pixs (puts result in pixd, by first
 *                copying to pixd and then doing an in-place operation)
 *      (3) For case (iii), pixd and pixs must be the same size and depth
 */
PIX *
pixFlipTB(PIX  *pixd,
          PIX  *pixs)
{
l_int32    h, d, wpld;
l_uint32  *datad, *buffer;

    PROCNAME("pixFlipTB");

    if (!pixs)
	return (PIX *)ERROR_PTR("pixs not defined", procName, pixd);
    d = pixGetDepth(pixs);
    if (d != 1 && d != 2 && d != 4 && d != 8 && d != 16 && d != 32)
	return (PIX *)ERROR_PTR("pixs not in {1,2,4,8,16,32} bpp",
                                procName, pixd);

    if (pixs != pixd) {
	if ((pixd = pixCopy(pixd, pixs)) == NULL)
	    return (PIX *)ERROR_PTR("copy fail", procName, pixd);
    }

    h = pixGetHeight(pixd);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);

    if ((buffer = (l_uint32 *)CALLOC(wpld, sizeof(l_uint32))) == NULL)
	return (PIX *)ERROR_PTR("buffer not made", procName, pixd);

    flipTBLow(datad, h, wpld, buffer);

    FREE((void *)buffer);
    return pixd;
}

