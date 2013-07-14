/*
Copyright 2013 Jay Sorg

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

This is the main driver file

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* this should be before all X11 .h files */
#include <xorg-server.h>

/* all driver need this */
#include <xf86.h>
#include <xf86_OSproc.h>

#include <mipointer.h>
#include <fb.h>
#include <micmap.h>
#include <mi.h>

#include "rdp.h"
#include "rdpPri.h"
#include "rdpDraw.h"
#include "rdpGC.h"
#include "rdpCursor.h"

#define XRDP_DRIVER_NAME "XRDPDEV"
#define XRDP_NAME "XRDPDEV"
#define XRDP_VERSION 1000

#define PACKAGE_VERSION_MAJOR 1
#define PACKAGE_VERSION_MINOR 0
#define PACKAGE_VERSION_PATCHLEVEL 0

#define LLOG_LEVEL 1
#define LLOGLN(_level, _args) \
  do \
  { \
    if (_level < LLOG_LEVEL) \
    { \
      ErrorF _args ; \
      ErrorF("\n"); \
    } \
  } \
  while (0)

int g_bpp = 32;
int g_depth = 24;
int g_rgb_bits = 8;
int g_redOffset = 16;
int g_redBits = 8;
int g_greenOffset = 8;
int g_greenBits = 8;
int g_blueOffset = 0;
int g_blueBits = 8;

static int g_setup_done = 0;

/* Supported "chipsets" */
static SymTabRec g_Chipsets[] =
{
    { 0, XRDP_DRIVER_NAME },
    { -1, 0 }
};

static XF86ModuleVersionInfo g_VersRec =
{
    XRDP_DRIVER_NAME,
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR,
    PACKAGE_VERSION_MINOR,
    PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
    0,
    { 0, 0, 0, 0 }
};

/*****************************************************************************/
static Bool
rdpAllocRec(ScrnInfoPtr pScrn)
{
    LLOGLN(10, ("rdpAllocRec:"));
    if (pScrn->driverPrivate != 0)
    {
        return 1;
    }
    pScrn->driverPrivate = xnfcalloc(sizeof(rdpRec), 1);
    return 1;
}

/*****************************************************************************/
static void
rdpFreeRec(ScrnInfoPtr pScrn)
{
    LLOGLN(10, ("rdpFreeRec:"));
    if (pScrn->driverPrivate == 0)
    {
        return;
    }
    free(pScrn->driverPrivate);
    pScrn->driverPrivate = 0;
}

/*****************************************************************************/
static Bool
rdpPreInit(ScrnInfoPtr pScrn, int flags)
{
    rgb zeros1;
    Gamma zeros2;
    int got_res_match;
    char **modename;
    DisplayModePtr mode;
    rdpPtr dev;

    LLOGLN(0, ("rdpPreInit:"));
    if (flags & PROBE_DETECT)
    {
        return 0;
    }
    if (pScrn->numEntities != 1)
    {
        return 0;
    }

    rdpPrivateInit();

    rdpAllocRec(pScrn);
    dev = XRDPPTR(pScrn);

    dev->width = 1024;
    dev->height = 768;

    pScrn->monitor = pScrn->confScreen->monitor;
    pScrn->bitsPerPixel = g_bpp;
    pScrn->virtualX = dev->width;
    pScrn->displayWidth = dev->width;
    pScrn->virtualY = dev->height;
    pScrn->progClock = 1;
    pScrn->rgbBits = g_rgb_bits;
    pScrn->depth = g_depth;
    pScrn->chipset = XRDP_DRIVER_NAME;
    pScrn->currentMode = pScrn->modes;
    pScrn->offset.blue = g_blueOffset;
    pScrn->offset.green = g_greenOffset;
    pScrn->offset.red = g_redOffset;
    pScrn->mask.blue = ((1 << g_blueBits) - 1) << pScrn->offset.blue;
    pScrn->mask.green = ((1 << g_greenBits) - 1) << pScrn->offset.green;
    pScrn->mask.red = ((1 << g_redBits) - 1) << pScrn->offset.red;

    if (!xf86SetDepthBpp(pScrn, g_depth, g_bpp, g_bpp,
                         Support24bppFb | Support32bppFb |
                         SupportConvert32to24 | SupportConvert24to32))
    {
        LLOGLN(0, ("rdpPreInit: xf86SetDepthBpp failed"));
        rdpFreeRec(pScrn);
        return 0;
    }
    xf86PrintDepthBpp(pScrn);
    memset(&zeros1, 0, sizeof(zeros1));
    if (!xf86SetWeight(pScrn, zeros1, zeros1))
    {
        LLOGLN(0, ("rdpPreInit: xf86SetWeight failed"));
        rdpFreeRec(pScrn);
        return 0;
    }
    memset(&zeros2, 0, sizeof(zeros2));
    if (!xf86SetGamma(pScrn, zeros2))
    {
        LLOGLN(0, ("rdpPreInit: xf86SetGamma failed"));
        rdpFreeRec(pScrn);
        return 0;
    }
    if (!xf86SetDefaultVisual(pScrn, -1))
    {
        LLOGLN(0, ("rdpPreInit: xf86SetDefaultVisual failed"));
        rdpFreeRec(pScrn);
        return 0;
    }
    xf86SetDpi(pScrn, 0, 0);
    if (0 == pScrn->display->modes)
    {
        LLOGLN(0, ("rdpPreInit: modes error"));
        rdpFreeRec(pScrn);
        return 0;
    }

    pScrn->virtualX = pScrn->display->virtualX;
    pScrn->virtualY = pScrn->display->virtualY;

    got_res_match = 0;
    for (modename = pScrn->display->modes; *modename != 0; modename++)
    {
        for (mode = pScrn->monitor->Modes; mode != 0; mode = mode->next)
        {
            LLOGLN(10, ("%s %s", mode->name, *modename));
            if (0 == strcmp(mode->name, *modename))
            {
                break;
            }
        }
        if (0 == mode)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "\tmode \"%s\" not found\n",
                       *modename);
            continue;
        }
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "\tmode \"%s\" ok\n", *modename);
        LLOGLN(10, ("%d %d %d %d", mode->HDisplay, dev->width,
               mode->VDisplay, dev->height));
        if ((mode->HDisplay == dev->width) && (mode->VDisplay == dev->height))
        {
            pScrn->virtualX = mode->HDisplay;
            pScrn->virtualY = mode->VDisplay;
            got_res_match = 1;
        }
        if (got_res_match)
        {
            pScrn->modes = xf86DuplicateMode(mode);
            pScrn->modes->next = pScrn->modes;
            pScrn->modes->prev = pScrn->modes;
            dev->num_modes = 1;
            break;
        }
    }
    pScrn->currentMode = pScrn->modes;
    xf86PrintModes(pScrn);
    LLOGLN(10, ("rdpPreInit: out fPtr->num_modes %d", dev->num_modes));
    if (!got_res_match)
    {
        LLOGLN(0, ("rdpPreInit: could not find screen resolution %dx%d",
               dev->width, dev->height));
        return 0;
    }
    return 1;
}

static miPointerSpriteFuncRec g_rdpSpritePointerFuncs =
{
    /* these are in rdpCursor.c */
    rdpSpriteRealizeCursor,
    rdpSpriteUnrealizeCursor,
    rdpSpriteSetCursor,
    rdpSpriteMoveCursor,
    rdpSpriteDeviceCursorInitialize,
    rdpSpriteDeviceCursorCleanup
};

/******************************************************************************/
static Bool
rdpSaveScreen(ScreenPtr pScreen, int on)
{
    LLOGLN(0, ("rdpSaveScreen:"));
    return 1;
}

/*****************************************************************************/
static Bool
rdpScreenInit(int scrnIndex, ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr pScrn;
    rdpPtr dev;
    VisualPtr vis;
    int vis_found;

    pScrn = xf86Screens[scrnIndex];
    dev = XRDPPTR(pScrn);

    dev->pScreen = pScreen;

    miClearVisualTypes();
    miSetVisualTypes(pScrn->depth, miGetDefaultVisualMask(pScrn->depth),
                    pScrn->rgbBits, TrueColor);
    miSetPixmapDepths();
    LLOGLN(0, ("rdpScreenInit: virtualX %d virtualY %d",
           pScrn->virtualX, pScrn->virtualY));
    dev->ptr = malloc(dev->width * dev->height * 4);
    if (!fbScreenInit(pScreen, dev->ptr, pScrn->virtualX, pScrn->virtualY,
                      pScrn->xDpi, pScrn->yDpi, pScrn->displayWidth,
                      pScrn->bitsPerPixel))
    {
        LLOGLN(0, ("rdpScreenInit: fbScreenInit failed"));
        return 0;
    }
    miInitializeBackingStore(pScreen);

#if 0
    /* XVideo */
    if (rdp_xv_init(pScreen, pScrn) != 0)
    {
        LLOGLN(0, ("rdpScreenInit: rdp_xv_init failed"));
    }
#endif

    vis = pScreen->visuals + (pScreen->numVisuals - 1);
    while (vis >= pScreen->visuals)
    {
        if ((vis->class | DynamicClass) == DirectColor)
        {
            vis->offsetBlue = pScrn->offset.blue;
            vis->blueMask = pScrn->mask.blue;
            vis->offsetGreen = pScrn->offset.green;
            vis->greenMask = pScrn->mask.green;
            vis->offsetRed = pScrn->offset.red;
            vis->redMask = pScrn->mask.red;
        }
        vis--;
    }
    fbPictureInit(pScreen, 0, 0);
    xf86SetBlackWhitePixels(pScreen);
    xf86SetBackingStore(pScreen);

#if 1
    /* hardware cursor */
    dev->pCursorFuncs = xf86GetPointerScreenFuncs();
    miPointerInitialize(pScreen, &g_rdpSpritePointerFuncs,
                        dev->pCursorFuncs, 0);
#else
    /* software cursor */
    dev->pCursorFuncs = xf86GetPointerScreenFuncs();
    miDCInitialize(pScreen, dev->pCursorFuncs);
#endif

    fbCreateDefColormap(pScreen);

    /* must assign this one */
    pScreen->SaveScreen = rdpSaveScreen;

    vis_found = 0;
    vis = pScreen->visuals + (pScreen->numVisuals - 1);
    while (vis >= pScreen->visuals)
    {
        if (vis->vid == pScreen->rootVisual)
        {
            vis_found = 1;
        }
        vis--;
    }
    if (!vis_found)
    {
        LLOGLN(0, ("rdpScreenInit: no root visual"));
        return 0;
    }

    dev->privateKeyRecGC = rdpAllocateGCPrivate(pScreen, sizeof(rdpGCRec));
    dev->privateKeyRecPixmap = rdpAllocatePixmapPrivate(pScreen, sizeof(rdpPixmapRec));

    dev->CopyWindow = pScreen->CopyWindow;
    pScreen->CopyWindow = rdpCopyWindow;

    dev->CreateGC = pScreen->CreateGC;
    pScreen->CreateGC = rdpCreateGC;

    dev->CreatePixmap = pScreen->CreatePixmap;
    pScreen->CreatePixmap = rdpCreatePixmap;

    dev->DestroyPixmap = pScreen->DestroyPixmap;
    pScreen->DestroyPixmap = rdpDestroyPixmap;

    dev->ModifyPixmapHeader = pScreen->ModifyPixmapHeader;
    pScreen->ModifyPixmapHeader = rdpModifyPixmapHeader;

    LLOGLN(0, ("rdpScreenInit: out"));
    return 1;
}

/*****************************************************************************/
static Bool
rdpSwitchMode(int a, DisplayModePtr b, int c)
{
    LLOGLN(0, ("rdpSwitchMode:"));
    return 1;
}

/*****************************************************************************/
static void
rdpAdjustFrame(int a, int b, int c, int d)
{
    LLOGLN(10, ("rdpAdjustFrame:"));
}

/*****************************************************************************/
static Bool
rdpEnterVT(int a, int b)
{
    LLOGLN(0, ("rdpEnterVT:"));
    return 1;
}

/*****************************************************************************/
static void
rdpLeaveVT(int a, int b)
{
    LLOGLN(0, ("rdpLeaveVT:"));
}

/*****************************************************************************/
static ModeStatus
rdpValidMode(int a, DisplayModePtr b, Bool c, int d)
{
    LLOGLN(0, ("rdpValidMode:"));
    return 0;
}

/*****************************************************************************/
static Bool
rdpProbe(DriverPtr drv, int flags)
{
    int num_dev_sections;
    int i;
    int entity;
    GDevPtr *dev_sections;
    Bool found_screen;
    ScrnInfoPtr pscrn;

    LLOGLN(0, ("rdpProbe:"));
    if (flags & PROBE_DETECT)
    {
        return 0;
    }
    /* fbScreenInit, fbPictureInit, ... */
    if (!xf86LoadDrvSubModule(drv, "fb"))
    {
        LLOGLN(0, ("rdpProbe: xf86LoadDrvSubModule for fb failed"));
        return 0;
    }

    num_dev_sections = xf86MatchDevice(XRDP_DRIVER_NAME, &dev_sections);
    if (num_dev_sections <= 0)
    {
        LLOGLN(0, ("rdpProbe: xf86MatchDevice failed"));
        return 0;
    }

    pscrn = 0;
    found_screen = 0;
    for (i = 0; i < num_dev_sections; i++)
    {
        entity = xf86ClaimFbSlot(drv, 0, dev_sections[i], 1);
        pscrn = xf86ConfigFbEntity(pscrn, 0, entity, 0, 0, 0, 0);
        if (pscrn)
        {
            LLOGLN(10, ("rdpProbe: found screen"));
            found_screen = 1;
            pscrn->driverVersion = XRDP_VERSION;
            pscrn->driverName    = XRDP_DRIVER_NAME;
            pscrn->name          = XRDP_NAME;
            pscrn->Probe         = rdpProbe;
            pscrn->PreInit       = rdpPreInit;
            pscrn->ScreenInit    = rdpScreenInit;
            pscrn->SwitchMode    = rdpSwitchMode;
            pscrn->AdjustFrame   = rdpAdjustFrame;
            pscrn->EnterVT       = rdpEnterVT;
            pscrn->LeaveVT       = rdpLeaveVT;
            pscrn->ValidMode     = rdpValidMode;

            xf86DrvMsg(pscrn->scrnIndex, X_INFO, "%s", "using default device\n");
        }
    }
    free(dev_sections);
    return found_screen;
}

/*****************************************************************************/
static const OptionInfoRec *
rdpAvailableOptions(int chipid, int busid)
{
    LLOGLN(0, ("rdpAvailableOptions:"));
    return 0;
}

/*****************************************************************************/
static Bool
rdpDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr)
{
    xorgHWFlags *flags;
    int rv;

    rv = 0;
    LLOGLN(0, ("rdpDriverFunc: op %d", (int)op));
    if (op == GET_REQUIRED_HW_INTERFACES)
    {
        flags = (xorgHWFlags *) ptr;
        *flags = HW_SKIP_CONSOLE;
        rv = 1;
    }
    return rv;
}

/*****************************************************************************/
static void
rdpIdentify(int flags)
{
    LLOGLN(0, ("rdpIdentify:"));
    xf86PrintChipsets(XRDP_NAME, "driver for xrdp", g_Chipsets);
}

_X_EXPORT DriverRec g_DriverRec =
{
    XRDP_VERSION,
    XRDP_DRIVER_NAME,
    rdpIdentify,
    rdpProbe,
    rdpAvailableOptions,
    0,
    0,
    rdpDriverFunc
};

/*****************************************************************************/
static pointer
xrdpdevSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    LLOGLN(0, ("xrdpdevSetup:"));
    if (!g_setup_done)
    {
        g_setup_done = 1;
        xf86AddDriver(&g_DriverRec, module, HaveDriverFuncs);
        return (pointer)1;
    }
    else
    {
        if (errmaj != 0)
        {
            *errmaj = LDR_ONCEONLY;
        }
        return 0;
    }
}

/* <drivername>ModuleData */
_X_EXPORT XF86ModuleData xrdpdevModuleData =
{
    &g_VersRec,
    xrdpdevSetup,
    0
};