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
 *  graphics.c
 *                     
 *      Pta generation for arbitrary shapes built with lines
 *
 *          PTA        *ptaGenerateLine()
 *          PTA        *ptaGenerateWideLine()
 *          PTA        *ptaGenerateBox()
 *          PTA        *ptaGenerateBoxa()
 *          PTA        *ptaGeneratePolyline()
 *
 *      Pta rendering
 *
 *          l_int32     pixRenderPta()
 *          l_int32     pixRenderPtaArb()
 *          l_int32     pixRenderPtaBlend()
 *
 *      Rendering of arbitrary shapes built with lines
 *
 *          l_int32     pixRenderLine()
 *          l_int32     pixRenderLineArb()
 *          l_int32     pixRenderLineBlend()
 *
 *          l_int32     pixRenderBox()
 *          l_int32     pixRenderBoxArb()
 *          l_int32     pixRenderBoxBlend()
 *
 *          l_int32     pixRenderBoxa()
 *          l_int32     pixRenderBoxaArb()
 *          l_int32     pixRenderBoxaBlend()
 *
 *          l_int32     pixRenderPolyline()
 *          l_int32     pixRenderPolylineArb()
 *          l_int32     pixRenderPolylineBlend()
 *
 *      Contour rendering on grayscale images
 *
 *          PIX        *pixRenderContours()
 *
 *  The line rendering functions are relatively crude, but they
 *  get the job done for most simple situations.  We use the pta
 *  as an intermediate data structure.  A pta is generated
 *  for a line.  One of two rendering functions are used to
 *  render this onto a Pix.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "allheaders.h"



/*------------------------------------------------------------------*
 *        Pta generation for arbitrary shapes built with lines      *
 *------------------------------------------------------------------*/
/*!
 *  ptaGenerateLine()
 *
 *      Input:  x1, y1  (end point 1)
 *              x2, y2  (end point 2)
 *      Return: pta, or null on error
 */
PTA  *
ptaGenerateLine(l_int32  x1,
                l_int32  y1,
	        l_int32  x2,
	        l_int32  y2)
{
l_int32    npts, diff, getyofx, sign, i, x, y;
l_float32  slope;
PTA       *pta;

    PROCNAME("ptaGenerateLine");

	/* generate line parameters */
    if (L_ABS(x2 - x1) >= L_ABS(y2 - y1)) {
	getyofx = TRUE;
	npts = L_ABS(x2 - x1) + 1;
	diff = x2 - x1;
	sign = L_SIGN(x2 - x1);
	slope = (l_float32)(sign * (y2 - y1)) / (l_float32)diff;
    }
    else {
	getyofx = FALSE;
	npts = L_ABS(y2 - y1) + 1;
	diff = y2 - y1;
	sign = L_SIGN(y2 - y1);
	slope = (l_float32)(sign * (x2 - x1)) / (l_float32)diff;
    }

    if ((pta = ptaCreate(npts)) == NULL)
	return (PTA *)ERROR_PTR("pta not made", procName, NULL);

    if (npts == 1) {  /* degenerate case */
	ptaAddPt(pta, x1, y1);
	return pta;
    }

	/* generate the set of points */
    if (getyofx) {  /* y = y(x) */
	for (i = 0; i < npts; i++) {
	    x = x1 + sign * i;
	    y = (l_int32)(y1 + (l_float32)i * slope + 0.5);
	    ptaAddPt(pta, x, y);
	}
    }
    else {   /* x = x(y) */
	for (i = 0; i < npts; i++) {
	    x = (l_int32)(x1 + (l_float32)i * slope + 0.5);
	    y = y1 + sign * i;
	    ptaAddPt(pta, x, y);
	}
    }

    return pta;
}


/*!
 *  ptaGenerateWideLine()
 *
 *      Input:  x1, y1  (end point 1)
 *              x2, y2  (end point 2)
 *      Return: ptaj, or null on error
 */
PTA  *
ptaGenerateWideLine(l_int32  x1,
                    l_int32  y1,
	            l_int32  x2,
	            l_int32  y2,
                    l_int32  width)
{
l_int32  i, x1a, x2a, y1a, y2a;
PTA     *pta, *ptaj;

    PROCNAME("ptaGenerateWideLine");

    if (width < 1) {
        L_WARNING("width < 1; setting to 1", procName);
        width = 1;
    }

    if ((ptaj = ptaGenerateLine(x1, y1, x2, y2)) == NULL)
	return (PTA *)ERROR_PTR("ptaj not made", procName, NULL);
    if (width == 1)
        return ptaj;

	/* width > 1; estimate line direction & join */
    if (L_ABS(x1 - x2) > L_ABS(y1 - y2)) {  /* "horizontal" line  */
	for (i = 1; i < width; i++) {
	    if ((i & 1) == 1) {   /* place above */
		y1a = y1 - (i + 1) / 2;
		y2a = y2 - (i + 1) / 2;
	    }
	    else {  /* place below */
		y1a = y1 + (i + 1) / 2;
		y2a = y2 + (i + 1) / 2;
	    }
	    if ((pta = ptaGenerateLine(x1, y1a, x2, y2a)) == NULL)
		return (PTA *)ERROR_PTR("pta not made", procName, NULL);
            ptaJoin(ptaj, pta, 0, 0);
	    ptaDestroy(&pta);
	}
    }
    else  {  /* "vertical" line  */
	for (i = 1; i < width; i++) {
	    if ((i & 1) == 1) {   /* place to left */
		x1a = x1 - (i + 1) / 2;
		x2a = x2 - (i + 1) / 2;
	    }
	    else {  /* place to right */
		x1a = x1 + (i + 1) / 2;
		x2a = x2 + (i + 1) / 2;
	    }
	    if ((pta = ptaGenerateLine(x1a, y1, x2a, y2)) == NULL)
		return (PTA *)ERROR_PTR("pta not made", procName, NULL);
            ptaJoin(ptaj, pta, 0, 0);
	    ptaDestroy(&pta);
	}
    }

    return ptaj;
}


/*!
 *  ptaGenerateBox()
 *
 *      Input:  box
 *              width
 *      Return: ptad, or null on error
 *
 *  Notes:
 *      (1) Because the box is constructed so that we don't have any
 *          overlapping lines, there is no need to remove duplicates.
 */
PTA  *
ptaGenerateBox(BOX     *box,
               l_int32  width)
{
l_int32  x, y, w, h;
PTA     *ptad, *pta;

    PROCNAME("ptaGenerateBox");

    if (!box)
	return (PTA *)ERROR_PTR("box not defined", procName, NULL);

	/* Generate line points and add them to the pta. */
    boxGetGeometry(box, &x, &y, &w, &h);
    ptad = ptaCreate(0);
    if ((width & 1) == 1) {   /* odd width */
	pta = ptaGenerateWideLine(x - width / 2, y,
                                  x + w - 1 + width / 2, y, width);
	ptaJoin(ptad, pta, 0, 0);
	ptaDestroy(&pta);
	pta = ptaGenerateWideLine(x - width / 2, y + h - 1,
                                  x + w - 1 + width / 2, y + h - 1, width);
	ptaJoin(ptad, pta, 0, 0);
	ptaDestroy(&pta);
	pta = ptaGenerateWideLine(x, y + 1 + width / 2,
                                  x, y + h - 2 - width / 2, width);
	ptaJoin(ptad, pta, 0, 0);
	ptaDestroy(&pta);
	pta = ptaGenerateWideLine(x + w - 1, y + 1 + width / 2,
                                  x + w - 1, y + h - 2 - width / 2, width);
	ptaJoin(ptad, pta, 0, 0);
	ptaDestroy(&pta);
    }
    else {   /* even width */
	pta = ptaGenerateWideLine(x - width / 2, y,
                                  x + w - 2 + width / 2, y, width);
	ptaJoin(ptad, pta, 0, 0);
	ptaDestroy(&pta);
	pta = ptaGenerateWideLine(x - width / 2, y + h - 1,
                                  x + w - 2 + width / 2, y + h - 1, width);
	ptaJoin(ptad, pta, 0, 0);
	ptaDestroy(&pta);
	pta = ptaGenerateWideLine(x, y + 0 + width / 2,
                                  x, y + h - 2 - width / 2, width);
	ptaJoin(ptad, pta, 0, 0);
	ptaDestroy(&pta);
	pta = ptaGenerateWideLine(x + w - 1, y + 0 + width / 2,
                                  x + w - 1, y + h - 2 - width / 2, width);
	ptaJoin(ptad, pta, 0, 0);
	ptaDestroy(&pta);
    }

    return ptad;
}


/*!
 *  ptaGenerateBoxa()
 *
 *      Input:  boxa
 *              width
 *              removedups  (1 to remove, 0 to leave)
 *      Return: ptad, or null on error
 *
 *  Notes:
 *      (1) If the boxa has overlapping boxes, and if blending will
 *          be used to give a transparent effect, transparency
 *          artifacts at line intersections can be removed using
 *          removedups = 1.
 */
PTA  *
ptaGenerateBoxa(BOXA    *boxa,
                l_int32  width,
                l_int32  removedups)
{
l_int32  i, n;
BOX     *box;
PTA     *ptad, *ptat, *pta;

    PROCNAME("ptaGenerateBoxa");

    if (!boxa)
	return (PTA *)ERROR_PTR("boxa not defined", procName, NULL);

    n = boxaGetCount(boxa);
    ptat = ptaCreate(0);
    for (i = 0; i < n; i++) {
	box = boxaGetBox(boxa, i, L_CLONE);
	pta = ptaGenerateBox(box, width);
	ptaJoin(ptat, pta, 0, 0);
	ptaDestroy(&pta);
	boxDestroy(&box);
    }

    if (removedups)
        ptad = ptaRemoveDuplicates(ptat, 0);
    else
        ptad = ptaClone(ptat);

    ptaDestroy(&ptat);
    return ptad;
}


/*!
 *  ptaGeneratePolyline()
 *
 *      Input:  pta (vertices of polyline)
 *              width
 *              removedups  (1 to remove, 0 to leave)
 *      Return: ptad, or null on error
 *
 *  Notes:
 *      (1) If the boxa has overlapping boxes, and if blending will
 *          be used to give a transparent effect, transparency
 *          artifacts at line intersections can be removed using
 *          removedups = 1.
 */
PTA  *
ptaGeneratePolyline(PTA     *ptas,
                    l_int32  width,
                    l_int32  removedups)
{
l_int32  i, n, x1, y1, x2, y2;
PTA     *ptad, *ptat, *pta;

    PROCNAME("ptaGeneratePolyline");

    if (!ptas)
	return (PTA *)ERROR_PTR("ptas not defined", procName, NULL);

    n = ptaGetCount(ptas);
    ptat = ptaCreate(0);
    if (n < 2)  /* nothing to do */
        return ptat;

    ptaGetIPt(ptas, 0, &x1, &y1);
    for (i = 1; i < n; i++) {
	ptaGetIPt(ptas, i, &x2, &y2);
        pta = ptaGenerateWideLine(x1, y1, x2, y2, width);
	ptaJoin(ptat, pta, 0, 0);
	ptaDestroy(&pta);
	x1 = x2;
	y1 = y2;
    }

        /* Close the contour */
    ptaGetIPt(ptas, 0, &x2, &y2);
    pta = ptaGenerateWideLine(x1, y1, x2, y2, width);
    ptaJoin(ptat, pta, 0, 0);
    ptaDestroy(&pta);

    if (removedups)
        ptad = ptaRemoveDuplicates(ptat, 0);
    else
        ptad = ptaClone(ptat);

    ptaDestroy(&ptat);
    return ptad;
}


/*------------------------------------------------------------------*
 *        Pta generation for arbitrary shapes built with lines      *
 *------------------------------------------------------------------*/
/*!
 *  pixRenderPta()
 *
 *      Input:  pix
 *              pta (arbitrary set of points)
 *              op   (one of L_SET_PIXELS, L_CLEAR_PIXELS, L_FLIP_PIXELS)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) L_SET_PIXELS puts all image bits in each pixel to 1
 *          (black for 1 bpp; white for depth > 1)
 *      (2) L_CLEAR_PIXELS puts all image bits in each pixel to 0
 *          (white for 1 bpp; black for depth > 1)
 *      (3) L_FLIP_PIXELS reverses all image bits in each pixel
 *      (4) This function clips the rendering to the pix.  It performs
 *          clipping for functions such as pixRenderLine(),
 *          pixRenderBox() and pixRenderBoxa(), that call pixRenderPta().
 */
l_int32
pixRenderPta(PIX     *pix,
             PTA     *pta,
	     l_int32  op)
{
l_int32  i, n, x, y, w, h, d, maxval;

    PROCNAME("pixRenderPta");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (!pta)
	return ERROR_INT("pta not defined", procName, 1);
    if (op != L_SET_PIXELS && op != L_CLEAR_PIXELS && op != L_FLIP_PIXELS)
	return ERROR_INT("invalid op", procName, 1);

    w = pixGetWidth(pix);
    h = pixGetHeight(pix);
    maxval = 1;
    if (op == L_SET_PIXELS) {
        d = pixGetDepth(pix);
        switch (d)
        {
        case 2: 
            maxval = 0x3;
            break;
        case 4: 
            maxval = 0xf;
            break;
        case 8: 
            maxval = 0xff;
            break;
        case 16: 
            maxval = 0xffff;
            break;
        case 32: 
            maxval = 0xffffffff;
            break;
        }
    }

    n = ptaGetCount(pta);
    for (i = 0; i < n; i++) {
	ptaGetIPt(pta, i, &x, &y);
	if (x < 0 || x >= w)
	    continue;
	if (y < 0 || y >= h)
	    continue;
        switch (op)
	{
	case L_SET_PIXELS:
	    pixSetPixel(pix, x, y, maxval);
	    break;
	case L_CLEAR_PIXELS:
	    pixClearPixel(pix, x, y);
	    break;
	case L_FLIP_PIXELS:
	    pixFlipPixel(pix, x, y);
	    break;
        default:
	    break;
	}
    }

    return 0;
}


/*!
 *  pixRenderPtaArb()
 *
 *      Input:  pix
 *              pta (arbitrary set of points)
 *              rval, gval, bval
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) If pix is colormapped, render this color on each pixel.
 *      (2) If pix is not colormapped, do the best job you can using
 *          the input colors:
 *          - d = 1: set the pixels
 *          - d = 2, 4, 8: average the input rgb value
 *          - d = 32: use the input rgb value
 *      (3) This function clips the rendering to the pix.
 */
l_int32
pixRenderPtaArb(PIX     *pix,
                PTA     *pta,
	        l_uint8  rval,
                l_uint8  gval,
                l_uint8  bval)
{
l_int32   i, n, x, y, w, h, d, index;
l_uint8   val;
l_uint32  val32;
PIXCMAP  *cmap;

    PROCNAME("pixRenderPtaArb");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (!pta)
	return ERROR_INT("pta not defined", procName, 1);
    d = pixGetDepth(pix);
    if (d != 1 && d != 2 && d != 4 && d != 8 && d != 32) 
	return ERROR_INT("depth not in {1,2,4,8,32}", procName, 1);

    if (d == 1) {
        pixRenderPta(pix, pta, L_SET_PIXELS);
        return 0;
    }

    cmap = pixGetColormap(pix);
    d = pixGetDepth(pix);
    if (cmap) {
        if (pixcmapAddNewColor(cmap, rval, gval, bval, &index))
            return ERROR_INT("colormap is full", procName, 1);
    }
    else {
        if (d == 2)
            val = (rval + gval + bval) / (3 * 64);
        else if (d == 4)
            val = (rval + gval + bval) / (3 * 16);
        else if (d == 8)
            val = (rval + gval + bval) / 3;
        else  /* d == 32 */
            composeRGBPixel(rval, gval, bval, &val32);
    }

    w = pixGetWidth(pix);
    h = pixGetHeight(pix);
    n = ptaGetCount(pta);
    for (i = 0; i < n; i++) {
	ptaGetIPt(pta, i, &x, &y);
	if (x < 0 || x >= w)
	    continue;
	if (y < 0 || y >= h)
	    continue;
        if (cmap)
	    pixSetPixel(pix, x, y, index);
        else if (d == 32)
	    pixSetPixel(pix, x, y, val32);
        else
	    pixSetPixel(pix, x, y, val);
    }

    return 0;
}


/*!
 *  pixRenderPtaBlend()
 *
 *      Input:  pix (32 bpp rgb)
 *              pta  (arbitrary set of points)
 *              rval, gval, bval
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) This function clips the rendering to the pix.
 */
l_int32
pixRenderPtaBlend(PIX     *pix,
                  PTA     *pta,
	          l_uint8  rval,
                  l_uint8  gval,
                  l_uint8  bval,
		  l_float32 fract)
{
l_int32    i, n, x, y, w, h;
l_uint8    nrval, ngval, nbval;
l_uint32   val32;
l_float32  frval, fgval, fbval;

    PROCNAME("pixRenderPtaBlend");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (!pta)
	return ERROR_INT("pta not defined", procName, 1);
    if (pixGetDepth(pix) != 32)
	return ERROR_INT("depth not 32 bpp", procName, 1);
    if (fract < 0.0 || fract > 1.0) {
        L_WARNING("fract must be in [0.0, 1.0]; setting to 0.5", procName);
        fract = 0.5;
    }

    w = pixGetWidth(pix);
    h = pixGetHeight(pix);
    n = ptaGetCount(pta);
    frval = fract * rval;
    fgval = fract * gval;
    fbval = fract * bval;
    for (i = 0; i < n; i++) {
	ptaGetIPt(pta, i, &x, &y);
	if (x < 0 || x >= w)
	    continue;
	if (y < 0 || y >= h)
	    continue;
        pixGetPixel(pix, x, y, &val32);
	nrval = GET_DATA_BYTE(&val32, COLOR_RED);
	nrval = (l_uint8)((1. - fract) * nrval + frval);
	ngval = GET_DATA_BYTE(&val32, COLOR_GREEN);
	ngval = (l_uint8)((1. - fract) * ngval + fgval);
	nbval = GET_DATA_BYTE(&val32, COLOR_BLUE);
	nbval = (l_uint8)((1. - fract) * nbval + fbval);
	composeRGBPixel(nrval, ngval, nbval, &val32);
	pixSetPixel(pix, x, y, val32);
    }

    return 0;
}


/*------------------------------------------------------------------*
 *           Rendering of arbitrary shapes built with lines         *
 *------------------------------------------------------------------*/
/*!
 *  pixRenderLine()
 *
 *      Input:  pix
 *              x1, y1
 *              x2, y2
 *              width  (thickness of line)
 *              op  (one of L_SET_PIXELS, L_CLEAR_PIXELS, L_FLIP_PIXELS)
 *      Return: 0 if OK, 1 on error
 */
l_int32
pixRenderLine(PIX     *pix,
              l_int32  x1,
              l_int32  y1,
              l_int32  x2,
              l_int32  y2,
	      l_int32  width,
	      l_int32  op)
{
PTA  *pta;

    PROCNAME("pixRenderLine");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (width < 1) {
	L_WARNING("width must be > 0; setting to 1", procName);
	width = 1;
    }
    if (op != L_SET_PIXELS && op != L_CLEAR_PIXELS && op != L_FLIP_PIXELS)
	return ERROR_INT("invalid op", procName, 1);

    if ((pta = ptaGenerateWideLine(x1, y1, x2, y2, width)) == NULL)
	return ERROR_INT("pta not made", procName, 1);
    pixRenderPta(pix, pta, op);
    ptaDestroy(&pta);
    return 0;
}


/*!
 *  pixRenderLineArb()
 *
 *      Input:  pix
 *              x1, y1
 *              x2, y2
 *              width  (thickness of line)
 *              rval, gval, bval
 *      Return: 0 if OK, 1 on error
 */
l_int32
pixRenderLineArb(PIX     *pix,
                 l_int32  x1,
                 l_int32  y1,
                 l_int32  x2,
                 l_int32  y2,
	         l_int32  width,
	         l_uint8  rval,
	         l_uint8  gval,
	         l_uint8  bval)
{
PTA  *pta;

    PROCNAME("pixRenderLineArb");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (width < 1) {
	L_WARNING("width must be > 0; setting to 1", procName);
	width = 1;
    }

    if ((pta = ptaGenerateWideLine(x1, y1, x2, y2, width)) == NULL)
	return ERROR_INT("pta not made", procName, 1);
    pixRenderPtaArb(pix, pta, rval, gval, bval);
    ptaDestroy(&pta);
    return 0;
}


/*!
 *  pixRenderLineBlend()
 *
 *      Input:  pix
 *              x1, y1
 *              x2, y2
 *              width  (thickness of line)
 *              rval, gval, bval
 *              fract
 *      Return: 0 if OK, 1 on error
 */
l_int32
pixRenderLineBlend(PIX       *pix,
                   l_int32    x1,
                   l_int32    y1,
                   l_int32    x2,
                   l_int32    y2,
                   l_int32    width,
                   l_uint8    rval,
                   l_uint8    gval,
                   l_uint8    bval,
        	   l_float32  fract)
{
PTA  *pta;

    PROCNAME("pixRenderLineBlend");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (width < 1) {
	L_WARNING("width must be > 0; setting to 1", procName);
	width = 1;
    }

    if ((pta = ptaGenerateWideLine(x1, y1, x2, y2, width)) == NULL)
	return ERROR_INT("pta not made", procName, 1);
    pixRenderPtaBlend(pix, pta, rval, gval, bval, fract);
    ptaDestroy(&pta);
    return 0;
}


/*!
 *  pixRenderBox()
 *
 *      Input:  pix
 *              box
 *              width  (thickness of box lines)
 *              op  (one of L_SET_PIXELS, L_CLEAR_PIXELS, L_FLIP_PIXELS)
 *      Return: 0 if OK, 1 on error
 */
l_int32
pixRenderBox(PIX     *pix,
             BOX     *box,
	     l_int32  width,
	     l_int32  op)
{
PTA  *pta;

    PROCNAME("pixRenderBox");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (!box)
	return ERROR_INT("box not defined", procName, 1);
    if (op != L_SET_PIXELS && op != L_CLEAR_PIXELS && op != L_FLIP_PIXELS)
	return ERROR_INT("invalid op", procName, 1);

    if ((pta = ptaGenerateBox(box, width)) == NULL)
	return ERROR_INT("pta not made", procName, 1);
    pixRenderPta(pix, pta, op);
    ptaDestroy(&pta);
    return 0;
}


/*!
 *  pixRenderBoxArb()
 *
 *      Input:  pix
 *              box
 *              width  (thickness of box lines)
 *              rval, gval, bval
 *      Return: 0 if OK, 1 on error
 */
l_int32
pixRenderBoxArb(PIX     *pix,
                BOX     *box,
	        l_int32  width,
	        l_uint8  rval,
                l_uint8  gval,
                l_uint8  bval)
{
PTA  *pta;

    PROCNAME("pixRenderBoxArb");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (!box)
	return ERROR_INT("box not defined", procName, 1);

    if ((pta = ptaGenerateBox(box, width)) == NULL)
	return ERROR_INT("pta not made", procName, 1);
    pixRenderPtaArb(pix, pta, rval, gval, bval);
    ptaDestroy(&pta);
    return 0;
}


/*!
 *  pixRenderBoxBlend()
 *
 *      Input:  pix
 *              box
 *              width  (thickness of box lines)
 *              rval, gval, bval
 *              fract (in [0.0 - 1.0]; complete transparency (no effect)
 *                     if 0.0; no transparency if 1.0)
 *      Return: 0 if OK, 1 on error
 */
l_int32
pixRenderBoxBlend(PIX       *pix,
                  BOX       *box,
	          l_int32    width,
	          l_uint8    rval,
                  l_uint8    gval,
                  l_uint8    bval,
		  l_float32  fract)
{
PTA  *pta;

    PROCNAME("pixRenderBoxBlend");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (!box)
	return ERROR_INT("box not defined", procName, 1);

    if ((pta = ptaGenerateBox(box, width)) == NULL)
	return ERROR_INT("pta not made", procName, 1);
    pixRenderPtaBlend(pix, pta, rval, gval, bval, fract);
    ptaDestroy(&pta);
    return 0;
}


/*!
 *  pixRenderBoxa()
 *
 *      Input:  pix
 *              boxa
 *              width  (thickness of line)
 *              op  (one of L_SET_PIXELS, L_CLEAR_PIXELS, L_FLIP_PIXELS)
 *      Return: 0 if OK, 1 on error
 */
l_int32
pixRenderBoxa(PIX     *pix,
              BOXA    *boxa,
              l_int32  width,
              l_int32  op)
{
PTA  *pta;

    PROCNAME("pixRenderBoxa");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (!boxa)
	return ERROR_INT("boxa not defined", procName, 1);
    if (op != L_SET_PIXELS && op != L_CLEAR_PIXELS && op != L_FLIP_PIXELS)
	return ERROR_INT("invalid op", procName, 1);

    if ((pta = ptaGenerateBoxa(boxa, width, 0)) == NULL)
	return ERROR_INT("pta not made", procName, 1);
    pixRenderPta(pix, pta, op);
    ptaDestroy(&pta);
    return 0;
}


/*!
 *  pixRenderBoxaArb()
 *
 *      Input:  pix
 *              boxa
 *              width  (thickness of line)
 *              rval, gval, bval
 *      Return: 0 if OK, 1 on error
 */
l_int32
pixRenderBoxaArb(PIX     *pix,
                 BOXA    *boxa,
                 l_int32  width,
                 l_uint8  rval,
                 l_uint8  gval,
                 l_uint8  bval)
{
PTA  *pta;

    PROCNAME("pixRenderBoxaArb");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (!boxa)
	return ERROR_INT("boxa not defined", procName, 1);

    if ((pta = ptaGenerateBoxa(boxa, width, 0)) == NULL)
	return ERROR_INT("pta not made", procName, 1);
    pixRenderPtaArb(pix, pta, rval, gval, bval);
    ptaDestroy(&pta);
    return 0;
}


/*!
 *  pixRenderBoxaBlend()
 *
 *      Input:  pix
 *              boxa
 *              width  (thickness of line)
 *              rval, gval, bval
 *              fract (in [0.0 - 1.0]; complete transparency (no effect)
 *                     if 0.0; no transparency if 1.0)
 *              removedups  (1 to remove; 0 otherwise)
 *      Return: 0 if OK, 1 on error
 */
l_int32
pixRenderBoxaBlend(PIX       *pix,
                   BOXA      *boxa,
	           l_int32    width,
	           l_uint8    rval,
                   l_uint8    gval,
                   l_uint8    bval,
                   l_float32  fract,
                   l_int32    removedups)
{
PTA  *pta;

    PROCNAME("pixRenderBoxaBlend");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (!boxa)
	return ERROR_INT("boxa not defined", procName, 1);

    if ((pta = ptaGenerateBoxa(boxa, width, removedups)) == NULL)
	return ERROR_INT("pta not made", procName, 1);
    pixRenderPtaBlend(pix, pta, rval, gval, bval, fract);
    ptaDestroy(&pta);
    return 0;
}


/*!
 *  pixRenderPolyline()
 *
 *      Input:  pix
 *              ptas
 *              width  (thickness of line)
 *              op  (one of L_SET_PIXELS, L_CLEAR_PIXELS, L_FLIP_PIXELS)
 *      Return: 0 if OK, 1 on error
 *
 *  Note: this renders a closed contour.
 */
l_int32
pixRenderPolyline(PIX     *pix,
                  PTA     *ptas,
                  l_int32  width,
                  l_int32  op)
{
PTA  *pta;

    PROCNAME("pixRenderPolyline");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (!ptas)
	return ERROR_INT("ptas not defined", procName, 1);
    if (op != L_SET_PIXELS && op != L_CLEAR_PIXELS && op != L_FLIP_PIXELS)
	return ERROR_INT("invalid op", procName, 1);

    if ((pta = ptaGeneratePolyline(ptas, width, 0)) == NULL)
	return ERROR_INT("pta not made", procName, 1);
    pixRenderPta(pix, pta, op);
    ptaDestroy(&pta);
    return 0;
}


/*!
 *  pixRenderPolylineArb()
 *
 *      Input:  pix
 *              ptas
 *              width  (thickness of line)
 *              rval, gval, bval
 *      Return: 0 if OK, 1 on error
 *
 *  Note: this renders a closed contour.
 */
l_int32
pixRenderPolylineArb(PIX     *pix,
                     PTA     *ptas,
                     l_int32  width,
                     l_uint8  rval,
                     l_uint8  gval,
                     l_uint8  bval)
{
PTA  *pta;

    PROCNAME("pixRenderPolylineArb");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (!ptas)
	return ERROR_INT("ptas not defined", procName, 1);

    if ((pta = ptaGeneratePolyline(ptas, width, 0)) == NULL)
	return ERROR_INT("pta not made", procName, 1);
    pixRenderPtaArb(pix, pta, rval, gval, bval);
    ptaDestroy(&pta);
    return 0;
}


/*!
 *  pixRenderPolylineBlend()
 *
 *      Input:  pix
 *              ptas
 *              width  (thickness of line)
 *              rval, gval, bval
 *              fract (in [0.0 - 1.0]; complete transparency (no effect)
 *                     if 0.0; no transparency if 1.0)
 *              removedups  (1 to remove; 0 otherwise)
 *      Return: 0 if OK, 1 on error
 */
l_int32
pixRenderPolylineBlend(PIX       *pix,
                       PTA       *ptas,
                       l_int32    width,
                       l_uint8    rval,
                       l_uint8    gval,
                       l_uint8    bval,
                       l_float32  fract,
                       l_int32    removedups)
{
PTA  *pta;

    PROCNAME("pixRenderPolylineBlend");

    if (!pix)
	return ERROR_INT("pix not defined", procName, 1);
    if (!ptas)
	return ERROR_INT("ptas not defined", procName, 1);

    if ((pta = ptaGeneratePolyline(ptas, width, removedups)) == NULL)
	return ERROR_INT("pta not made", procName, 1);
    pixRenderPtaBlend(pix, pta, rval, gval, bval, fract);
    ptaDestroy(&pta);
    return 0;
}


        
/*------------------------------------------------------------------*
 *             Contour rendering on grayscale images                *
 *------------------------------------------------------------------*/
/*!
 *  pixRenderContours()
 *
 *      Input:  pixs (8 or 16 bpp)
 *              startval (value of lowest contour; must be in [0 ... maxval])
 *              incr  (increment to next contour; must be > 0)
 *              outdepth (either 1 or depth of pixs)
 *      Return: pixd, or null on error
 *
 *  The output can be either 1 bpp, showing just the contour
 *  lines, or a copy of the input pixs with the contour lines
 *  superposed.
 */
PIX *
pixRenderContours(PIX     *pixs,
		  l_int32  startval,
                  l_int32  incr,
		  l_int32  outdepth)
{
l_int32    w, h, d, maxval, wpls, wpld, i, j, val, test;
l_uint32  *datas, *datad, *lines, *lined;
PIX       *pixd;

    PROCNAME("pixRenderContours");

    if (!pixs)
	return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    if (pixGetColormap(pixs))
	return (PIX *)ERROR_PTR("pixs has colormap", procName, NULL);
    d = pixGetDepth(pixs);
    if (d != 8 && d != 16)
	return (PIX *)ERROR_PTR("pixs not 8 or 16 bpp", procName, NULL);
    if (outdepth != 1 && outdepth != d) {
        L_WARNING("invalid outdepth; setting to 1", procName);
	outdepth = 1;
    }
    maxval = (1 << d) - 1;
    if (startval < 0 || startval > maxval)
	return (PIX *)ERROR_PTR("startval not in [0 ... maxval]",
	       procName, NULL);
    if (incr < 1)
	return (PIX *)ERROR_PTR("incr < 1", procName, NULL);

    w = pixGetWidth(pixs);
    h = pixGetHeight(pixs);
    if (outdepth == d)
	pixd = pixCopy(NULL, pixs);
    else
        pixd = pixCreate(w, h, 1);

    pixCopyResolution(pixd, pixs);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);
    datas = pixGetData(pixs);
    wpls = pixGetWpl(pixs);

    switch (d)
    {
    case 8:
        if (outdepth == 1) {
	    for (i = 0; i < h; i++) {
		lines = datas + i * wpls;
		lined = datad + i * wpld;
		for (j = 0; j < w; j++) {
		    val = GET_DATA_BYTE(lines, j);
		    if (val < startval)
		        continue;
		    test = (val - startval) % incr;
		    if (!test)
			SET_DATA_BIT(lined, j); 
		}
	    }
        }
	else {  /* outdepth == d */
	    for (i = 0; i < h; i++) {
		lines = datas + i * wpls;
		lined = datad + i * wpld;
		for (j = 0; j < w; j++) {
		    val = GET_DATA_BYTE(lines, j);
		    if (val < startval)
		        continue;
		    test = (val - startval) % incr;
		    if (!test)
			SET_DATA_BYTE(lined, j, 0); 
		}
	    }
        }
	break;

    case 16:
        if (outdepth == 1) {
	    for (i = 0; i < h; i++) {
		lines = datas + i * wpls;
		lined = datad + i * wpld;
		for (j = 0; j < w; j++) {
		    val = GET_DATA_TWO_BYTES(lines, j);
		    if (val < startval)
		        continue;
		    test = (val - startval) % incr;
		    if (!test)
			SET_DATA_BIT(lined, j); 
		}
	    }
        }
	else {  /* outdepth == d */
	    for (i = 0; i < h; i++) {
		lines = datas + i * wpls;
		lined = datad + i * wpld;
		for (j = 0; j < w; j++) {
		    val = GET_DATA_TWO_BYTES(lines, j);
		    if (val < startval)
		        continue;
		    test = (val - startval) % incr;
		    if (!test)
			SET_DATA_TWO_BYTES(lined, j, 0); 
		}
	    }
        }
	break;

    default:
	return (PIX *)ERROR_PTR("pixs not 8 or 16 bpp", procName, NULL);
    }

    return pixd;
}

