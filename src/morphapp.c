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
 *  morphapp.c
 *
 *      These are some useful and/or interesting composite
 *      image processing operations, of the type that are often
 *      useful in applications.  Most are morphological in
 *      nature.
 *
 *      Selective morph sequence operation on each component          *
 *            PIX     *pixMorphSequenceByComponent()
 *            PIXA    *pixaMorphSequenceByComponent()
 *
 *      Selective connected component filling
 *            PIX     *pixSelectiveConnCompFill()
 *
 *      Removal of matched patterns
 *            PIX     *pixRemoveMatchedPattern()
 *
 *      Display of matched patterns
 *            PIX     *pixDisplayMatchedPattern()
 *
 *      Iterative morphological seed filling (don't use for real work)
 *            PIX     *pixSeedfillMorph()
 *      
 *      Granulometry on binary images
 *            NUMA    *pixRunHistogramMorph()
 *
 *      Composite operations on grayscale images
 *            PIX     *pixTophat()
 *            PIX     *pixHDome()
 *            PIX     *pixFastTophat()
 *            PIX     *pixMorphGradient()
 *
 *      Centroids of PIXA
 *            PTA     *pixaCentroids()
 */

#include <stdio.h>
#include "allheaders.h"

#define   SWAP(x, y)   {temp = (x); (x) = (y); (y) = temp;}


/*-----------------------------------------------------------------*
 *             Morph sequence operation on each component          *
 *-----------------------------------------------------------------*/
/*!
 *  pixMorphSequenceByComponent()
 *
 *      Input:  pixs
 *              sequence (string specifying sequence)
 *              connectivity (4 or 8)
 *              minw  (minimum width to consider; use 0 or 1 for any width)
 *              minh  (minimum height to consider; use 0 or 1 for any height)
 *              &boxa (<optional> return boxa of c.c. in pixs)
 *      Return: pixd, or null on error
 *
 *  Notes:
 *      (1) See pixMorphSequence() for composing operation sequences.
 *      (2) This operates separately on each c.c. in the input pix.
 *      (3) The dilation does NOT increase the c.c. size; it is clipped
 *          to the size of the original c.c.   This is necessary to
 *          keep the c.c. independent after the operation.
 *      (4) You can specify that the width and/or height must equal
 *          or exceed a minimum size for the operation to take place.
 *      (5) Use NULL for boxa to avoid returning the boxa.
 */
PIX *
pixMorphSequenceByComponent(PIX         *pixs,
                            const char  *sequence,
                            l_int32      connectivity,
                            l_int32      minw,
                            l_int32      minh,
			    BOXA       **pboxa)
{
l_int32  n, i, x, y, w, h;
BOXA    *boxa;
PIX     *pix, *pixd;
PIXA    *pixas, *pixad;

    PROCNAME("pixMorphSequenceByComponent");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    if (!sequence)
        return (PIX *)ERROR_PTR("sequence not defined", procName, NULL);

    if (minw <= 0) minw = 1;
    if (minh <= 0) minh = 1;

        /* Get the c.c. */
    if ((boxa = pixConnComp(pixs, &pixas, connectivity)) == NULL)
        return (PIX *)ERROR_PTR("boxa not made", procName, NULL);

        /* Operate on each c.c. independently */
    pixad = pixaMorphSequenceByComponent(pixas, sequence, minw, minh);
    pixaDestroy(&pixas);
    boxaDestroy(&boxa);
    if (!pixad)
        return (PIX *)ERROR_PTR("pixad not made", procName, NULL);

        /* Display the result out into pixd */
    pixd = pixCreateTemplate(pixs);
    n = pixaGetCount(pixad);
    for (i = 0; i < n; i++) {
        pixaGetBoxGeometry(pixad, i, &x, &y, &w, &h);
	pix = pixaGetPix(pixad, i, L_CLONE);
        pixRasterop(pixd, x, y, w, h, PIX_PAINT, pix, 0, 0);
        pixDestroy(&pix);
    }

    if (pboxa)
        *pboxa = pixaGetBoxa(pixad, L_CLONE);
    pixaDestroy(&pixad);
    return pixd;
}


/*!
 *  pixaMorphSequenceByComponent()
 *
 *      Input:  pixas (of 1 bpp pix)
 *              sequence (string specifying sequence)
 *              minw  (minimum width to consider; use 0 or 1 for any width)
 *              minh  (minimum height to consider; use 0 or 1 for any height)
 *      Return: pixad, or null on error
 *
 *  Notes:
 *      (1) See pixMorphSequence() for composing operation sequences.
 *      (2) This operates separately on each c.c. in the input pixa.
 *      (3) You can specify that the width and/or height must equal
 *          or exceed a minimum size for the operation to take place.
 *      (4) The input pixa should have a boxa giving the locations
 *          of the pix components.
 */
PIXA *
pixaMorphSequenceByComponent(PIXA        *pixas,
                             const char  *sequence,
                             l_int32      minw,
                             l_int32      minh)
{
l_int32  n, i, w, h, d;
BOX     *box;
PIX     *pixt1, *pixt2;
PIXA    *pixad;

    PROCNAME("pixaMorphSequenceByComponent");

    if (!pixas)
        return (PIXA *)ERROR_PTR("pixas not defined", procName, NULL);
    if ((n = pixaGetCount(pixas)) == 0)
        return (PIXA *)ERROR_PTR("no pix in pixas", procName, NULL);
    if (n != pixaGetBoxaCount(pixas))
        L_WARNING("boxa size != n", procName);
    pixaGetPixDimensions(pixas, 0, NULL, NULL, &d);
    if (d != 1)
        return (PIXA *)ERROR_PTR("depth not 1 bpp", procName, NULL);

    if (!sequence)
        return (PIXA *)ERROR_PTR("sequence not defined", procName, NULL);
    if (minw <= 0) minw = 1;
    if (minh <= 0) minh = 1;

    if ((pixad = pixaCreate(n)) == NULL)
        return (PIXA *)ERROR_PTR("pixad not made", procName, NULL);
    for (i = 0; i < n; i++) {
        pixaGetPixDimensions(pixas, i, &w, &h, NULL);
        if (w >= minw && h >= minh) {
            if ((pixt1 = pixaGetPix(pixas, i, L_CLONE)) == NULL)
                return (PIXA *)ERROR_PTR("pixt1 not found", procName, NULL);
            if ((pixt2 = pixMorphSequence(pixt1, sequence, 0)) == NULL)
                return (PIXA *)ERROR_PTR("pixt2 not made", procName, NULL);
	    pixaAddPix(pixad, pixt2, L_INSERT);
	    box = pixaGetBox(pixas, i, L_COPY);
	    pixaAddBox(pixad, box, L_INSERT);
            pixDestroy(&pixt1);
        }
    }

    return pixad;
}


/*-----------------------------------------------------------------*
 *             Selective connected component filling               *
 *-----------------------------------------------------------------*/
/*!
 *  pixSelectiveConnCompFill()
 *
 *      Input:  pixs (binary)
 *              connectivity (4 or 8)
 *              minw  (minimum width to consider; use 0 or 1 for any width)
 *              minh  (minimum height to consider; use 0 or 1 for any height)
 *      Return: pix (with holes filled in selected c.c.), or null on error
 */
PIX *
pixSelectiveConnCompFill(PIX     *pixs,
                         l_int32  connectivity,
                         l_int32  minw,
                         l_int32  minh)
{
l_int32  n, i, x, y, w, h;
BOXA    *boxa;
PIX     *pixt1, *pixt2, *pixd;
PIXA    *pixa;

    PROCNAME("pixSelectiveConnCompFill");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    if (pixGetDepth(pixs) != 1)
        return (PIX *)ERROR_PTR("pixs not 1 bpp", procName, NULL);

    if (minw <= 0) minw = 1;
    if (minh <= 0) minh = 1;

    if ((pixd = pixCopy(NULL, pixs)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);

    if ((boxa = pixConnComp(pixs, &pixa, connectivity)) == NULL)
        return (PIX *)ERROR_PTR("boxa not made", procName, NULL);
    n = boxaGetCount(boxa);
    for (i = 0; i < n; i++) {
        boxaGetBoxGeometry(boxa, i, &x, &y, &w, &h);
        if (w >= minw && h >= minh) {
            if ((pixt1 = pixaGetPix(pixa, i, L_CLONE)) == NULL)
                return (PIX *)ERROR_PTR("pixt1 not found", procName, NULL);
            if ((pixt2 = pixHolesByFilling(pixt1, 12 - connectivity)) == NULL)
                return (PIX *)ERROR_PTR("pixt2 not made", procName, NULL);
            pixRasterop(pixd, x, y, w, h, PIX_PAINT, pixt2, 0, 0);
            pixDestroy(&pixt1);
            pixDestroy(&pixt2);
        }
    }
    pixaDestroy(&pixa);
    boxaDestroy(&boxa);

    return pixd;
}


/*-----------------------------------------------------------------*
 *                    Removal of matched patterns                  *
 *-----------------------------------------------------------------*/
/*!
 *  pixRemoveMatchedPattern()
 *
 *      Input:  pixs (input image, 1 bpp)
 *              pixp (pattern to be removed from image, 1 bpp)
 *              pixe (image after erosion by Sel that approximates pixp, 1 bpp)
 *              x0, y0 (center of Sel)
 *              dsize (number of pixels on each side by which pixp is
 *                     dilated before being subtracted from pixs;
 *                     valid values are {0, 1, 2, 3, 4})
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *    (1) This is in-place.
 *    (2) You can use various functions in selgen to create a Sel
 *        that is used to generate pixe from pixs.
 *    (3) This function is applied after pixe has been computed.
 *        It finds the centroid of each c.c., and subtracts
 *        (the appropriately dilated version of) pixp, with the center
 *        of the Sel used to align pixp with pixs.
 */
l_int32
pixRemoveMatchedPattern(PIX     *pixs,
                        PIX     *pixp,
                        PIX     *pixe,
                        l_int32  x0,
                        l_int32  y0,
                        l_int32  dsize)
{
l_int32  i, nc, x, y, w, h, xb, yb;
BOXA    *boxa;
PIX     *pixt1, *pixt2;
PIXA    *pixa;
PTA     *pta;
SEL     *sel;

    PROCNAME("pixRemoveMatchedPattern");

    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    if (!pixp)
        return ERROR_INT("pixp not defined", procName, 1);
    if (!pixe)
        return ERROR_INT("pixe not defined", procName, 1);
    if (pixGetDepth(pixs) != 1 || pixGetDepth(pixp) != 1 ||
        pixGetDepth(pixe) != 1)
        return ERROR_INT("all input pix not 1 bpp", procName, 1);
    if (dsize < 0 || dsize > 4)
        return ERROR_INT("dsize not in {0,1,2,3,4}", procName, 1);

        /* Find the connected components and their centroids */
    boxa = pixConnComp(pixe, &pixa, 8);
    if ((nc = boxaGetCount(boxa)) == 0) {
        L_WARNING("no matched patterns", procName);
        boxaDestroy(&boxa);
        pixaDestroy(&pixa);
        return 0;
    }
    pta = pixaCentroids(pixa);

        /* Optionally dilate the pattern, first adding a border that
         * is large enough to accommodate the dilated pixels */
    sel = NULL;
    if (dsize > 0) {
        sel = selCreateBrick(2 * dsize + 1, 2 * dsize + 1, dsize, dsize,
                             SEL_HIT);
        pixt1 = pixAddBorder(pixp, dsize, 0);
        pixt2 = pixDilate(NULL, pixt1, sel);
        selDestroy(&sel);
        pixDestroy(&pixt1);
    }
    else
        pixt2 = pixClone(pixp);

        /* Subtract out each dilated pattern.  The centroid of each
         * component is located at:
         *       (box->x + x, box->y + y)
         * and the 'center' of the pattern used in making pixe is located at
         *       (x0 + dsize, (y0 + dsize)
         * relative to the UL corner of the pattern.  The center of the
         * pattern is placed at the center of the component. */
    w = pixGetWidth(pixt2);
    h = pixGetHeight(pixt2);
    for (i = 0; i < nc; i++) {
        ptaGetIPt(pta, i, &x, &y);
        boxaGetBoxGeometry(boxa, i, &xb, &yb, NULL, NULL);
        pixRasterop(pixs, xb + x - x0 - dsize, yb + y - y0 - dsize,
                    w, h, PIX_DST & PIX_NOT(PIX_SRC), pixt2, 0, 0);
    }

    boxaDestroy(&boxa);
    pixaDestroy(&pixa);
    ptaDestroy(&pta);
    pixDestroy(&pixt2);
    return 0;
}


/*-----------------------------------------------------------------*
 *                    Display of matched patterns                  *
 *-----------------------------------------------------------------*/
/*!
 *  pixDisplayMatchedPattern()
 *
 *      Input:  pixs (input image, 1 bpp)
 *              pixp (pattern to be removed from image, 1 bpp)
 *              pixe (image after erosion by Sel that approximates pixp, 1 bpp)
 *              x0, y0 (center of Sel)
 *              color (to paint the matched patterns; 0xrrggbb00)
 *              scale (reduction factor for output pixd)
 *              nlevels (if scale < 1.0, threshold to this number of levels)
 *      Return: pixd (8 bpp, colormapped), or null on error
 *
 *  Notes:
 *    (1) A 4 bpp colormapped image is generated.
 *    (2) If scale <= 1.0, do scale to gray for the output, and threshold
 *        to nlevels of gray.
 *    (3) You can use various functions in selgen to create a Sel
 *        that will generate pixe from pixs.
 *    (4) This function is applied after pixe has been computed.
 *        It finds the centroid of each c.c., and colors the output
 *        pixels using pixp (appropriately aligned) as a stencil.
 *        Alignment is done using the origin of the Sel and the
 *        centroid of the eroded image to place the stencil pixp.
 */
PIX *
pixDisplayMatchedPattern(PIX       *pixs,
                         PIX       *pixp,
                         PIX       *pixe,
                         l_int32    x0,
                         l_int32    y0,
                         l_uint32   color,
                         l_float32  scale,
                         l_int32    nlevels)
{
l_int32   i, nc, xb, yb, x, y, xi, yi, rval, gval, bval;
BOXA     *boxa;
PIX      *pixd, *pixt, *pixps;
PIXA     *pixa;
PTA      *pta;
PIXCMAP  *cmap;

    PROCNAME("pixDisplayMatchedPattern");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    if (!pixp)
        return (PIX *)ERROR_PTR("pixp not defined", procName, NULL);
    if (!pixe)
        return (PIX *)ERROR_PTR("pixe not defined", procName, NULL);
    if (pixGetDepth(pixs) != 1 || pixGetDepth(pixp) != 1 ||
        pixGetDepth(pixe) != 1)
        return (PIX *)ERROR_PTR("all input pix not 1 bpp", procName, NULL);
    if (scale > 1.0 || scale <= 0.0) {
        L_WARNING("scale > 1.0 or < 0.0; setting to 1.0", procName);
        scale = 1.0;
    }

        /* Find the connected components and their centroids */
    boxa = pixConnComp(pixe, &pixa, 8);
    if ((nc = boxaGetCount(boxa)) == 0) {
        L_WARNING("no matched patterns", procName);
        boxaDestroy(&boxa);
        pixaDestroy(&pixa);
        return 0;
    }
    pta = pixaCentroids(pixa);

    rval = GET_DATA_BYTE(&color, COLOR_RED);
    gval = GET_DATA_BYTE(&color, COLOR_GREEN);
    bval = GET_DATA_BYTE(&color, COLOR_BLUE);
    if (scale == 1.0) {  /* output 4 bpp at full resolution */
        pixd = pixConvert1To4(NULL, pixs, 0, 1);
        cmap = pixcmapCreate(4);
        pixcmapAddColor(cmap, 255, 255, 255);
        pixcmapAddColor(cmap, 0, 0, 0);
        pixSetColormap(pixd, cmap);

        /* Paint through pixp for each match location.  The centroid of each
         * component in pixe is located at:
         *       (box->x + x, box->y + y)
         * and the 'center' of the pattern used in making pixe is located at
         *       (x0, y0)
         * relative to the UL corner of the pattern.  The center of the
         * pattern is placed at the center of the component. */
        for (i = 0; i < nc; i++) {
            ptaGetIPt(pta, i, &x, &y);
            boxaGetBoxGeometry(boxa, i, &xb, &yb, NULL, NULL);
            pixSetMaskedCmap(pixd, pixp, xb + x - x0, yb + y - y0,
                             rval, gval, bval);
        }
    }
    else {  /* output 4 bpp downscaled */
        pixt = pixScaleToGray(pixs, scale);
        pixd = pixThresholdTo4bpp(pixt, nlevels, 1);
        pixps = pixScaleBySampling(pixp, scale, scale);

        for (i = 0; i < nc; i++) {
            ptaGetIPt(pta, i, &x, &y);
            boxaGetBoxGeometry(boxa, i, &xb, &yb, NULL, NULL);
            xi = (l_int32)(scale * (xb + x - x0));
            yi = (l_int32)(scale * (yb + y - y0));
            pixSetMaskedCmap(pixd, pixps, xi, yi, rval, gval, bval);
        }
        pixDestroy(&pixt);
        pixDestroy(&pixps);
    }

    boxaDestroy(&boxa);
    pixaDestroy(&pixa);
    ptaDestroy(&pta);
    return pixd;
}


/*-----------------------------------------------------------------*
 *             Iterative morphological seed filling                *
 *-----------------------------------------------------------------*/
/*!
 *  pixSeedfillMorph()
 *
 *      Input:  pixs (seed)
 *              pixm (mask)
 *              connectivity (4 or 8)
 *      Return: pix where seed has been grown to completion
 *              into the mask, or null on error
 *
 *  Notes:
 *    (1) This is in general a very inefficient method for filling
 *        from a seed into a mask.  I've included it here for
 *        pedagogical reasons, but it should NEVER be used if
 *        efficiency is any consideration -- use pixSeedfillBinary()!
 *    (2) We use a 3x3 brick SEL for 8-cc filling and a 3x3 plus SEL for 4-cc.
 */
PIX *
pixSeedfillMorph(PIX     *pixs,
                 PIX     *pixm,
                 l_int32  connectivity)
{
l_int32  same, iter;
PIX     *pixt1, *pixd, *temp;
SEL     *sel_3;

    PROCNAME("pixSeedfillMorph");

    if (!pixs)
        return (PIX *)ERROR_PTR("seed pix not defined", procName, NULL);
    if (!pixm)
        return (PIX *)ERROR_PTR("mask pix not defined", procName, NULL);
    if (connectivity != 4 && connectivity != 8)
        return (PIX *)ERROR_PTR("connectivity not in {4,8}", procName, NULL);

    if (pixSizesEqual(pixs, pixm) == 0)
        return (PIX *)ERROR_PTR("pix sizes unequal", procName, NULL);
    if (pixGetDepth(pixs) != 1)
        return (PIX *)ERROR_PTR("pix not binary", procName, NULL);

    if ((sel_3 = selCreateBrick(3, 3, 1, 1, 1)) == NULL)
        return (PIX *)ERROR_PTR("sel_3 not made", procName, NULL);
    if (connectivity == 4) {  /* remove corner hits to make a '+' */
        selSetElement(sel_3, 0, 0, SEL_DONT_CARE);
        selSetElement(sel_3, 2, 2, SEL_DONT_CARE);
        selSetElement(sel_3, 2, 0, SEL_DONT_CARE);
        selSetElement(sel_3, 0, 2, SEL_DONT_CARE);
    }

    if ((pixt1 = pixCopy(NULL, pixs)) == NULL)
        return (PIX *)ERROR_PTR("pixt1 not made", procName, NULL);
    if ((pixd = pixCreateTemplate(pixs)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);

    iter = 0;
    while (1) {
        iter++;
        pixDilate(pixd, pixt1, sel_3);
        pixAnd(pixd, pixd, pixm);
        pixEqual(pixd, pixt1, &same);
        if (same)
            break;
        else
            SWAP(pixt1, pixd);
    }
    fprintf(stderr, " Num iters in binary reconstruction = %d\n", iter);

    pixDestroy(&pixt1);
    selDestroy(&sel_3);

    return pixd;
}



/*-----------------------------------------------------------------*
 *                   Granulometry on binary images                 *
 *-----------------------------------------------------------------*/
/*!
 *  pixRunHistogramMorph()
 *
 *      Input:  pixs
 *              runtype (L_RUN_OFF, L_RUN_ON)
 *              direction (L_HORIZ, L_VERT)
 *              maxsize  (size of largest runlength counted)
 *      Return: numa of run-lengths
 */
NUMA *
pixRunHistogramMorph(PIX     *pixs,
                     l_int32  runtype,
                     l_int32  direction,
                     l_int32  maxsize)
{
l_int32    count, i;
l_float32  val;
NUMA      *na, *nah;
PIX       *pixt1, *pixt2, *pixt3;
SEL       *sel_2a;

    PROCNAME("pixRunHistogramMorph");

    if (!pixs)
        return (NUMA *)ERROR_PTR("seed pix not defined", procName, NULL);
    if (runtype != L_RUN_OFF && runtype != L_RUN_ON)
        return (NUMA *)ERROR_PTR("invalid run type", procName, NULL);
    if (direction != L_HORIZ && direction != L_VERT)
        return (NUMA *)ERROR_PTR("direction not in {L_HORIZ, L_VERT}",
                                 procName, NULL);

    if (pixGetDepth(pixs) != 1)
        return (NUMA *)ERROR_PTR("pixs must be binary", procName, NULL);

    if ((na = numaCreate(0)) == NULL)
        return (NUMA *)ERROR_PTR("na not made", procName, NULL);

    if (direction == L_HORIZ)
        sel_2a = selCreateBrick(1, 2, 0, 0, 1);
    else   /* direction == L_VERT */
        sel_2a = selCreateBrick(2, 1, 0, 0, 1);
    if (!sel_2a)
        return (NUMA *)ERROR_PTR("sel_2a not made", procName, NULL);

    if (runtype == L_RUN_OFF) {
        if ((pixt1 = pixCopy(NULL, pixs)) == NULL)
            return (NUMA *)ERROR_PTR("pix1 not made", procName, NULL);
        pixInvert(pixt1, pixt1);
    }
    else  /* runtype == L_RUN_ON */
        pixt1 = pixClone(pixs);

    if ((pixt2 = pixCreateTemplate(pixs)) == NULL)
        return (NUMA *)ERROR_PTR("pix2 not made", procName, NULL);
    if ((pixt3 = pixCreateTemplate(pixs)) == NULL)
        return (NUMA *)ERROR_PTR("pix3 not made", procName, NULL);

        /* Get pixel counts at different stages of erosion */
    pixCountPixels(pixt1, &count, NULL);
    numaAddNumber(na, count);
    pixErode(pixt2, pixt1, sel_2a);
    pixCountPixels(pixt2, &count, NULL);
    numaAddNumber(na, count);
    for (i = 0; i < maxsize / 2; i++) {
        pixErode(pixt3, pixt2, sel_2a);
        pixCountPixels(pixt3, &count, NULL);
        numaAddNumber(na, count);
        pixErode(pixt2, pixt3, sel_2a);
        pixCountPixels(pixt2, &count, NULL);
        numaAddNumber(na, count);
    }

        /* Compute length histogram */
    if ((nah = numaCreate(na->n)) == NULL)
        return (NUMA *)ERROR_PTR("nah not made", procName, NULL);
    numaAddNumber(nah, 0); /* number at length 0 */
    for (i = 1; i < na->n - 1; i++) {
        val = na->array[i+1] - 2 * na->array[i] + na->array[i-1];
        numaAddNumber(nah, val);
    }

    pixDestroy(&pixt1);
    pixDestroy(&pixt2);
    pixDestroy(&pixt3);
    selDestroy(&sel_2a);
    numaDestroy(&na);

    return nah;
}


/*-----------------------------------------------------------------*
 *            Composite operations on grayscale images             *
 *-----------------------------------------------------------------*/
/*!
 *  pixTophat()
 *
 *      Input:  pixs
 *              hsize (of Sel; must be odd; origin implicitly in center)
 *              vsize (ditto)
 *              type   (L_TOPHAT_WHITE: image - opening
 *                      L_TOPHAT_BLACK: closing - image)
 *      Return: pixd, or null on error
 *
 *  Notes:
 *      (1) Sel is a brick with all elements being hits
 *      (2) If hsize = vsize = 1, returns an image with all 0 data.
 *      (3) The L_TOPHAT_WHITE flag emphasizes small bright regions,
 *          whereas the L_TOPHAT_BLACK flag emphasizes small dark regions.
 *          The L_TOPHAT_WHITE tophat can be accomplished by doing a
 *          L_TOPHAT_BLACK tophat on the inverse, or v.v.
 */
PIX *
pixTophat(PIX     *pixs,
          l_int32  hsize,
          l_int32  vsize,
          l_int32  type)
{
PIX  *pixd;

    PROCNAME("pixTophat");

    if (!pixs)
        return (PIX *)ERROR_PTR("seed pix not defined", procName, NULL);
    if (pixGetDepth(pixs) != 8)
        return (PIX *)ERROR_PTR("pixs not 8 bpp", procName, NULL);
    if (hsize < 1 || vsize < 1)
        return (PIX *)ERROR_PTR("hsize or vsize < 1", procName, NULL);
    if ((hsize & 1) == 0 ) {
        L_WARNING("horiz sel size must be odd; increasing by 1", procName);
        hsize++;
    }
    if ((vsize & 1) == 0 ) {
        L_WARNING("vert sel size must be odd; increasing by 1", procName);
        vsize++;
    }
    if (type != L_TOPHAT_WHITE && type != L_TOPHAT_BLACK)
        return (PIX *)ERROR_PTR("type must be L_TOPHAT_BLACK or L_TOPHAT_WHITE",
                                procName, NULL);

    if (hsize == 1 && vsize == 1)
        return pixCreateTemplate(pixs);

    switch (type)
    {
    case L_TOPHAT_WHITE:
        if ((pixd = pixOpenGray(pixs, hsize, vsize)) == NULL)
            return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
        pixSubtractGray(pixd, pixs, pixd);
        break;
    case L_TOPHAT_BLACK:
        if ((pixd = pixCloseGray(pixs, hsize, vsize)) == NULL)
            return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
        pixSubtractGray(pixd, pixd, pixs);
        break;
    default:
        return (PIX *)ERROR_PTR("invalid type", procName, NULL);
    }

    return pixd;
}
        

/*!
 *  pixHDome()
 *
 *      Input:  pixs (8 bpp, filling mask)
 *              height (of seed below the filling maskhdome; must be >= 0)
 *              connectivity (4 or 8)
 *      Return: pixd (8 bpp), or null on error
 *
 *  Notes:
 *      (1) It is more efficient to use a connectivity of 4 for the fill.
 *      (2) It is useful to compare the HDome operation with the TopHat.
 *          The latter extracts peaks or valleys that have a width
 *          not exceeding the size of the structuring element used
 *          in the opening or closing, rsp.  The height of the peak is
 *          irrelevant.  By contrast, for the HDome, the gray seedfill
 *          is used to extract all peaks that have a height not exceeding
 *          a given value, regardless of their width!
 *      (3) Slightly more precisely, suppose you set 'height' = 40.
 *          Then all bumps in pixs with a height greater than or equal
 *          to 40 become, in pixd, bumps with a max value of exactly 40.
 *          All shorter bumps have a max value in pixd equal to the height
 *          of the bump.
 *      (4) The method: the filling mask, pixs, is the image whose peaks
 *          are to be extracted.  The height of a peak is the distance
 *          between the top of the peak and the highest "leak" to the
 *          outside -- think of a sombrero, where the leak occurs
 *          at the highest point on the rim.
 *            (a) Generate a seed, pixd, by subtracting some value, p, from
 *                each pixel in the filling mask, pixs.  The value p is
 *                the 'height' input to this function.
 *            (b) Fill in pixd starting with this seed, clipping by pixs,
 *                in the way described in seedfillGrayLow().  The filling
 *                stops before the peaks in pixs are filled.
 *                For peaks that have a height > p, pixd is filled to
 *                the level equal to the (top-of-the-peak - p).
 *                For peaks of height < p, the peak is left unfilled
 *                from its highest saddle point (the leak to the outside).
 *            (c) Subtract the filled seed (pixd) from the filling mask (pixs).
 *          Note that in this procedure, everything is done starting
 *          with the filling mask, pixs.
 *      (5) For segmentation, the resulting image, pixd, can be thresholded
 *          and used as a seed for another filling operation.
 */
PIX *
pixHDome(PIX     *pixs,
         l_int32  height,
         l_int32  connectivity)
{
PIX  *pixd;

    PROCNAME("pixHDome");

    if (!pixs)
        return (PIX *)ERROR_PTR("src pix not defined", procName, NULL);
    if (pixGetDepth(pixs) != 8)
        return (PIX *)ERROR_PTR("pixs not 8 bpp", procName, NULL);
    if (height < 0)
        return (PIX *)ERROR_PTR("height not >= 0", procName, NULL);
    if (height == 0)
        return pixCreateTemplate(pixs);

    if ((pixd = pixCopy(NULL, pixs)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    pixAddConstantGray(pixd, -height);
    pixSeedfillGray(pixd, pixs, connectivity);
    pixSubtractGray(pixd, pixs, pixd);

    return pixd;
}


/*!
 *  pixFastTophat()
 *
 *      Input:  pixs
 *              xsize (width of max/min op, smoothing; any integer >= 1)
 *              ysize (height of max/min op, smoothing; any integer >= 1)
 *              type   (L_TOPHAT_WHITE: image - min
 *                      L_TOPHAT_BLACK: max - image)
 *      Return: pixd, or null on error
 *
 *  Notes:
 *      (1) Don't be fooled. This is NOT a tophat.  It is a tophat-like
 *          operation, where the result is similar to what you'd get
 *          if you used an erosion instead of an opening, or a dilation
 *          instead of a closing.
 *      (2) Instead of opening or closing at full resolution, it does
 *          a fast downscale/minmax operation, then a quick small smoothing
 *          at low res, a replicative expansion of the "background"
 *          to full res, and finally a removal of the background level
 *          from the input image.  The smoothing step may not be important.
 *      (3) It does not remove noise as well as a tophat, but it is
 *          5 to 10 times faster.
 *          If you need the preciseness of the tophat, don't use this.
 *      (4) The L_TOPHAT_WHITE flag emphasizes small bright regions,
 *          whereas the L_TOPHAT_BLACK flag emphasizes small dark regions.
 */
PIX *
pixFastTophat(PIX     *pixs,
              l_int32  xsize,
              l_int32  ysize,
              l_int32  type)
{
PIX  *pixt1, *pixt2, *pixd;

    PROCNAME("pixFastTophat");

    if (!pixs)
        return (PIX *)ERROR_PTR("seed pix not defined", procName, NULL);
    if (pixGetDepth(pixs) != 8)
        return (PIX *)ERROR_PTR("pixs not 8 bpp", procName, NULL);
    if (xsize < 1 || ysize < 1)
        return (PIX *)ERROR_PTR("size < 1", procName, NULL);
    if (type != L_TOPHAT_WHITE && type != L_TOPHAT_BLACK)
        return (PIX *)ERROR_PTR("type must be L_TOPHAT_BLACK or L_TOPHAT_WHITE",
                                procName, NULL);

    if (xsize == 1 && ysize == 1)
        return pixCreateTemplate(pixs);

    switch (type)
    {
    case L_TOPHAT_WHITE:
        if ((pixt1 = pixScaleGrayMinMax(pixs, xsize, ysize, L_CHOOSE_MIN))
               == NULL)
            return (PIX *)ERROR_PTR("pixt1 not made", procName, NULL);
        pixt2 = pixBlockconv(pixt1, 1, 1);  /* small smoothing */
        pixd = pixScaleBySampling(pixt2, xsize, ysize);
        pixSubtractGray(pixd, pixs, pixd);
        break;
    case L_TOPHAT_BLACK:
        if ((pixt1 = pixScaleGrayMinMax(pixs, xsize, ysize, L_CHOOSE_MAX))
               == NULL)
            return (PIX *)ERROR_PTR("pixt1 not made", procName, NULL);
        pixt2 = pixBlockconv(pixt1, 1, 1);  /* small smoothing */
        pixd = pixScaleBySampling(pixt2, xsize, ysize);
        pixSubtractGray(pixd, pixd, pixs);
        break;
    default:
        return (PIX *)ERROR_PTR("invalid type", procName, NULL);
    }

    pixDestroy(&pixt1);
    pixDestroy(&pixt2);
    return pixd;
}
        

/*!
 *  pixMorphGradient()
 *
 *      Input:  pixs
 *              hsize (of Sel; must be odd; origin implicitly in center)
 *              vsize (ditto)
 *              smoothing  (half-width of convolution smoothing filter.
 *                          The width is (2 * smoothing + 1), so 0 is no-op.
 *      Return: pixd, or null on error
 */
PIX *
pixMorphGradient(PIX     *pixs,
                 l_int32  hsize,
                 l_int32  vsize,
                 l_int32  smoothing)
{
PIX  *pixg, *pixd;

    PROCNAME("pixMorphGradient");

    if (!pixs)
        return (PIX *)ERROR_PTR("seed pix not defined", procName, NULL);
    if (pixGetDepth(pixs) != 8)
        return (PIX *)ERROR_PTR("pixs not 8 bpp", procName, NULL);
    if (hsize < 1 || vsize < 1)
        return (PIX *)ERROR_PTR("hsize or vsize < 1", procName, NULL);
    if ((hsize & 1) == 0 ) {
        L_WARNING("horiz sel size must be odd; increasing by 1", procName);
        hsize++;
    }
    if ((vsize & 1) == 0 ) {
        L_WARNING("vert sel size must be odd; increasing by 1", procName);
        vsize++;
    }

        /* Optionally smooth first to remove noise.
         * If smoothing is 0, just get a copy */
    pixg = pixBlockconvGray(pixs, NULL, smoothing, smoothing);

        /* This gives approximately the gradient of a transition */
    pixd = pixDilateGray(pixg, hsize, vsize);
    pixSubtractGray(pixd, pixd, pixg);
    pixDestroy(&pixg);
    return pixd;
}


/*-----------------------------------------------------------------*
 *                            Center of mass                       *
 *-----------------------------------------------------------------*/
/*!
 *  pixaCentroids()
 *
 *      Input:  pixa of components
 *      Return: pta of centroids relative to the UL corner of
 *              each pix, or null on error
 *
 *  Notes:
 *      (1) It is assumed that all pix are the same depth.
 *      (2) Only depths of 1 and 8 bpp are allowed
 */
PTA *
pixaCentroids(PIXA  *pixa)
{
l_int32    d, i, j, k, n, w, h, wpl, pixsum, rowsum, val;
l_float32  xsum, ysum, xave, yave;
l_uint32  *data, *line;
l_uint32   word;
l_uint8    byte;
PIX       *pix;
PTA       *pta;
static l_int32 *centtab = NULL;
static l_int32 *sumtab = NULL;

    PROCNAME("pixaCentroids");

    if (!pixa)
        return (PTA *)ERROR_PTR("pixa not defined", procName, NULL);
    if ((n = pixaGetCount(pixa)) == 0)
        return (PTA *)ERROR_PTR("no pix in pixa", procName, NULL);
    pix = pixaGetPix(pixa, 0, L_CLONE);
    d = pixGetDepth(pix);
    pixDestroy(&pix);
    if (d != 1 && d != 8)
        return (PTA *)ERROR_PTR("depth not 1 or 8 bpp", procName, NULL);

    if ((pta = ptaCreate(n)) == NULL)
        return (PTA *)ERROR_PTR("pta not defined", procName, NULL);

    if ((centtab == NULL) &&
        ((centtab = makePixelCentroidTab8()) == NULL))
        return (PTA *)ERROR_PTR("couldn't make centtab", procName, NULL);

    if ((sumtab == NULL) &&
        ((sumtab = makePixelSumTab8()) == NULL))
        return (PTA *)ERROR_PTR("couldn't make sumtab", procName, NULL);

    for (k = 0; k < n; k++) {
        pix = pixaGetPix(pixa, k, L_CLONE);
        w = pixGetWidth(pix);
        h = pixGetHeight(pix);
        data = pixGetData(pix);
        wpl = pixGetWpl(pix);
        xsum = ysum = 0.0;
        pixsum = 0;
        if (d == 1) {
            for (i = 0; i < h; i++) {
                    /* The body of this loop computes the sum of the set
                     * (1) bits on this row, weighted by their distance
                     * from the left edge of pix, and accumulates that into
                     * xsum; it accumulates their distance from the top
                     * edge of pix into ysum, and their total count into
                     * pixsum.  It's equivalent to
                     * for (j = 0; j < w; j++) {
                     *     if (GET_DATA_BIT(line, j)) {
                     *         xsum += j;
                     *         ysum += i;
                     *         pixsum++;
                     *     }
                     * }
                     */
                line = data + wpl * i;
                rowsum = 0;
                for (j = 0; j < wpl; j++) {
                    word = line[j];
                    if (word) {
                        byte = word & 0xff;
                        rowsum += sumtab[byte];
                        xsum += centtab[byte] + (j * 32 + 24) * sumtab[byte];
                        byte = (word >> 8) & 0xff;
                        rowsum += sumtab[byte];
                        xsum += centtab[byte] + (j * 32 + 16) * sumtab[byte];
                        byte = (word >> 16) & 0xff;
                        rowsum += sumtab[byte];
                        xsum += centtab[byte] + (j * 32 + 8) * sumtab[byte];
                        byte = (word >> 24) & 0xff;
                        rowsum += sumtab[byte];
                        xsum += centtab[byte] + j * 32 * sumtab[byte];
                    }
                }
                pixsum += rowsum;
                ysum += rowsum * i;
            }
            if (pixsum == 0) {
                L_WARNING("no ON pixels in pix", procName);
                ptaAddPt(pta, 0.0, 0.0);   /* this shouldn't happen */
            }
            else {
                xave = xsum / (l_float32)pixsum;
                yave = ysum / (l_float32)pixsum;
                ptaAddPt(pta, xave, yave);
            }
        }
        else {  /* d == 8 */
            for (i = 0; i < h; i++) {
                line = data + wpl * i;
                for (j = 0; j < w; j++) {
                    val = GET_DATA_BYTE(line, j);
                    xsum += val * j;
                    ysum += val * i;
                    pixsum += val;
                }
            }
            if (pixsum == 0) {
                L_WARNING("all pixels are 0", procName);
                ptaAddPt(pta, 0.0, 0.0);   /* this shouldn't happen */
            }
            else {
                xave = xsum / (l_float32)pixsum;
                yave = ysum / (l_float32)pixsum;
                ptaAddPt(pta, xave, yave);
            }
        }
        pixDestroy(&pix);
    }

    return pta;
}
