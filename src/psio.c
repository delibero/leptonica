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
 *  psio.c
 *                     
 *     This is a PostScript "device driver" for wrapping images
 *     in PostScript.  The images can be rendered by a PostScript
 *     interpreter, such as gs or the embedded interpreter in a
 *     PostScript printer.
 *
 *     Convert any image file to PS for embedding
 *          l_int32          convertToPSEmbed()
 *
 *     For uncompressed images
 *
 *          l_int32          pixWritePSEmbed()
 *
 *          l_int32          pixWriteStreamPS()
 *          char            *pixWriteStringPS()
 *
 *          void             getScaledParametersPS()
 *          l_int32          convertByteToHexAscii()
 *
 *     For jpeg compressed images
 *
 *          l_int32          convertJpegToPSEmbed()
 *
 *          l_int32          convertJpegToPS()
 *          l_int32          convertJpegToPSString()
 *          l_int32          extractJpegDataFromFile()
 *          l_int32          extractJpegDataFromArray()
 *
 *          static l_int32   locateJpegImageParameters()
 *          static l_int32   getNextJpegMarker()
 *          static l_int32   getTwoByteParameter()
 *
 *     For tiff g4 compressed images
 *
 *          l_int32          convertTiffG4ToPSEmbed()
 *
 *          l_int32          convertTiffG4ToPS()
 *          l_int32          convertTiffG4ToPSString()
 *          l_int32          extractTiffG4DataFromFile()
 *
 *     For multipage tiff images
 *
 *          l_int32          convertTiffMultipageToPS()
 *
 *     Converting resolution
 *          l_int32          getResLetterPage()
 *          l_int32          getResA4Page()
 *
 *     Utility for encoding and decoding data with ascii85
 *
 *          char            *encodeAscii85()
 *          l_int32         *convertChunkToAscii85()
 *          l_uint8         *decodeAscii85()
 *
 *  These PostScript converters are used in three different ways:
 *
 *  (1) For embedding a PS file in a program like TeX.  We must have
 *      a bounding box.  convertToPSEmbed() handles this for
 *      both level 1 and level 2 output, and prog/converttops
 *      wraps this in an executable.  converttops is a generalization
 *      of Thomas Merz's jpeg2ps wrapper, in that it works for
 *      all types (formats, depth, colormap) of input images and
 *      gives PS output in either compressed or uncompressed format,
 *      depending on an input flag.
 *
 *  (2) For composing a set of pages with any number of images
 *      painted on them, in DCT or G4 compressed format depending
 *      on if the image is grayscale/color or binary.  Because we
 *      append each PS string and specify the scaling and placement
 *      explicitly, one must NOT have a bounding box attached to
 *      each separate image.  
 *
 *  (3) For printing a page image or a set of page images, at a
 *      resolution that optimally fills the page.  Here we use
 *      a bounding box and scale the image appropriately.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "allheaders.h"

static const char *TEMP_G4TIFF_FILE = "/usr/tmp/junk_temp_g4tiff.tif";
static const char *TEMP_JPEG_FILE   = "/usr/tmp/junk_temp_jpeg.jpg";

    /* MS VC++ can't handle array initialization with static consts ! */
#define L_BUF_SIZE      512

static const l_int32  DEFAULT_PRINTER_RES     = 300; /* default printing ppi */
static const l_int32  MIN_RES                 = 5;
static const l_int32  MAX_RES                 = 3000;
static const l_int32  MAX_85_LINE_COUNT       = 64;

    /* For computing resolution that fills page to desired amount */
static const l_int32  LETTER_WIDTH            = 612;   /* points */
static const l_int32  LETTER_HEIGHT           = 792;   /* points */
static const l_int32  A4_WIDTH                = 595;   /* points */
static const l_int32  A4_HEIGHT               = 842;   /* points */
static const l_float32  DEFAULT_FILL_FRACTION = 0.95;

static const l_uint32  power85[5] = {1,
                                     85,
                                     85 * 85,
                                     85 * 85 * 85,
                                     85 * 85 * 85 * 85};

static l_int32  locateJpegImageParameters(l_uint8 *, l_int32, l_int32 *);
static l_int32  getNextJpegMarker(l_uint8 *, l_int32, l_int32 *);
static l_int32  getTwoByteParameter(l_uint8 *, l_int32);


#ifndef  NO_CONSOLE_IO
#define  DEBUG_JPEG     0
#define  DEBUG_G4       0
#endif  /* ~NO_CONSOLE_IO */

    /* This should be false for documents that are composited from
     * sequences of painted images, where more than one image can
     * be placed in an arbitrary location on any page.  
     * However, for images that are composited, we use special *Embed()
     * functions for writing PostScript with bounding boxes, so they
     * can be embedded in TeX files, e.g. */
#define  PRINT_BOUNDING_BOX      0



/*-------------------------------------------------------------*
 *            Convert any image file to PS for embedding       *
 *-------------------------------------------------------------*/
/*
 *  convertToPSEmbed()
 *
 *      Input:  filein (input file)
 *              fileout (output ps file)
 *              level (1 - uncompressed,  2 - compressed)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) This is a wrapper function that generates a PS file with
 *          a bounding box, from any input image file.
 *      (2) Colormaps are removed.
 *      (3) If the image is not 1 bpp and is not jpeg compressed,
 *          and it is to be written as PS with DCT compression
 *          (level = 2), it will first be written to file as jpeg with
 *          quality = 75.  This will cause some degradation in the image.
 *      (4) The bounding box is required when a program such as TeX
 *          (through epsf) places and rescales the image.
 *      (5) The bounding box is sized for fitting the image to an
 *          8.5 x 11.0 inch page.
 */
l_int32
convertToPSEmbed(const char  *filein,
                 const char  *fileout,
                 l_int32     level)
{
l_int32  d, format;
FILE    *fp;
PIX     *pix, *pixs;

    PROCNAME("convertToPSEmbed");

    if (!filein)
        return ERROR_INT("filein not defined", procName, 1);
    if (!fileout)
        return ERROR_INT("fileout not defined", procName, 1);

    if (level == 1) {
        pixWritePSEmbed(filein, fileout);
        return 0;
    }

        /* We must write out level 2 PS */
    if ((fp = fopen(filein, "r")) == NULL)
        return ERROR_INT("filein not found", procName, 1);
    format = findFileFormat(fp);
    fclose(fp);
    if (format == IFF_JFIF_JPEG) {  /* write out directly */
        convertJpegToPSEmbed(filein, fileout);
        return 0;
    }

        /* We need to convert to jpeg or tiff g4.
         * If it's already in tiff g4 format, we take the hit of
         * reading it in and writing it back out the same way. */
    if ((pixs = pixRead(filein)) == NULL)
        return ERROR_INT("image not read from file", procName, 1);
    d = pixGetDepth(pixs);
    if (d == 16)
        pix = pixConvert16To8(pixs, 1);
    else
        pix = pixRemoveColormap(pixs, REMOVE_CMAP_BASED_ON_SRC);
    d = pixGetDepth(pix);
    if (d == 1) {
        pixWrite(TEMP_G4TIFF_FILE, pix, IFF_TIFF_G4);
        convertTiffG4ToPSEmbed(TEMP_G4TIFF_FILE, fileout);
    }
    else {
        pixWrite(TEMP_JPEG_FILE, pix, IFF_JFIF_JPEG);
        convertJpegToPSEmbed(TEMP_JPEG_FILE, fileout);
    }

    pixDestroy(&pix);
    pixDestroy(&pixs);
    return 0;
}


/*-------------------------------------------------------------*
 *                  For uncompressed images                    *
 *-------------------------------------------------------------*/
/*!
 *  pixWritePSEmbed()
 *
 *      Input:  filein (input file)
 *              fileout (output ps file)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) This is a simple wrapper function that generates an
 *          uncompressed PS file, with a bounding box.
 *      (2) The bounding box is required when a program such as TeX
 *          (through epsf) places and rescales the image.
 *      (3) The bounding box is sized for fitting the image to an
 *          8.5 x 11.0 inch page.
 */
l_int32
pixWritePSEmbed(const char  *filein,
                const char  *fileout)
{
l_int32    w, h;
l_float32  scale;
FILE      *fp;
PIX       *pix;

    PROCNAME("pixWritePSEmbed");

    if (!filein)
        return ERROR_INT("filein not defined", procName, 1);
    if (!fileout)
        return ERROR_INT("fileout not defined", procName, 1);

    if ((pix = pixRead(filein)) == NULL)
        return ERROR_INT("image not read from file", procName, 1);
    w = pixGetWidth(pix);
    h = pixGetHeight(pix);
    if (w * 11.0 > h * 8.5)
        scale = 8.5 * 300. / (l_float32)w;
    else
        scale = 11.0 * 300. / (l_float32)h;

    if ((fp = fopen(fileout, "w")) == NULL)
        return ERROR_INT("file not opened for write", procName, 1);
    pixWriteStreamPS(fp, pix, NULL, 0, scale);
    fclose(fp);

    pixDestroy(&pix);
    return 0;
}


/*!
 *  pixWriteStreamPS()
 *
 *      Input:  stream
 *              pix
 *              box  (<optional>)
 *              res  (can use 0 for default of 300 ppi)
 *              scale (to prevent scaling, use either 1.0 or 0.0)
 *      Return: 0 if OK; 1 on error
 *
 *  Notes:
 *      (1) This writes image in PS format, optionally scaled,
 *          adjusted for the printer resolution, and with
 *          a bounding box.
 *      (2) For details on use of parameters, see pixWriteStringPS().
 */
l_int32
pixWriteStreamPS(FILE      *fp,
                 PIX       *pix,
                 BOX       *box,
                 l_int32    res,
                 l_float32  scale)
{
char    *pstring;
l_int32  length;
PIX     *pixc;

    PROCNAME("pixWriteStreamPS");

    if (!fp)
        return (l_int32)ERROR_INT("stream not open", procName, 1);
    if (!pix)
        return (l_int32)ERROR_INT("pix not defined", procName, 1);

    if ((pixc = pixConvertForPSWrap(pix)) == NULL)
        return (l_int32)ERROR_INT("pixc not made", procName, 1);

    pstring = pixWriteStringPS(pixc, box, res, scale);
    length = strlen(pstring);
    fwrite(pstring, 1, length, fp);
    FREE(pstring);
    pixDestroy(&pixc);

    return 0;
}


/*!
 *  pixWriteStringPS()
 *
 *      Input:  pix:  1 bpp, colormapped, 8 bpp grayscale, 32 bpp (RGB)
 *              box:  (a) If box == null, image is placed, optionally scaled,
 *                        in a standard b.b. at the center of the page.
 *                        This is to be used when another program like
 *                        TeX (through epsf) places the image.
 *                    (b) If box != null, image is placed without a
 *                        b.b. at the specified page location and with
 *                        (optional) scaling.  This is to be used when
 *                        you want to specify exactly where (and optionally
 *                        how big) you want the image to be.
 *                        Note that all coordinates are in PS convention,
 *                        with (0,0) at LL corner of the page:
 *                            (x,y)    location of LL corner of image, in mils.
 *                            (w,h)    scaled size, in mils.  Use 0 to
 *                                     scale with "scale" and "res" input.
 *              res:  resolution, in printer ppi.  Use 0 for default (300 ppi).
 *              scale: scale factor.  If no scaling is desired, use
 *                     either 1.0 or 0.0.   Scaling just resets the resolution
 *                     parameter; the actual scaling is done in the
 *                     interpreter at rendering time.  This is important:
 *                     it allows you to scale the image up without
 *                     increasing the file size.
 *
 *      Return: ps string if OK, or null on error
 *
 *  Usage notes:
 *      OK, this seems a bit complicated, because there are various
 *      ways to scale and not to scale.
 *      If you don't want any scaling at all:
 *          if you are using a box:
 *              set w = 0, h = 0, and use scale = 1.0; it will print
 *              each pixel unscaled at printer resolution
 *          if you are not using a box:
 *              set scale = 1.0; it will print at printer resolution
 *      If you want the image to be a certain size in inches:
 *          you must use a box and set the box (w,h) in mils
 *      If you want the image to be scaled by a scale factor != 1.0:
 *          if you are using a box:
 *              set w = 0, h = 0, and use the desired scale factor;
 *              the higher the printer resolution, the smaller the
 *              image will actually appear.
 *          if you are not using a box:
 *              set the desired scale factor; the higher the printer
 *              resolution, the smaller the image will actually appear.
 *                     
 *  Distance units:
 *      The interface distances are in milli-inches.
 *      In addition, we use 3 different units internally:
 *          - pixels  (units of 1/res inch)
 *          - printer pts (units of 1/72 inch)
 *          - inches
 *  
 *  Here is a quiz on units from a reviewer:
 *      How many UK milli-cups in a US kilo-teaspoon?
 *      (Hint: a US cup = 0.75 UK cups + 2 desertspoons.)
 */
char *
pixWriteStringPS(PIX       *pixs,
                 BOX       *box,
                 l_int32    res,
                 l_float32  scale)
{
char       nib1, nib2;
char       bigbuf[L_BUF_SIZE];
char      *hexdata, *pstring;
l_uint8    byteval;
l_int32    i, j, k, d, wpix, hpix;
l_float32  wpt, hpt, xpt, ypt;
l_int32    wpl, psbpl, hexbytes, boxflag, sampledepth;
l_uint32  *line, *data;
PIX       *pix;
SARRAY    *sa;

    PROCNAME("pixWriteStringPS");

    if (!pixs)
        return (char *)ERROR_PTR("pix not defined", procName, NULL);

    d = pixGetDepth(pixs);
    if (d == 16)
        pix = pixConvert16To8(pixs, 1);
    else
        pix = pixRemoveColormap(pixs, REMOVE_CMAP_BASED_ON_SRC);
    d = pixGetDepth(pix);

        /* Get the factors by which PS scales and translates, in pts */
    wpix = pixGetWidth(pix);
    hpix = pixGetHeight(pix);
    if (!box)
        boxflag = 0;  /* no scaling; b.b. at center */
    else
        boxflag = 1;  /* no b.b., specify placement and optional scaling */
    getScaledParametersPS(box, wpix, hpix, res, scale, &xpt, &ypt, &wpt, &hpt);

    if (d == 1)
        sampledepth = 1;
    else  /* d == 8 || d == 32 */
        sampledepth = 8;

        /* Convert image data to hex string */
    wpl = pixGetWpl(pix);
    if (d == 1 || d == 8)
        psbpl = (wpix * d + 7) / 8;   /* packed to byte boundary */
    else /* d == 32 */
        psbpl = 3 * wpix;   /* packed to byte boundary */
    data = pixGetData(pix);
    hexbytes = 2 * psbpl * hpix;  /* size of ps hex array */
    if ((hexdata = (char *)CALLOC(hexbytes + 1, sizeof(char))) == NULL)
        return (char *)ERROR_PTR("hexdata not made", procName, NULL);
    if (d == 1 || d == 8) {
        for (i = 0, k = 0; i < hpix; i++) {
            line = data + i * wpl;
            for (j = 0; j < psbpl; j++) {
                byteval = GET_DATA_BYTE(line, j);
                convertByteToHexAscii(byteval, &nib1, &nib2);
                hexdata[k++] = nib1;
                hexdata[k++] = nib2;
            }
        }
    }
    else  {  /* d == 32; hexdata bytes packed RGBRGB..., 2 per sample */
        for (i = 0, k = 0; i < hpix; i++) {
            line = data + i * wpl;
            for (j = 0; j < wpix; j++) {
                byteval = GET_DATA_BYTE(line + j, 0);  /* red */
                convertByteToHexAscii(byteval, &nib1, &nib2);
                hexdata[k++] = nib1;
                hexdata[k++] = nib2;
                byteval = GET_DATA_BYTE(line + j, 1);  /* green */
                convertByteToHexAscii(byteval, &nib1, &nib2);
                hexdata[k++] = nib1;
                hexdata[k++] = nib2;
                byteval = GET_DATA_BYTE(line + j, 2);  /* blue */
                convertByteToHexAscii(byteval, &nib1, &nib2);
                hexdata[k++] = nib1;
                hexdata[k++] = nib2;
            }
        }
    }
    hexdata[k] = '\0';

    if ((sa = sarrayCreate(0)) == NULL)
        return (char *)ERROR_PTR("sa not made", procName, NULL);

    sarrayAddString(sa, "%!Adobe-PS", 1);
    if (boxflag == 0) {
        sprintf(bigbuf,
            "%%%%BoundingBox: %7.2f %7.2f %7.2f %7.2f",
            xpt, ypt, xpt + wpt, ypt + hpt);
        sarrayAddString(sa, bigbuf, 1);
    }
    else    /* boxflag == 1 */
        sarrayAddString(sa, "gsave", 1);

    if (d == 1)
        sarrayAddString(sa, "{1 exch sub} settransfer    %invert binary", 1);

    sprintf(bigbuf, "/bpl %d string def         %%bpl as a string", psbpl);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf,
        "%7.2f %7.2f translate         %%set image origin in pts", xpt, ypt);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf,
        "%7.2f %7.2f scale             %%set image size in pts", wpt, hpt);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf,
        "%d %d %d                 %%image dimensions in pixels",
            wpix, hpix, sampledepth);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf,
        "[%d %d %d %d %d %d]     %%mapping matrix: [wpix 0 0 -hpix 0 hpix]",
               wpix, 0, 0, -hpix, 0, hpix);
    sarrayAddString(sa, bigbuf, 1);

    if (boxflag == 0) {
        if (d == 1 || d == 8)
            sarrayAddString(sa, "{currentfile bpl readhexstring pop} image", 1);
        else  /* d == 32 */
            sarrayAddString(sa,
                "{currentfile bpl readhexstring pop} false 3 colorimage", 1);
    }
    else {  /* boxflag == 1 */
        if (d == 1 || d == 8)
            sarrayAddString(sa,
                "{currentfile bpl readhexstring pop} bind image", 1);
        else  /* d == 32 */
            sarrayAddString(sa,
                "{currentfile bpl readhexstring pop} bind false 3 colorimage",
                 1);
    }

    sarrayAddString(sa, hexdata, 0);

    if (boxflag == 0)
        sarrayAddString(sa, "\nshowpage", 1);
    else  /* boxflag == 1 */
        sarrayAddString(sa, "\ngrestore", 1);

    if ((pstring = sarrayToString(sa, 1)) == NULL)
        return (char *)ERROR_PTR("pstring not made", procName, NULL);

    sarrayDestroy(&sa);
    pixDestroy(&pix);
    return pstring;
}


/*!
 *  getScaledParametersPS()
 *
 *      Input:  box (<optional> location of image in mils; with
 *                   (x,y) being the LL corner)
 *              wpix (pix width in pixels)
 *              hpix (pix height in pixels)
 *              res (of printer; use 0 for default)
 *              scale (use 1.0 or 0.0 for no scaling) 
 *              &xpt (location of llx in pts)
 *              &ypt (location of lly in pts)
 *              &wpt (image width in pts)
 *              &hpt (image height in pts)
 *      Return: void (no arg checking)
 *
 *  Notes:
 *      (1) The image is always scaled, depending on res and scale.
 *      (2) If no box, the image is centered on the page.
 *      (3) If there is a box, the image is placed within it.
 */
void
getScaledParametersPS(BOX        *box,
                      l_int32     wpix,
                      l_int32     hpix,
                      l_int32     res,
                      l_float32   scale,
                      l_float32  *pxpt,
                      l_float32  *pypt,
                      l_float32  *pwpt,
                      l_float32  *phpt)
{
l_float32  winch, hinch, xinch, yinch, fres;

    PROCNAME("getScaledParametersPS");

    if (res == 0)
        res = DEFAULT_PRINTER_RES;
    fres = (l_float32)res;

        /* Allow the PS interpreter to scale the resolution */
    if (scale == 0.0)
        scale = 1.0;
    if (scale != 1.0) {
        fres = (l_float32)res / scale;
        res = (l_int32)fres;
    }

        /* Limit valid resolution interval */
    if (res < MIN_RES || res > MAX_RES) {
        L_WARNING_INT("res %d out of bounds; using default res; no scaling",
                      procName, res);
        res = DEFAULT_PRINTER_RES;
        fres = (l_float32)res;
    }

    if (!box) {  /* center on page */
        winch = (l_float32)wpix / fres;
        hinch = (l_float32)hpix / fres;
        xinch = (8.5 - winch) / 2.;
        yinch = (11.0 - hinch) / 2.;
    }
    else {
        if (box->w == 0)
            winch = (l_float32)wpix / fres;
        else
            winch = (l_float32)box->w / 1000.;
        if (box->h == 0)
            hinch = (l_float32)hpix / fres;
        else
            hinch = (l_float32)box->h / 1000.;
        xinch = (l_float32)box->x / 1000.;
        yinch = (l_float32)box->y / 1000.;
    }

    if (xinch < 0)
        L_WARNING("left edge < 0.0 inch", procName);
    if (xinch + winch > 8.5)
        L_WARNING("right edge > 8.5 inch", procName);
    if (yinch < 0.0)
        L_WARNING("bottom edge < 0.0 inch", procName);
    if (yinch + hinch > 11.0)
        L_WARNING("top edge > 11.0 inch", procName);

    *pwpt = 72. * winch;
    *phpt = 72. * hinch;
    *pxpt = 72. * xinch;
    *pypt = 72. * yinch;
    return;
}
    

/*!
 *  convertByteToHexAscii()
 *
 *      Input:  byteval  (input byte)
 *             &nib1, &nib2  (<return> two hex ascii characters)
 *      Return: void
 */
void
convertByteToHexAscii(l_uint8  byteval,
                      char    *pnib1,
                      char    *pnib2)
{
l_uint8  nib;

    nib = byteval >> 4;
    if (nib < 10)
        *pnib1 = '0' + nib;
    else
        *pnib1 = 'a' + (nib - 10);
    nib = byteval & 0xf;
    if (nib < 10)
        *pnib2 = '0' + nib;
    else
        *pnib2 = 'a' + (nib - 10);

    return;
}


/*-------------------------------------------------------------*
 *                  For jpeg compressed images                 *
 *-------------------------------------------------------------*/
/*!
 *  convertJpegToPSEmbed()
 *
 *      Input:  filein (input jpeg file)
 *              fileout (output ps file)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) This function takes a jpeg file as input and generates a DCT
 *          compressed, ascii85 encoded PS file, with a bounding box.
 *      (2) The bounding box is required when a program such as TeX
 *          (through epsf) places and rescales the image.
 *      (3) The bounding box is sized for fitting the image to an
 *          8.5 x 11.0 inch page.
 */
l_int32
convertJpegToPSEmbed(const char  *filein,
                     const char  *fileout)
{
char      *pstring, *outstr;
char      *data85;  /* ascii85 encoded file */
char       bigbuf[512];
l_uint8   *bindata;  /* binary encoded jpeg data (entire file) */
l_int32    bps, w, h, spp;
l_int32    nbinbytes, psbytes, nbytes85, totbytes;
l_float32  xpt, ypt, wpt, hpt;
SARRAY    *sa;

    PROCNAME("convertJpegToPSEmbed");

    if (!filein)
        return ERROR_INT("filein not defined", procName, 1);
    if (!fileout)
        return ERROR_INT("fileout not defined", procName, 1);

        /* The returned jpeg data in memory is the entire jpeg file,
         * which starts with ffd8 and ends with ffd9 */
    if (extractJpegDataFromFile(filein, &bindata, &nbinbytes,
                                &w, &h, &bps, &spp))
        return ERROR_INT("bindata not extracted from file", procName, 1);

        /* Convert entire jpeg file of encoded DCT data to ascii85 */
    data85 = encodeAscii85(bindata, nbinbytes, &nbytes85);
    FREE(bindata);
    if (!data85)
        return ERROR_INT("data85 not made", procName, 1);

        /* Scale for 20 pt boundary and otherwise full filling
         * in one direction on 8.5 x 11 inch device */
    xpt = 20.0;
    ypt = 20.0;
    if (w * 11.0 > h * 8.5) {
        wpt = 572.0;   /* 612 - 2 * 20 */
        hpt = wpt * (l_float32)h / (l_float32)w;
    }
    else {
        hpt = 752.0;   /* 792 - 2 * 20 */
        wpt = hpt * (l_float32)w / (l_float32)h;
    }

        /*  -------- Generate PostScript output -------- */
    if ((sa = sarrayCreate(50)) == NULL)
        return ERROR_INT("sa not made", procName, 1);

    sarrayAddString(sa, "%!PS-Adobe-3.0", 1);
    sarrayAddString(sa, "%%Creator: leptonica", 1);
    sprintf(bigbuf, "%%%%Title: %s", filein);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf,
        "%%%%BoundingBox: %7.2f %7.2f %7.2f %7.2f",
                   xpt, ypt, xpt + wpt, ypt + hpt);
    sarrayAddString(sa, bigbuf, 1);
    sarrayAddString(sa, "%%DocumentData: Clean7Bit", 1);
    sarrayAddString(sa, "%%LanguageLevel: 2", 1);
    sarrayAddString(sa, "%%EndComments", 1);
    sarrayAddString(sa, "%%Page: 1 1", 1);

    sarrayAddString(sa, "save", 1);
    sarrayAddString(sa, "/RawData currentfile /ASCII85Decode filter def", 1);
    sarrayAddString(sa, "/Data RawData << >> /DCTDecode filter def", 1);

    sprintf(bigbuf,
        "%7.2f %7.2f translate         %%set image origin in pts", xpt, ypt);
    sarrayAddString(sa, bigbuf, 1);

    sprintf(bigbuf,
        "%7.2f %7.2f scale             %%set image size in pts", wpt, hpt);
    sarrayAddString(sa, bigbuf, 1);

    if (spp == 1)
        sarrayAddString(sa, "/DeviceGray setcolorspace", 1);
    else if (spp == 3)
        sarrayAddString(sa, "/DeviceRGB setcolorspace", 1);
    else  /*spp == 4 */
        sarrayAddString(sa, "/DeviceCMYK setcolorspace", 1);
    
    sarrayAddString(sa, "{ << /ImageType 1", 1);
    sprintf(bigbuf, "     /Width %d", w);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf, "     /Height %d", h);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf, "     /ImageMatrix [ %d 0 0 %d 0 %d ]", w, -h, h);
    sarrayAddString(sa, bigbuf, 1);
    sarrayAddString(sa, "     /DataSource Data", 1);
    sprintf(bigbuf, "     /BitsPerComponent %d", bps);
    sarrayAddString(sa, bigbuf, 1);

    if (spp == 1)
        sarrayAddString(sa, "     /Decode [0 1]", 1);
    else if (spp == 3)
        sarrayAddString(sa, "     /Decode [0 1 0 1 0 1]", 1);
    else   /* spp == 4 */
        sarrayAddString(sa, "     /Decode [0 1 0 1 0 1 0 1]", 1);
    
    sarrayAddString(sa, "  >> image", 1);
    sarrayAddString(sa, "  Data closefile", 1);
    sarrayAddString(sa, "  RawData flushfile", 1);
    sarrayAddString(sa, "  showpage", 1);
    sarrayAddString(sa, "  restore", 1);
    sarrayAddString(sa, "} exec", 1);

    if ((pstring = sarrayToString(sa, 1)) == NULL)
        return ERROR_INT("pstring not made", procName, 1);
    sarrayDestroy(&sa);
    psbytes = strlen(pstring);

        /* Add the ascii85 data */
    totbytes = psbytes + nbytes85;
    if ((outstr = (char *)CALLOC(totbytes + 4, sizeof(char))) == NULL)
        return ERROR_INT("outstr not made", procName, 1);
    memcpy(outstr, pstring, psbytes);
    memcpy(outstr + psbytes, data85, nbytes85);
    FREE(pstring);
    FREE(data85);

    if (arrayWrite(fileout, "w", outstr, totbytes))
        return ERROR_INT("ps string not written to file", procName, 1);
    FREE(outstr);
    return 0;
}


/*!
 *  convertJpegToPS()
 *
 *      Input:  filein (input jpeg file)
 *              fileout (output ps file)
 *              operation ("w" for write; "a" for append)
 *              x, y (location of LL corner of image, in pixels, relative
 *                    to the PostScript origin (0,0) at the LL corner
 *                    of the page)
 *              res (resolution of the input image, in ppi; use 0 for default)
 *              scale (scaling by printer; use 0.0 or 1.0 for no scaling)
 *              pageno (page number; must start with 1; you can use 0
 *                  if there is only one page.)
 *              endpage (boolean: TRUE if the last image to be
 *                  added to the page; FALSE otherwise)
 *      Return: 0 if OK, 1 on error
 *
 *  Usage:  This is simpler to use than pixWriteStringPS(), and
 *          it outputs in level 2 PS as compressed DCT (overlaid
 *          with ascii85 encoding).
 *
 *          An output file can contain multiple pages, each with
 *          multiple images.  The arguments to convertJpegToPS()
 *          allow you to control placement of jpeg images on multiple
 *          pages within a PostScript file.
 *
 *          For the first image written to a file, use "w", which
 *          opens for write and clears the file.  For all subsequent
 *          images written to that file, use "a".
 *
 *          The (x, y) parameters give the LL corner of the image
 *          relative to the LL corner of the page.  They are in
 *          units of pixels if scale = 1.0.  If you use (e.g.)
 *          scale = 2.0, the image is placed at (2x, 2y) on the page,
 *          and the image dimensions are also doubled.
 *
 *          If your display is 75 ppi and your image was created
 *          at a resolution of 300 ppi, you can get the image
 *          to print at the same size as it appears on your display
 *          by either setting scale = 4.0 or by setting  res = 75.
 *          Both tell the printer to make a 4x enlarged image.
 *
 *          If your image is generated at 150 ppi and you use scale = 1,
 *          it will be rendered such that 150 pixels correspond
 *          to 72 pts (1 inch on the printer).  This function does
 *          the conversion from pixels (with or without scaling) to
 *          pts, which are the units that the printer uses.
 *
 *          The printer will choose its own resolution to use
 *          in rendering the image, which will not affect the size
 *          of the rendered image.  That is because the output
 *          PostScript file describes the geometry in terms of pts,
 *          which are defined to be 1/72 inch.  The printer will
 *          only see the size of the image in pts, through the
 *          scale and translate parameters and the affine
 *          transform (the ImageMatrix) of the image.
 *
 *          To render multiple images on the same page, set
 *          endpage = FALSE for each image until you get to the
 *          last, for which you set endpage = TRUE.  This causes the
 *          "showpage" command to be invoked.  Showpage outputs
 *          the entire page and clears the raster buffer for the
 *          next page to be added.  Without a "showpage",
 *          subsequent images from the next page will overlay those
 *          previously put down.
 *
 *          For multiple pages, increment the page number, starting
 *          with page 1.  This allows PostScript (and PDF) to build
 *          a page directory, which viewers use for navigation.
 */
l_int32
convertJpegToPS(const char  *filein,
                const char  *fileout,
                const char  *operation,
                l_int32      x,
                l_int32      y,
                l_int32      res,
                l_float32    scale,
                l_int32      pageno,
                l_int32      endpage)
{
char    *outstr;
l_int32  nbytes;

    PROCNAME("convertJpegToPS");

    if (!filein)
        return ERROR_INT("filein not defined", procName, 1);
    if (!fileout)
        return ERROR_INT("fileout not defined", procName, 1);
    if (strcmp(operation, "w") && strcmp(operation, "a"))
        return ERROR_INT("operation must be \"w\" or \"a\"", procName, 1);

    if (convertJpegToPSString(filein, &outstr, &nbytes, x, y, res, scale,
                          pageno, endpage))
        return ERROR_INT("ps string not made", procName, 1);

    if (arrayWrite(fileout, operation, outstr, nbytes))
        return ERROR_INT("ps string not written to file", procName, 1);

    FREE(outstr);
    return 0;
}


/*!
 *  convertJpegToPSString()
 *
 *      Generates PS string in jpeg format from jpeg file
 *
 *      Input:  filein (input jpeg file)
 *              &poutstr (<return> PS string)
 *              &nbytes (<return> number of bytes in PS string)
 *              x, y (location of LL corner of image, in pixels, relative
 *                    to the PostScript origin (0,0) at the LL corner
 *                    of the page)
 *              res (resolution of the input image, in ppi; use 0 for default)
 *              scale (scaling by printer; use 0.0 or 1.0 for no scaling)
 *              pageno (page number; must start with 1; you can use 0
 *                  if there is only one page.)
 *              endpage (boolean: TRUE if the last image to be
 *                  added to the page; FALSE otherwise)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) The returned PS character array is binary string, not a
 *          null-terminated ascii C string.  It has null bytes
 *          embedded in it!
 *
 *  Usage:  See convertJpegToPS()
 */
l_int32
convertJpegToPSString(const char  *filein,
                      char       **poutstr,
                      l_int32     *pnbytes,
                      l_int32      x,
                      l_int32      y,
                      l_int32      res,
                      l_float32    scale,
                      l_int32      pageno,
                      l_int32      endpage)
{
char      *pstring, *outstr;
char      *data85;  /* ascii85 encoded file */
char       bigbuf[L_BUF_SIZE];
l_uint8   *bindata;  /* binary encoded jpeg data (entire file) */
l_int32    bps, w, h, spp;
l_int32    nbinbytes, psbytes, nbytes85, totbytes;
l_float32  xpt, ypt, wpt, hpt;
SARRAY    *sa;

    PROCNAME("convertJpegToPSString");

    if (!filein)
        return ERROR_INT("filein not defined", procName, 1);
    if (!poutstr)
        return ERROR_INT("&outstr not defined", procName, 1);
    if (!pnbytes)
        return ERROR_INT("&nbytes not defined", procName, 1);
    *poutstr = NULL;

        /* The returned jpeg data in memory is the entire jpeg file,
         * which starts with ffd8 and ends with ffd9 */
    if (extractJpegDataFromFile(filein, &bindata, &nbinbytes,
                                &w, &h, &bps, &spp))
        return ERROR_INT("bindata not extracted from file", procName, 1);

        /* Convert entire jpeg file of encoded DCT data to ascii85 */
    data85 = encodeAscii85(bindata, nbinbytes, &nbytes85);
    FREE(bindata);
    if (!data85)
        return ERROR_INT("data85 not made", procName, 1);

#if  DEBUG_JPEG
    fprintf(stderr, "w = %d, h = %d, bps = %d, spp = %d\n", w, h, bps, spp);
    fprintf(stderr, "nbinbytes = %d, nbytes85 = %d, ratio = %5.3f\n",
           nbinbytes, nbytes85, (l_float32)nbytes85 / (l_float32)nbinbytes);
#endif   /* DEBUG_JPEG */

        /* Get scaled location in pts */
    if (scale == 0.0)
        scale = 1.0;
    if (res == 0)
        res = DEFAULT_PRINTER_RES;
    xpt = scale * x * 72. / res;
    ypt = scale * y * 72. / res;
    wpt = scale * w * 72. / res;
    hpt = scale * h * 72. / res;

    if (pageno == 0)
        pageno = 1;

#if  DEBUG_JPEG
    fprintf(stderr, "xpt = %7.2f, ypt = %7.2f, wpt = %7.2f, hpt = %7.2f\n",
             xpt, ypt, wpt, hpt);
#endif   /* DEBUG_JPEG */

        /*  -------- Generate PostScript output -------- */
    if ((sa = sarrayCreate(50)) == NULL)
        return ERROR_INT("sa not made", procName, 1);

    sarrayAddString(sa, "%!PS-Adobe-3.0", 1);
    sarrayAddString(sa, "%%Creator: leptonica", 1);
    sprintf(bigbuf, "%%%%Title: %s", filein);
    sarrayAddString(sa, bigbuf, 1);

#if  PRINT_BOUNDING_BOX
    sprintf(bigbuf,
        "%%%%BoundingBox: %7.2f %7.2f %7.2f %7.2f",
                   xpt, ypt, xpt + wpt, ypt + hpt);
    sarrayAddString(sa, bigbuf, 1);
#endif  /* PRINT_BOUNDING_BOX */

    sarrayAddString(sa, "%%DocumentData: Clean7Bit", 1);
    sarrayAddString(sa, "%%LanguageLevel: 2", 1);
    sarrayAddString(sa, "%%EndComments", 1);
    sprintf(bigbuf, "%%%%Page: %d %d", pageno, pageno);
    sarrayAddString(sa, bigbuf, 1);

    sarrayAddString(sa, "save", 1);
    sarrayAddString(sa, "/RawData currentfile /ASCII85Decode filter def", 1);
    sarrayAddString(sa, "/Data RawData << >> /DCTDecode filter def", 1);

    sprintf(bigbuf,
        "%7.2f %7.2f translate         %%set image origin in pts", xpt, ypt);
    sarrayAddString(sa, bigbuf, 1);

    sprintf(bigbuf,
        "%7.2f %7.2f scale             %%set image size in pts", wpt, hpt);
    sarrayAddString(sa, bigbuf, 1);

    if (spp == 1)
        sarrayAddString(sa, "/DeviceGray setcolorspace", 1);
    else if (spp == 3)
        sarrayAddString(sa, "/DeviceRGB setcolorspace", 1);
    else  /*spp == 4 */
        sarrayAddString(sa, "/DeviceCMYK setcolorspace", 1);
    
    sarrayAddString(sa, "{ << /ImageType 1", 1);
    sprintf(bigbuf, "     /Width %d", w);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf, "     /Height %d", h);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf, "     /ImageMatrix [ %d 0 0 %d 0 %d ]", w, -h, h);
    sarrayAddString(sa, bigbuf, 1);
    sarrayAddString(sa, "     /DataSource Data", 1);
    sprintf(bigbuf, "     /BitsPerComponent %d", bps);
    sarrayAddString(sa, bigbuf, 1);

    if (spp == 1)
        sarrayAddString(sa, "     /Decode [0 1]", 1);
    else if (spp == 3)
        sarrayAddString(sa, "     /Decode [0 1 0 1 0 1]", 1);
    else   /* spp == 4 */
        sarrayAddString(sa, "     /Decode [0 1 0 1 0 1 0 1]", 1);
    
    sarrayAddString(sa, "  >> image", 1);
    sarrayAddString(sa, "  Data closefile", 1);
    sarrayAddString(sa, "  RawData flushfile", 1);
    if (endpage == TRUE)
        sarrayAddString(sa, "  showpage", 1);
    sarrayAddString(sa, "  restore", 1);
    sarrayAddString(sa, "} exec", 1);

    if ((pstring = sarrayToString(sa, 1)) == NULL)
        return ERROR_INT("pstring not made", procName, 1);
    psbytes = strlen(pstring);

        /* Add the ascii85 data */
    totbytes = psbytes + nbytes85;
    *pnbytes = totbytes;
    if ((outstr = (char *)CALLOC(totbytes + 4, sizeof(char))) == NULL)
        return ERROR_INT("outstr not made", procName, 1);
    *poutstr = outstr;
    memcpy(outstr, pstring, psbytes);
    memcpy(outstr + psbytes, data85, nbytes85);

    sarrayDestroy(&sa);
    FREE(data85);
    FREE(pstring);
    return 0;
}


/*!
 *  extractJpegDataFromFile()
 *
 *      Input:  filein
 *              &data (binary data consisting of the entire jpeg file)
 *              &nbytes (size of binary data)
 *              &w (<return> image width)
 *              &h (<return> image height)
 *              &bps (<return> bits/sample; should be 8)
 *              &spp (<return> samples/pixel; should be 1 or 3)
 *      Return: 0 if OK, 1 on error
 */
l_int32
extractJpegDataFromFile(const char  *filein,
                        l_uint8    **pdata,
                        l_int32     *pnbytes,
                        l_int32     *pw,
                        l_int32     *ph,
                        l_int32     *pbps,
                        l_int32     *pspp)
{
l_uint8  *data;
l_int32   format, nbytes;
FILE     *fpin;

    PROCNAME("extractJpegDataFromFile");

    if (!filein)
        return ERROR_INT("filein not defined", procName, 1);
    if (!pdata)
        return ERROR_INT("&data not defined", procName, 1);
    if (!pnbytes)
        return ERROR_INT("&nbytes not defined", procName, 1);
    if (!pw || !ph || !pbps || !pspp)
        return ERROR_INT("&w, &h, &bps, &spp not all defined", procName, 1);
    *pdata = NULL;
    *pnbytes = *pw = *ph = *pbps = *pspp = 0;

    if ((fpin = fopen(filein, "r")) == NULL)
        return ERROR_INT("filein not defined", procName, 1);
    format = findFileFormat(fpin);
    fclose(fpin);
    if (format != IFF_JFIF_JPEG)
        return ERROR_INT("filein not jfif jpeg", procName, 1);

    if ((data = arrayRead(filein, &nbytes)) == NULL)
        return ERROR_INT("inarray not made", procName, 1);
    *pnbytes = nbytes;
    *pdata = data;

        /* On error, free the data */
    if (extractJpegDataFromArray(data, nbytes, pw, ph, pbps, pspp)) {
      FREE(data);
      *pdata = NULL;
      *pnbytes = 0;
    }

    return 0;
}


/*!
 *  extractJpegDataFromArray()
 *
 *      Input:  data (binary data consisting of the entire jpeg file)
 *              nbytes (size of binary data)
 *              &w (<return> image width)
 *              &h (<return> image height)
 *              &bps (<return> bits/sample; should be 8)
 *              &spp (<return> samples/pixel; should be 1 or 3)
 *      Return: 0 if OK, 1 on error
 */
l_int32
extractJpegDataFromArray(const void  *data,
                         l_int32      nbytes,
                         l_int32     *pw,
                         l_int32     *ph,
                         l_int32     *pbps,
                         l_int32     *pspp)
{
l_uint8  *data8;
l_int32   imeta, msize, bps, w, h, spp;

    PROCNAME("extractJpegDataFromArray");

    if (!data)
        return ERROR_INT("data not defined", procName, 1);
    if (!pw || !ph || !pbps || !pspp)
        return ERROR_INT("&w, &h, &bps, &spp not all defined", procName, 1);
    *pw = *ph = *pbps = *pspp = 0;
    data8 = (l_uint8 *)data;

        /* Find where the image metadata begins in header:
         * 0xc0 is start of metadata for baseline DCT;
         * 0xc1 is start of metadata for extended sequential DCT;
         * ...   */
    imeta = 0;
    if (locateJpegImageParameters(data8, nbytes, &imeta))
        return ERROR_INT("metadata not found", procName, 1);

        /* Save the metadata */
    msize = getTwoByteParameter(data8, imeta);   /* metadata size */
    bps = data8[imeta + 2];
    h = getTwoByteParameter(data8, imeta + 3);
    w = getTwoByteParameter(data8, imeta + 5);
    spp = data8[imeta + 7];
    *pbps = bps;
    *ph = h;
    *pw = w;
    *pspp = spp;

#if  DEBUG_JPEG
    fprintf(stderr, "w = %d, h = %d, bps = %d, spp = %d\n", w, h, bps, spp);
    fprintf(stderr, "imeta = %d, msize = %d\n", imeta, msize);
#endif   /* DEBUG_JPEG */
 
        /* Is the data obviously bad? */
    if (h <= 0 || w <= 0 || bps != 8 || (spp != 1 && spp !=3 && spp != 4)) {
        fprintf(stderr, "h = %d, w = %d, bps = %d, spp = %d\n", h, w, bps, spp);
        return ERROR_INT("image parameters not valid", procName, 1);
    }

    return 0;
}


/*
 *  locateJpegImageParameters()
 *  
 *      Input:  inarray (binary jpeg)
 *              size (of the data array)
 *             &index (<return> location of image metadata)
 *      Return: 0 if OK, 1 on error.  Caller must check this!
 *  
 *  Notes:
 *      (1) The parameters listed here appear to be the only jpeg flags
 *          we need to worry about.  It would have been nice to have
 *          avoided the switch with all these parameters, but
 *          unfortunately the parser for the jpeg header is set
 *          to accept any old flag that's not on the approved list!
 *          So we have to look for a flag that's not on the list
 *          (and is not 0), and then interpret the size of the
 *          data chunk and skip it.  Sometimes such a chunk contains
 *          a thumbnail version of the image, so if we don't skip it,
 *          we will find a pair of bytes such as 0xffc0, followed
 *          by small w and h dimensions. 
 */
static l_int32
locateJpegImageParameters(l_uint8  *inarray,
                          l_int32   size,
                          l_int32  *pindex)
{
l_uint8  val;
l_int32  index, skiplength;

    PROCNAME("locateJpegImageParameters");

    if (!inarray)
        return ERROR_INT("inarray not defined", procName, 1);
    if (!pindex)
        return ERROR_INT("&index not defined", procName, 1);

    index = *pindex;
    while (1) {
        if (getNextJpegMarker(inarray, size, &index))
            break;
        if ((val = inarray[index]) == 0)  /* ignore if "escaped" */
            continue;
/*        fprintf(stderr, " marker %x at %o, %d\n", val, index, index); */
        switch(val)
        {
        case 0xc0:  /* M_SOF0 */
        case 0xc1:  /* M_SOF1 */
        case 0xc2:  /* M_SOF2 */
        case 0xc3:  /* M_SOF3 */
        case 0xc5:  /* M_SOF5 */
        case 0xc6:  /* M_SOF6 */
        case 0xc7:  /* M_SOF7 */
        case 0xc9:  /* M_SOF9 */
        case 0xca:  /* M_SOF10 */
        case 0xcd:  /* M_SOF13 */
        case 0xce:  /* M_SOF14 */
        case 0xcf:  /* M_SOF15 */
            *pindex = index + 1;  /* found it */
            return 0;

        case 0x01:  /* M_TEM */
        case 0xd0:  /* M_RST0 */
        case 0xd1:  /* M_RST1 */
        case 0xd2:  /* M_RST2 */
        case 0xd3:  /* M_RST3 */
        case 0xd4:  /* M_RST4 */
        case 0xd5:  /* M_RST5 */
        case 0xd6:  /* M_RST6 */
        case 0xd7:  /* M_RST7 */
        case 0xd8:  /* M_SOI */
        case 0xd9:  /* M_EOI */
        case 0xe0:  /* M_APP0 */
        case 0xee:  /* M_APP14 */
            break;

        default:
            skiplength = getTwoByteParameter(inarray, index + 1);
            index += skiplength;
            break;
        }
    }

    return 1;  /* not found */
}


/*
 *  getNextJpegMarker()
 *
 *      Input:  array (jpeg data)
 *              size (from current point to the end)
 *             &index (<return> the last position searched.  If it
 *                     is not at the end of the array, we return
 *                     the first byte that is not 0xff, after
 *                     having encountered at least one 0xff.)
 *      Return: 0 if a marker is found, 1 if the end of the array is reached
 *      
 *  Notes:
 *      (1) In jpeg, 0xff is used to mark the end of a data segment.
 *          There may be more than one 0xff in succession.  But not every
 *          0xff marks the end of a segment.  It is possible, though
 *          rare, that 0xff can occur within some data.  In that case,
 *          the marker is "escaped", by following it with 0x00.
 *      (2) This function parses a jpeg data stream.  It doesn't
 *          _really_ get the next marker, because it doesn't check if
 *          the 0xff is escaped.  But the caller checks for this escape
 *          condition, and ignores the marker if escaped.
 */ 
static l_int32
getNextJpegMarker(l_uint8  *array,
                  l_int32   size,
                  l_int32  *pindex)
{
l_uint8  val;
l_int32  index;

    PROCNAME("getNextJpegMarker");

    if (!array)
        return ERROR_INT("array not defined", procName, 1);
    if (!pindex)
        return ERROR_INT("&index not defined", procName, 1);

    index = *pindex;

    while (index < size) {  /* skip to 0xff */
       val = array[index++];    
       if (val == 0xff)
           break;
    }

    while (index < size) {  /* skip repeated 0xff */
       val = array[index++];    
       if (val != 0xff)
           break;
    }

    *pindex = index - 1;
    if (index >= size)
        return 1;
    else
        return 0;
}


static l_int32
getTwoByteParameter(l_uint8  *array,
                    l_int32   index)
{
    return (l_int32)((array[index]) << 8) + (l_int32)(array[index + 1]);
}



/*-------------------------------------------------------------*
 *                  For tiff g4 compressed images              *
 *-------------------------------------------------------------*/
/*!
 *  convertTiffG4ToPSEmbed()
 *
 *      Input:  filein (input jpeg file)
 *              fileout (output ps file)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) This function takes a g4 compressed tif file as input and
 *          generates a g4 compressed, ascii85 encoded PS file, with
 *          a bounding box.
 *      (2) The bounding box is required when a program such as TeX
 *          (through epsf) places and rescales the image.
 *      (3) The bounding box is sized for fitting the image to an
 *          8.5 x 11.0 inch page.
 *      (4) We paint this through a mask, over whatever is below.
 */
l_int32
convertTiffG4ToPSEmbed(const char  *filein,
                       const char  *fileout)
{
char      *pstring, *pstring2, *outstr;
char      *data85;  /* ascii85 encoded ccitt g4 data */
char       bigbuf[512];
l_uint8   *bindata;  /* binary encoded ccitt g4 data */
l_int32    minisblack;   /* TRUE or FALSE */
l_int32    w, h;
l_int32    nbinbytes, nbytes85, psbytes, psbytes2, totbytes;
l_float32  xpt, ypt, wpt, hpt;
SARRAY    *sa, *sa2;

    PROCNAME("convertTiffG4ToPSEmbed");

    if (!filein)
        return ERROR_INT("filein not defined", procName, 1);
    if (!fileout)
        return ERROR_INT("fileout not defined", procName, 1);

        /* The returned ccitt g4 data in memory is the block of
         * bytes in the tiff file, starting after 8 bytes and
         * ending before the directory. */ 
    if (extractTiffG4DataFromFile(filein, &bindata, &nbinbytes,
                                  &w, &h, &minisblack))
        return ERROR_INT("bindata not extracted from file", procName, 1);

        /* Convert the ccittg4 encoded data to ascii85 */
    data85 = encodeAscii85(bindata, nbinbytes, &nbytes85);
    FREE(bindata);
    if (!data85)
        return ERROR_INT("data85 not made", procName, 1);

        /* Scale for 20 pt boundary and otherwise full filling
         * in one direction on 8.5 x 11 inch device */
    xpt = 20.0;
    ypt = 20.0;
    if (w * 11.0 > h * 8.5) {
        wpt = 572.0;   /* 612 - 2 * 20 */
        hpt = wpt * (l_float32)h / (l_float32)w;
    }
    else {
        hpt = 752.0;   /* 792 - 2 * 20 */
        wpt = hpt * (l_float32)w / (l_float32)h;
    }

        /*  -------- Generate PostScript output -------- */
    if ((sa = sarrayCreate(50)) == NULL)
        return ERROR_INT("sa not made", procName, 1);

    sarrayAddString(sa, "%!PS-Adobe-3.0", 1);
    sarrayAddString(sa, "%%Creator: leptonica", 1);
    sprintf(bigbuf, "%%%%Title: %s", filein);
    sarrayAddString(sa, bigbuf, 1);
    sarrayAddString(sa, "%%DocumentData: Clean7Bit", 1);
    sprintf(bigbuf,
        "%%%%BoundingBox: %7.2f %7.2f %7.2f %7.2f",
                xpt, ypt, xpt + wpt, ypt + hpt);
    sarrayAddString(sa, bigbuf, 1);

    sarrayAddString(sa, "%%LanguageLevel: 2", 1);
    sarrayAddString(sa, "%%EndComments", 1);
    sarrayAddString(sa, "%%Page: 1 1", 1);

    sarrayAddString(sa, "save", 1);
    sarrayAddString(sa, "100 dict begin", 1);

    sprintf(bigbuf,
        "%7.2f %7.2f translate         %%set image origin in pts", xpt, ypt);
    sarrayAddString(sa, bigbuf, 1);

    sprintf(bigbuf,
        "%7.2f %7.2f scale             %%set image size in pts", wpt, hpt);
    sarrayAddString(sa, bigbuf, 1);

    sarrayAddString(sa, "/DeviceGray setcolorspace", 1);

    sarrayAddString(sa, "{", 1);
    sarrayAddString(sa, "  /RawData currentfile /ASCII85Decode filter def", 1);
    sarrayAddString(sa, "  << ", 1);
    sarrayAddString(sa, "    /ImageType 1", 1);
    sprintf(bigbuf, "    /Width %d", w);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf, "    /Height %d", h);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf, "    /ImageMatrix [ %d 0 0 %d 0 %d ]", w, -h, h);
    sarrayAddString(sa, bigbuf, 1);
    sarrayAddString(sa, "    /BitsPerComponent 1", 1);
    sarrayAddString(sa, "    /Interpolate true", 1);
    if (minisblack)
        sarrayAddString(sa, "    /Decode [1 0]", 1);
    else  /* miniswhite; typical for 1 bpp */
        sarrayAddString(sa, "    /Decode [0 1]", 1);
    sarrayAddString(sa, "    /DataSource RawData", 1);
    sarrayAddString(sa, "        <<", 1);
    sarrayAddString(sa, "          /K -1", 1);
    sprintf(bigbuf, "          /Columns %d", w);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf, "          /Rows %d", h);
    sarrayAddString(sa, bigbuf, 1);
    sarrayAddString(sa, "        >> /CCITTFaxDecode filter", 1);
    sarrayAddString(sa, "  >> imagemask", 1);
    sarrayAddString(sa, "  RawData flushfile", 1);
    sarrayAddString(sa, "  showpage", 1);
    sarrayAddString(sa, "}", 1);

    sarrayAddString(sa, "%%BeginData:", 1);
    sarrayAddString(sa, "exec", 1);

    if ((pstring = sarrayToString(sa, 1)) == NULL)
        return ERROR_INT("pstring not made", procName, 1);
    psbytes = strlen(pstring);
    sarrayDestroy(&sa);

        /* Concat the trailing data */
    sa2 = sarrayCreate(10);
    sarrayAddString(sa2, "%%EndData", 1);
    sarrayAddString(sa2, "end", 1);
    sarrayAddString(sa2, "restore", 1);
    if ((pstring2 = sarrayToString(sa2, 1)) == NULL)
        return ERROR_INT("pstring2 not made", procName, 1);
    psbytes2 = strlen(pstring2);
    sarrayDestroy(&sa2);

        /* Add the ascii85 data */
    totbytes = psbytes + psbytes2 + nbytes85;
    if ((outstr = (char *)CALLOC(totbytes + 4, sizeof(char))) == NULL)
        return ERROR_INT("outstr not made", procName, 1);
    memcpy(outstr, pstring, psbytes);
    memcpy(outstr + psbytes, data85, nbytes85);
    memcpy(outstr + psbytes + nbytes85, pstring2, psbytes2);
    FREE(data85);
    FREE(pstring);
    FREE(pstring2);

    if (arrayWrite(fileout, "w", outstr, totbytes))
        return ERROR_INT("ps string not written to file", procName, 1);
    FREE(outstr);
    return 0;
}


/*!
 *  convertTiffG4ToPS()
 *
 *      Input:  filein (input tiff g4 file)
 *              fileout (output ps file)
 *              operation ("w" for write; "a" for append)
 *              x, y (location of LL corner of image, in pixels, relative
 *                    to the PostScript origin (0,0) at the LL corner
 *                    of the page)
 *              res (resolution of the input image, in ppi; typ. values
 *                   are 300 and 600; use 0 for automatic determination
 *                   based on image size)
 *              scale (scaling by printer; use 0.0 or 1.0 for no scaling)
 *              pageno (page number; must start with 1; you can use 0
 *                  if there is only one page.)
 *              mask (boolean: use TRUE if just painting through fg;
 *                    FALSE if painting both fg and bg.
 *              endpage (boolean: use TRUE if the last image to be
 *                  added to the page; FALSE otherwise)
 *      Return: 0 if OK, 1 on error
 *
 *  Usage:  See the usage comments in convertJpegToPS(), some of
 *          which are repeated here.
 *
 *          This is a wrapper for tiff g4.  The PostScript that
 *          is generated is expanded by about 5/4 (due to the
 *          ascii85 encoding.  If you convert to pdf (ps2pdf), the
 *          ascii85 decoder is automatically invoked, so that the
 *          pdf wrapped g4 file is essentially the same size as
 *          the original g4 file.  It's useful to have the PS
 *          file ascii85 encoded, because many printers will not
 *          print binary PS files.
 *
 *          For the first image written to a file, use "w", which
 *          opens for write and clears the file.  For all subsequent
 *          images written to that file, use "a".
 *
 *          To render multiple images on the same page, set
 *          endpage = FALSE for each image until you get to the
 *          last, for which you set endpage = TRUE.  This causes the
 *          "showpage" command to be invoked.  Showpage outputs
 *          the entire page and clears the raster buffer for the
 *          next page to be added.  Without a "showpage",
 *          subsequent images from the next page will overlay those
 *          previously put down.
 *
 *          For multiple images to the same page, where you are writing
 *          both jpeg and tiff-g4, you have two options:
 *
 *           (1) write the g4 first, as either image (mask == false)
 *               or imagemask (mask == true), and then write the
 *               jpeg over it.
 *
 *           (2) write the jpeg first and as the last item, write
 *               the g4 as an imagemask (mask == true), to paint
 *               through the foreground only.  
 *
 *          We have this flexibility with the tiff-g4 because it is 1 bpp.
 *
 *          For multiple pages, increment the page number, starting
 *          with page 1.  This allows PostScript (and PDF) to build
 *          a page directory, which viewers use for navigation.
 */
l_int32
convertTiffG4ToPS(const char  *filein,
                  const char  *fileout,
                  const char  *operation,
                  l_int32      x,
                  l_int32      y,
                  l_int32      res,
                  l_float32    scale,
                  l_int32      pageno,
                  l_int32      mask,
                  l_int32      endpage)
{
char    *outstr;
l_int32  nbytes;

    PROCNAME("convertTiffG4ToPS");

    if (!filein)
        return ERROR_INT("filein not defined", procName, 1);
    if (!fileout)
        return ERROR_INT("fileout not defined", procName, 1);
    if (strcmp(operation, "w") && strcmp(operation, "a"))
        return ERROR_INT("operation must be \"w\" or \"a\"", procName, 1);

    if (convertTiffG4ToPSString(filein, &outstr, &nbytes, x, y, res, scale,
                          pageno, mask, endpage))
        return ERROR_INT("ps string not made", procName, 1);

    if (arrayWrite(fileout, operation, outstr, nbytes))
        return ERROR_INT("ps string not written to file", procName, 1);

    FREE(outstr);
    return 0;
}


/*!
 *  convertTiffG4ToPSString()
 *
 *      Generates PS string in G4 compressed tiff format from G4 tiff file
 *
 *      Input:  filein (input tiff g4 file)
 *              &poutstr (<return> PS string)
 *              &nbytes (<return> number of bytes in PS string)
 *              x, y (location of LL corner of image, in pixels, relative
 *                    to the PostScript origin (0,0) at the LL corner
 *                    of the page)
 *              res (resolution of the input image, in ppi; typ. values
 *                   are 300 and 600; use 0 for automatic determination
 *                   based on image size)
 *              scale (scaling by printer; use 0.0 or 1.0 for no scaling)
 *              pageno (page number; must start with 1; you can use 0
 *                  if there is only one page.)
 *              mask (boolean: use TRUE if just painting through fg;
 *                    FALSE if painting both fg and bg.
 *              endpage (boolean: use TRUE if the last image to be
 *                  added to the page; FALSE otherwise)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) The returned PS character array is binary string, not a
 *          null-terminated C string.  It has null bytes embedded in it!
 *      (2) For usage, see convertTiffG4ToPS()
 */
l_int32
convertTiffG4ToPSString(const char  *filein,
                        char       **poutstr,
                        l_int32     *pnbytes,
                        l_int32      x,
                              l_int32      y,
                        l_int32      res,
                        l_float32    scale,
                        l_int32      pageno,
                        l_int32      mask,
                        l_int32      endpage)
{
char      *pstring, *pstring2, *outstr;
char      *data85;  /* ascii85 encoded ccitt g4 data */
char       bigbuf[L_BUF_SIZE];
l_uint8   *bindata;  /* binary encoded ccitt g4 data */
l_int32    minisblack;   /* TRUE or FALSE */
l_int32    w, h;
l_int32    nbinbytes, nbytes85, psbytes, psbytes2, totbytes;
l_float32  xpt, ypt, wpt, hpt;
SARRAY    *sa, *sa2;

    PROCNAME("convertTiffG4ToPSString");

    if (!filein)
        return ERROR_INT("filein not defined", procName, 1);
    if (!poutstr)
        return ERROR_INT("&outstr not defined", procName, 1);
    if (!pnbytes)
        return ERROR_INT("&nbytes not defined", procName, 1);
    *poutstr = NULL;

        /* The returned ccitt g4 data in memory is the block of
         * bytes in the tiff file, starting after 8 bytes and
         * ending before the directory. */ 
    if (extractTiffG4DataFromFile(filein, &bindata, &nbinbytes,
                                  &w, &h, &minisblack))
        return ERROR_INT("bindata not extracted from file", procName, 1);

#if  DEBUG_G4
/*    arrayWrite("junkarray", "w", bindata, nbinbytes); */
    fprintf(stderr, "nbinbytes = %d, w = %d, h = %d, minisblack = %d\n",
            nbinbytes, w, h, minisblack);
#endif   /* DEBUG_G4 */

        /* Convert the ccittg4 encoded data to ascii85 */
    data85 = encodeAscii85(bindata, nbinbytes, &nbytes85);
    FREE(bindata);
    if (!data85)
        return ERROR_INT("data85 not made", procName, 1);

        /* Get scaled location in pts */
    if (scale == 0.0)
        scale = 1.0;
    if (res == 0) {
        if (h <= 3300)
            res = 300;
        else
            res = 600;
    }
    xpt = scale * x * 72. / res;
    ypt = scale * y * 72. / res;
    wpt = scale * w * 72. / res;
    hpt = scale * h * 72. / res;

#if  DEBUG_G4
    fprintf(stderr, "xpt = %7.2f, ypt = %7.2f, wpt = %7.2f, hpt = %7.2f\n",
             xpt, ypt, wpt, hpt);
#endif   /* DEBUG_G4 */

        /*  -------- generate PostScript output -------- */
    if ((sa = sarrayCreate(50)) == NULL)
        return ERROR_INT("sa not made", procName, 1);

    sarrayAddString(sa, "%!PS-Adobe-3.0", 1);
    sarrayAddString(sa, "%%Creator: leptonica", 1);
    sprintf(bigbuf, "%%%%Title: %s", filein);
    sarrayAddString(sa, bigbuf, 1);
    sarrayAddString(sa, "%%DocumentData: Clean7Bit", 1);

#if  PRINT_BOUNDING_BOX
    sprintf(bigbuf,
        "%%%%BoundingBox: %7.2f %7.2f %7.2f %7.2f",
                xpt, ypt, xpt + wpt, ypt + hpt);
    sarrayAddString(sa, bigbuf, 1);
#endif  /* PRINT_BOUNDING_BOX */

    sarrayAddString(sa, "%%LanguageLevel: 2", 1);
    sarrayAddString(sa, "%%EndComments", 1);
    sprintf(bigbuf, "%%%%Page: %d %d", pageno, pageno);
    sarrayAddString(sa, bigbuf, 1);

    sarrayAddString(sa, "save", 1);
    sarrayAddString(sa, "100 dict begin", 1);

    sprintf(bigbuf,
        "%7.2f %7.2f translate         %%set image origin in pts", xpt, ypt);
    sarrayAddString(sa, bigbuf, 1);

    sprintf(bigbuf,
        "%7.2f %7.2f scale             %%set image size in pts", wpt, hpt);
    sarrayAddString(sa, bigbuf, 1);

    sarrayAddString(sa, "/DeviceGray setcolorspace", 1);

    sarrayAddString(sa, "{", 1);
    sarrayAddString(sa, "  /RawData currentfile /ASCII85Decode filter def", 1);
    sarrayAddString(sa, "  << ", 1);
    sarrayAddString(sa, "    /ImageType 1", 1);
    sprintf(bigbuf, "    /Width %d", w);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf, "    /Height %d", h);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf, "    /ImageMatrix [ %d 0 0 %d 0 %d ]", w, -h, h);
    sarrayAddString(sa, bigbuf, 1);
    sarrayAddString(sa, "    /BitsPerComponent 1", 1);
    sarrayAddString(sa, "    /Interpolate true", 1);
    if (minisblack)
        sarrayAddString(sa, "    /Decode [1 0]", 1);
    else  /* miniswhite; typical for 1 bpp */
        sarrayAddString(sa, "    /Decode [0 1]", 1);
    sarrayAddString(sa, "    /DataSource RawData", 1);
    sarrayAddString(sa, "        <<", 1);
    sarrayAddString(sa, "          /K -1", 1);
    sprintf(bigbuf, "          /Columns %d", w);
    sarrayAddString(sa, bigbuf, 1);
    sprintf(bigbuf, "          /Rows %d", h);
    sarrayAddString(sa, bigbuf, 1);
    sarrayAddString(sa, "        >> /CCITTFaxDecode filter", 1);
    if (mask == TRUE)  /* just paint through the fg */
        sarrayAddString(sa, "  >> imagemask", 1);
    else  /* paint full image */
        sarrayAddString(sa, "  >> image", 1);
    sarrayAddString(sa, "  RawData flushfile", 1);
    if (endpage == TRUE)
        sarrayAddString(sa, "  showpage", 1);
    sarrayAddString(sa, "}", 1);

    sarrayAddString(sa, "%%BeginData:", 1);
    sarrayAddString(sa, "exec", 1);

    if ((pstring = sarrayToString(sa, 1)) == NULL)
        return ERROR_INT("pstring not made", procName, 1);
    psbytes = strlen(pstring);

        /* Concat the trailing data */
    sa2 = sarrayCreate(10);
    sarrayAddString(sa2, "%%EndData", 1);
    sarrayAddString(sa2, "end", 1);
    sarrayAddString(sa2, "restore", 1);
    if ((pstring2 = sarrayToString(sa2, 1)) == NULL)
        return ERROR_INT("pstring2 not made", procName, 1);
    psbytes2 = strlen(pstring2);

        /* Add the ascii85 data */
    totbytes = psbytes + psbytes2 + nbytes85;
    *pnbytes = totbytes;
    if ((outstr = (char *)CALLOC(totbytes + 4, sizeof(char))) == NULL)
        return ERROR_INT("outstr not made", procName, 1);
    *poutstr = outstr;
    memcpy(outstr, pstring, psbytes);
    memcpy(outstr + psbytes, data85, nbytes85);
    memcpy(outstr + psbytes + nbytes85, pstring2, psbytes2);

    sarrayDestroy(&sa);
    sarrayDestroy(&sa2);
    FREE(data85);
    FREE(pstring);
    FREE(pstring2);
    return 0;
}


/*!
 *  extractTiffG4DataFromFile()
 *
 *      Input:  filein
 *              &data (binary data of ccitt g4 encoded stream)
 *              &nbytes (size of binary data)
 *              &w (image width)
 *              &h (image height)
 *              &minisblack (boolean, but must use l_uint16)
 *      Return: 0 if OK, 1 on error
 */
l_int32
extractTiffG4DataFromFile(const char  *filein,
                          l_uint8    **pdata,
                          l_int32     *pnbytes,
                          l_int32     *pw,
                          l_int32     *ph,
                          l_int32     *pminisblack)
{
l_uint8  *inarray, *data;
l_uint16  minisblack, comptype;  /* accessors require l_uint16 */
l_int32   format, fbytes, nbytes;
l_uint32  w, h, rowsperstrip;  /* accessors require l_uint32 */
l_uint32  diroff;
FILE     *fpin;
TIFF     *tif;

    PROCNAME("extractTiffG4DataFromFile");

    if (!pdata)
        return ERROR_INT("&data not defined", procName, 1);
    if (!pnbytes || !pw || !ph || !pminisblack)
        return ERROR_INT("&nbytes, &w, &h, &minisblack not all defined",
                         procName, 1);
    *pdata = NULL;
    *pnbytes = *pw = *ph = *pminisblack = 0;

    if ((fpin = fopen(filein, "r")) == NULL)
        return ERROR_INT("filein not defined", procName, 1);
    format = findFileFormat(fpin);
    fclose(fpin);
    if (format != IFF_TIFF)
        return ERROR_INT("filein not tiff", procName, 1);

    if ((inarray = arrayRead(filein, &fbytes)) == NULL)
        return ERROR_INT("inarray not made", procName, 1);

        /* Get metadata about the image */
    if ((tif = TIFFOpen(filein, "r")) == NULL)
        return ERROR_INT("tif not open for read", procName, 1);
    TIFFGetField(tif, TIFFTAG_COMPRESSION, &comptype);
    if (comptype != COMPRESSION_CCITTFAX4) {
        FREE(inarray);
        TIFFClose(tif);
        return ERROR_INT("filein is not g4 compressed", procName, 1);
    }

    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
    TIFFGetField(tif, TIFFTAG_ROWSPERSTRIP, &rowsperstrip);
    if (h != rowsperstrip)
        L_WARNING("more than 1 strip", procName);
    TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &minisblack);  /* for 1 bpp */
/*    TIFFPrintDirectory(tif, stderr, 0); */
    TIFFClose(tif);
    *pw = (l_int32)w;
    *ph = (l_int32)h;
    *pminisblack = (l_int32)minisblack;

        /* The header has 8 bytes: the first 2 are the magic number,
         * the next 2 are the version, and the last 4 are the
         * offset to the first directory.  That's what we want here.
         * We have to test the byte order before decoding 4 bytes! */
    if (inarray[0] == 0x4d) {  /* big-endian */
        diroff = (inarray[4] << 24) | (inarray[5] << 16) |
                 (inarray[6] << 8) | inarray[7];
    }
    else  {   /* inarray[0] == 0x49 :  little-endian */
        diroff = (inarray[7] << 24) | (inarray[6] << 16) |
                 (inarray[5] << 8) | inarray[4];
    }
/*    fprintf(stderr, " diroff = %d, %x\n", diroff, diroff); */

        /* Extract the ccittg4 encoded data from the tiff file.
         * We skip the 8 byte header and take nbytes of data,
         * up to the beginning of the directory (at diroff)  */
    nbytes = diroff - 8;
    *pnbytes = nbytes;
    if ((data = (l_uint8 *)CALLOC(nbytes, sizeof(l_uint8))) == NULL) {
        FREE(inarray);
        return ERROR_INT("data not allocated", procName, 1);
    }
    *pdata = data;
    memcpy(data, inarray + 8, nbytes);
    FREE(inarray);

    return 0;
}


/*-------------------------------------------------------------*
 *                     For tiff multipage files                *
 *-------------------------------------------------------------*/
/*!
 *  convertTiffMultipageToPS()
 *
 *      Input:  filein (input tiff multipage file)
 *              fileout (output ps file)
 *              tempfile (<optional> for temporary g4 tiffs;
 *                        use NULL for default)
 *              factor (for filling 8.5 x 11 inch page;
 *                      use 0.0 for DEFAULT_FILL_FRACTION)
 *      Return: 0 if OK, 1 on error
 *      
 *  Notes:
 *      (1) This converts a multipage tiff file of binary page images
 *          into a ccitt g4 compressed PS file.
 *      (2) If the images are generated from a standard resolution fax,
 *          the vertical resolution is doubled to give a normal-looking
 *          aspect ratio.
 */
l_int32
convertTiffMultipageToPS(const char  *filein,
                         const char  *fileout,
                         const char  *tempfile,
                         l_float32    fillfract)
{
const char   tempdefault[] = "/usr/tmp/junk_temp_g4.tif";
const char  *tempname;
l_int32      i, npages, w, h;
l_float32    scale;
PIX         *pix, *pixs;
FILE        *fp;

    PROCNAME("convertTiffMultipageToPS");

    if (!filein)
        return ERROR_INT("filein not defined", procName, 1);
    if (!fileout)
        return ERROR_INT("fileout not defined", procName, 1);

    if ((fp = fopen(filein, "r")) == NULL)
        return ERROR_INT("file not found", procName, 1);
    if (findFileFormat(fp) != IFF_TIFF)
        return ERROR_INT("file not tiff format", procName, 1);
    tiffGetCount(fp, &npages);
    fclose(fp);

    if (tempfile)
        tempname = tempfile;
    else
        tempname = tempdefault;

    if (fillfract == 0.0)
        fillfract = DEFAULT_FILL_FRACTION;

    for (i = 0; i < npages; i++) {
        if ((pix = pixReadTiff(filein, i)) == NULL)
             return ERROR_INT("pix not made", procName, 1);

        w = pixGetWidth(pix);
        h = pixGetHeight(pix);
        if (w == 1728 && h < w)   /* it's a std res fax */
            pixs = pixScale(pix, 1.0, 2.0);
        else
            pixs = pixClone(pix);

        pixWrite(tempname, pixs, IFF_TIFF_G4);
        scale = L_MIN(fillfract * 2550 / w, fillfract * 3300 / h);
        if (i == 0)
            convertTiffG4ToPS(tempname, fileout, "w", 0, 0, 300, scale,
                              i + 1, FALSE, TRUE);
        else
            convertTiffG4ToPS(tempname, fileout, "a", 0, 0, 300, scale,
                              i + 1, FALSE, TRUE);
        pixDestroy(&pix);
        pixDestroy(&pixs);
    }

    return 0;
}


/*-------------------------------------------------------------*
 *                    Converting resolution                    *
 *-------------------------------------------------------------*/
/*!
 *  getResLetterPage()
 *
 *      Input:  w (image width, pixels)
 *              h (image height, pixels)
 *              fillfract (fraction in linear dimension of full page, not
 *                         to be exceeded; use 0 for default)
 *      Return: 0 if OK, 1 on error
 */
l_int32
getResLetterPage(l_int32    w,
                 l_int32    h,
                 l_float32  fillfract)
{
l_int32  resw, resh, res;

    if (fillfract == 0.0)
        fillfract = DEFAULT_FILL_FRACTION;
    resw = (l_int32)((w * 72.) / (LETTER_WIDTH * fillfract));
    resh = (l_int32)((h * 72.) / (LETTER_HEIGHT * fillfract));
    res = L_MAX(resw, resh);
    return res;
}


/*!
 *  getResA4Page()
 *
 *      Input:  w (image width, pixels)
 *              h (image height, pixels)
 *              fillfract (fraction in linear dimension of full page, not
 *                        to be exceeded; use 0 for default)
 *      Return: 0 if OK, 1 on error
 */
l_int32
getResA4Page(l_int32    w,
             l_int32    h,
             l_float32  fillfract)
{
l_int32  resw, resh, res;

    if (fillfract == 0.0)
        fillfract = DEFAULT_FILL_FRACTION;
    resw = (l_int32)((w * 72.) / (A4_WIDTH * fillfract));
    resh = (l_int32)((h * 72.) / (A4_HEIGHT * fillfract));
    res = L_MAX(resw, resh);
    return res;
}



/*-------------------------------------------------------------*
 *      Utility for encoding and decoding data with ascii85    *
 *-------------------------------------------------------------*/
/*!
 *  encodeAscii85()
 *
 *      Input:  inarray (input data)
 *              insize (number of bytes in input array)
 *              &outsize (<return> number of bytes in output char array)
 *      Return: chara (with 64 characters + \n in each line)
 *
 *  Notes:
 *      (1) Ghostscript has a stack break if the last line of
 *          data only has a '>', so we avoid the problem by
 *          always putting '~>' on the last line.
 */
char *
encodeAscii85(l_uint8  *inarray,
              l_int32   insize,
              l_int32  *poutsize)
{
char    *chara;
char    *outbuf;
l_int32  maxsize, i, index, outindex, linecount, nbout, eof;

    PROCNAME("encodeAscii85");

    if (!inarray)
        return (char *)ERROR_PTR("inarray not defined", procName, NULL);

        /* Accumulate results in chara */
    maxsize = (l_int32)(80. + (insize * 5. / 4.) *
                        (1. + 2. / MAX_85_LINE_COUNT));
    if ((chara = (char *)CALLOC(maxsize, sizeof(char))) == NULL)
        return (char *)ERROR_PTR("chara not made", procName, NULL);

    if ((outbuf = (char *)CALLOC(8, sizeof(char))) == NULL)
        return (char *)ERROR_PTR("outbuf not made", procName, NULL);

    linecount = 0;
    index = 0;
    outindex = 0;
    while (1) {
        eof = convertChunkToAscii85(inarray, insize, &index, outbuf, &nbout);
        for (i = 0; i < nbout; i++) {
            chara[outindex++] = outbuf[i];
            linecount++;
            if (linecount >= MAX_85_LINE_COUNT) {
                chara[outindex++] = '\n';
                linecount = 0;
            }
        }
        if (eof == TRUE) {
            if (linecount != 0)
                chara[outindex++] = '\n';
            chara[outindex++] = '~';
            chara[outindex++] = '>';
            chara[outindex++] = '\n';
            break;
        }
    }
    
    FREE(outbuf);
    *poutsize = outindex;
    return chara;
}


/*!
 *  convertChunkToAscii85()
 *
 *      Input:  inarray (input data)
 *              insize  (number of bytes in input array)
 *              &index (use and <return> -- ptr)
 *              outbuf (holds 8 ascii chars; we use no more than 7)
 *              &nbsout (<return> number of bytes written to outbuf)
 *      Return: boolean for eof (0 if more data, 1 if end of file)
 *    
 *  Notes:
 *      (1) Attempts to read 4 bytes and write 5.
 *      (2) Writes 1 byte if the value is 0; writes 2 extra bytes if EOF.
 */
l_int32
convertChunkToAscii85(l_uint8  *inarray,
                      l_int32   insize,
                      l_int32  *pindex,
                      char     *outbuf,
                      l_int32  *pnbout)
{
l_uint8   inbyte;
l_uint32  inword, val;
l_int32   eof, index, nread, nbout, i;

    eof = FALSE;
    index = *pindex;
    nread = L_MIN(4, (insize - index));
    if (insize == index + nread)
        eof = TRUE;
    *pindex += nread;  /* save new index */

        /* Read input data and save in l_uint32 */
    inword = 0;
    for (i = 0; i < nread; i++) {
        inbyte = inarray[index + i];
        inword += inbyte << (8 * (3 - i));
    }

#if 0
    fprintf(stderr, "index = %d, nread = %d\n", index, nread);
    fprintf(stderr, "inword = %x\n", inword);
    fprintf(stderr, "eof = %d\n", eof);
#endif
    
        /* Special case: output 1 byte only */
    if (inword == 0) {
        outbuf[0] = 'z';
        nbout = 1;
    }
    else { /* output nread + 1 bytes */
        for (i = 4; i >= 4 - nread; i--) {
            val = inword / power85[i];
            outbuf[4 - i] = (l_uint8)(val + '!');
            inword -= val * power85[i];
        }
        nbout = nread + 1;
    }
    *pnbout = nbout;

    return eof;
}


/*!
 *  decodeAscii85()
 *
 *      Input:  inarray (ascii85 input data)
 *              insize (number of bytes in input array)
 *              &outsize (<return> number of bytes in output l_uint8 array)
 *      Return: outarray (binary)
 *
 *  Notes:
 *      (1) We assume the data is properly encoded, so we do not check
 *          for invalid characters or the final '>' character.
 *      (2) We permit whitespace to be added to the encoding in an
 *          arbitrary way.
 */
l_uint8 *
decodeAscii85(char     *ina,
              l_int32   insize,
              l_int32  *poutsize)
{
char      inc;
char     *pin;
l_uint8   val;
l_uint8  *outa;
l_int32   maxsize, ocount, bytecount, index;
l_uint32  oword;

    PROCNAME("decodeAscii85");

    if (!ina)
        return (l_uint8 *)ERROR_PTR("ina not defined", procName, NULL);

        /* Accumulate results in outa */
    maxsize = (l_int32)(80. + (insize * 4. / 5.));  /* plenty big */
    if ((outa = (l_uint8 *)CALLOC(maxsize, sizeof(l_uint8))) == NULL)
        return (l_uint8 *)ERROR_PTR("outa not made", procName, NULL);

    pin = ina;
    ocount = 0;  /* byte index into outa */
    oword = 0;
    for (index = 0, bytecount = 0; index < insize; index++, pin++) {
        inc = *pin;

        if (inc == ' ' || inc == '\t' || inc == '\n' ||
            inc == '\f' || inc == '\r' || inc == '\v')  /* ignore white space */
            continue;

        val = inc - '!';
        if (val < 85) {
            oword = oword * 85 + val;
            if (bytecount < 4)
                bytecount++;
            else {  /* we have all 5 input chars for the oword */
                outa[ocount] = (oword >> 24) & 0xff;
                outa[ocount + 1] = (oword >> 16) & 0xff;
                outa[ocount + 2] = (oword >> 8) & 0xff;
                outa[ocount + 3] = oword & 0xff;
                ocount += 4;
                bytecount = 0;
                oword = 0;
            }
        }
        else if (inc == 'z' && bytecount == 0) {
            outa[ocount] = 0;
            outa[ocount + 1] = 0;
            outa[ocount + 2] = 0;
            outa[ocount + 3] = 0;
            ocount += 4;
        }
        else if (inc == '~') {  /* end of data */
            fprintf(stderr, " %d extra bytes output\n", bytecount - 1);
            switch (bytecount) {
            case 0:   /* normal eof */
            case 1:   /* error */
                break;
            case 2:   /* 1 extra byte */
                oword = oword * (85 * 85 * 85) + 0xffffff;
                outa[ocount] = (oword >> 24) & 0xff; 
                break;
            case 3:   /* 2 extra bytes */
                oword = oword * (85 * 85) + 0xffff;
                outa[ocount] = (oword >> 24) & 0xff; 
                outa[ocount + 1] = (oword >> 16) & 0xff; 
                break;
            case 4:   /* 3 extra bytes */
                oword = oword * 85 + 0xff;
                outa[ocount] = (oword >> 24) & 0xff; 
                outa[ocount + 1] = (oword >> 16) & 0xff; 
                outa[ocount + 2] = (oword >> 8) & 0xff; 
                break;
            }
            if (bytecount > 1)
                ocount += (bytecount - 1);
            break;
        }
    }
    *poutsize = ocount;

    return outa;
}

