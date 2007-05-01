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
 *      Top-level fast binary morphology with auto-generated sels
 *
 *            PIX      *pixFMorphopGen_*()
 */

#include <stdio.h>
#include "allheaders.h"

static l_int32   NUM_SELS_GENERATED = 52;

static char  *SEL_NAMES[] = {
                             "sel_2h",
                             "sel_3h",
                             "sel_4h",
                             "sel_5h",
                             "sel_6h",
                             "sel_7h",
                             "sel_8h",
                             "sel_9h",
                             "sel_10h",
                             "sel_11h",
                             "sel_15h",
                             "sel_20h",
                             "sel_21h",
                             "sel_30h",
                             "sel_31h",
                             "sel_40h",
                             "sel_41h",
                             "sel_50h",
                             "sel_51h",
                             "sel_2v",
                             "sel_3v",
                             "sel_4v",
                             "sel_5v",
                             "sel_6v",
                             "sel_7v",
                             "sel_8v",
                             "sel_9v",
                             "sel_10v",
                             "sel_11v",
                             "sel_15v",
                             "sel_20v",
                             "sel_21v",
                             "sel_30v",
                             "sel_31v",
                             "sel_40v",
                             "sel_41v",
                             "sel_50v",
                             "sel_51v",
                             "sel_1",
                             "sel_2",
                             "sel_3",
                             "sel_4",
                             "sel_5",
                             "sel_6",
                             "sel_7",
                             "sel_8",
                             "sel_9",
                             "sel_10",
                             "sel_2dp",
                             "sel_2dm",
                             "sel_5dp",
                             "sel_5dm"};

/*
 *  pixFMorphopGen_*()
 *
 *     Input:  pixd (usual 3 choices: null, == pixs, != pixs)
 *             pixs 
 *             operation  (L_MORPH_DILATE, L_MORPH_ERODE)
 *             sel name
 *     Return: pixd
 *
 *     Action: operation on pixs by the sel
 */
PIX *
pixFMorphopGen_1(PIX      *pixd,
                 PIX      *pixs,
                 l_int32   operation,
                 char     *selname)
{
l_int32    i, index, found, w, h, wpls, wpld;
l_uint32  *datad, *datas, *datat;
PIX       *pixt;

    PROCNAME("pixFMorphopGen_*");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, pixd);
    if (pixGetDepth(pixs) != 1)
        return (PIX *)ERROR_PTR("pixs must be 1 bpp", procName, pixd);

    found = FALSE;
    for (i = 0; i < NUM_SELS_GENERATED; i++) {
        if (strcmp(selname, SEL_NAMES[i]) == 0) {
            found = TRUE;
            index = 2 * i;
            if (operation == L_MORPH_ERODE)
                index++;
            break;
        }
    }
    if (found == FALSE)
        return (PIX *)ERROR_PTR("sel index not found", procName, pixd);

    if (pixd) {
        if (!pixSizesEqual(pixs, pixd))
            return (PIX *)ERROR_PTR("sizes not equal", procName, pixd);
    }
    else {
        if ((pixd = pixCreateTemplate(pixs)) == NULL)
            return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    }

    wpls = pixGetWpl(pixs);
    wpld = pixGetWpl(pixd);

        /*  The images must be surrounded with ADDED_BORDER pixels,
         *  that we'll read from.  We fabricate a "proper"
         *  image as the subimage within the border, having the 
         *  following parameters:  */
    w = pixGetWidth(pixs) - 2 * ADDED_BORDER;
    h = pixGetHeight(pixs) - 2 * ADDED_BORDER;
    datas = pixGetData(pixs) + ADDED_BORDER * wpls + ADDED_BORDER / 32;
    datad = pixGetData(pixd) + ADDED_BORDER * wpld + ADDED_BORDER / 32;

    if (pixd == pixs) {  /* need temp image if in-place */
        if ((pixt = pixCopy(NULL, pixs)) == NULL)
            return (PIX *)ERROR_PTR("pixt not made", procName, pixd);
        datat = pixGetData(pixt) + ADDED_BORDER * wpls + ADDED_BORDER / 32;
        fmorphopgen_low_1(datad, w, h, wpld, datat, wpls, index);
        pixDestroy(&pixt);
    }
    else {  /* simple and not in-place */
        fmorphopgen_low_1(datad, w, h, wpld, datas, wpls, index);
    }

    return pixd;
}

