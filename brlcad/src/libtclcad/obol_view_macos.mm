/*             O B O L _ V I E W _ M A C O S . M M
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/**
 * @file libtclcad/obol_view_macos.mm
 *
 * macOS NSOpenGL hardware rendering backend for the obol_view Tcl/Tk widget.
 *
 * This file is compiled as Objective-C++ (Clang) so that it can use
 * NSOpenGLContext, NSOpenGLView, and related Cocoa GL APIs.  It is only
 * compiled when BRLCAD_ENABLE_AQUA is ON and is linked into libtclcad
 * alongside obol_view.cpp.
 *
 * AGL (Apple GL, Carbon era) is NOT used — it was deprecated in macOS 10.9
 * and its type definitions were removed in macOS 14.  The correct modern
 * replacement for GL rendering embedded in a Cocoa window is NSOpenGLView
 * plus NSOpenGLContext, as implemented here.
 *
 * NSOpenGLView/NSOpenGLContext are themselves deprecated since macOS 10.14
 * (Mojave) in favour of Metal, but remain available and functional on all
 * macOS versions through at least macOS 15 (Sequoia) as of 2025.  Since
 * Obol/Coin3D requires OpenGL, this is the correct path for hardware-
 * accelerated Obol rendering on macOS.
 *
 * Architecture:
 *
 *   An ObolNSGLView (NSOpenGLView subclass) is created at widget-init time
 *   and added as a positioned subview of the Tk window's NSWindow contentView.
 *   The view covers exactly the area occupied by the Tk frame widget, computed
 *   from Tk_GetRootCoords() + NSScreen height conversion.
 *
 *   On resize (Tk Configure event) obol_nsgl_resize() re-queries the widget's
 *   screen position and updates the NSOpenGLView frame so it stays aligned
 *   with the Tk frame, even when the Tk container is resized or repositioned.
 *
 *   Rendering is driven externally by obol_view.cpp (SoRenderManager) — this
 *   file provides only the context lifecycle (make-current, flush, destroy).
 *
 * Thread safety:
 *   All calls must be made on the main thread (Tcl/Tk UI thread).
 *
 * See RADICAL_MIGRATION.md Stage 6 and brlcad/misc/Notes/aqua_tk_notes.md.
 */

#ifdef BRLCAD_ENABLE_OBOL

/* Suppress deprecation warnings for NSOpenGL* on macOS 10.14+ */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#import <Cocoa/Cocoa.h>
#import <OpenGL/gl.h>
#import <OpenGL/OpenGL.h>    /* CGLContext, CGLFlushDrawable */

#include "common.h"

extern "C" {
#include <tcl.h>
#ifdef HAVE_TK
#  include <tk.h>
#endif
#include "bu/log.h"
}

/*
 * Tk_MacOSXGetNSWindowForDrawable is declared in <tk.h> / tkDecls.h under
 * #if defined(MAC_OSX_TK) when building with Aqua Tk.  Provide an explicit
 * extern declaration as a fallback in case the installed tk.h does not
 * define MAC_OSX_TK (e.g. during cross-compilation or bext transition).
 */
#if !defined(MAC_OSX_TK)
extern "C" void *Tk_MacOSXGetNSWindowForDrawable(unsigned long drawable);
#endif


/* ======================================================================== *
 * ObolNSGLView — lightweight NSOpenGLView subclass                         *
 * ======================================================================== */

@interface ObolNSGLView : NSOpenGLView

/* drawRect is intentionally empty: all rendering is driven externally by the
 * SoRenderManager in obol_view.cpp via obol_nsgl_make_current / swap_buffers. */
- (void)drawRect:(NSRect)dirtyRect;

/* We do not want this view to steal keyboard focus from Tk widgets. */
- (BOOL)acceptsFirstResponder;

@end

@implementation ObolNSGLView

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    /* Rendering driven externally — do nothing here. */
}

- (BOOL)acceptsFirstResponder
{
    return NO;
}

@end


/* ======================================================================== *
 * Internal coordinate helpers                                               *
 * ======================================================================== */

/*
 * obol_tk_to_nsrect:
 *
 * Convert a Tk widget's screen position + size into an NSRect in the
 * coordinate system of the given NSWindow's contentView.
 *
 * Tk (X11 emulation on macOS) uses screen coordinates with the top-left
 * origin (0,0).  Cocoa uses screen coordinates with the bottom-left origin.
 * Additionally NSWindow frame coordinates include the title bar, so we work
 * in the content-rect coordinate system throughout.
 *
 * Parameters:
 *   nswin        NSWindow* that hosts the Tk widget
 *   rx, ry       Root-window (screen) pixel coords of the Tk widget's
 *                top-left corner, from Tk_GetRootCoords()
 *   width,height Tk widget dimensions in pixels
 *
 * Returns the NSRect for use with [NSView setFrame:].
 */
static NSRect
obol_tk_to_nsrect(NSWindow *nswin, int rx, int ry, int width, int height)
{
    /* Logical screen height (Cocoa, points, bottom-left = 0,0) */
    NSScreen *screen = [nswin screen];
    if (!screen) screen = [NSScreen mainScreen];
    CGFloat screen_h = [screen frame].size.height;

    /* Content rect of the NSWindow in screen coords (Cocoa convention). */
    NSRect content = [nswin contentRectForFrameRect:[nswin frame]];

    /* Flip the Tk Y coordinate: Tk (0,0) is top-left; Cocoa (0,0) is
     * bottom-left.  The Tk ry is the top of the widget from the Tk origin;
     * we want the bottom of the widget in Cocoa screen coords.            */
    CGFloat cocoa_screen_bottom = screen_h - (CGFloat)(ry + height);

    /* Convert from screen to content-view-local coordinates. */
    CGFloat view_x = (CGFloat)rx         - content.origin.x;
    CGFloat view_y = cocoa_screen_bottom - content.origin.y;

    return NSMakeRect(view_x, view_y, (CGFloat)width, (CGFloat)height);
}


/* ======================================================================== *
 * Public bridge functions (called from obol_view.cpp via extern "C")       *
 * ======================================================================== */

extern "C" bool
obol_nsgl_init(Tk_Window tkwin, int width, int height,
	       void **view_out, void **cgl_ctx_out)
{
    /* Realise the Tk window so Tk_WindowId() returns a valid Drawable. */
    Tk_MakeWindowExist(tkwin);

    /* Get the NSWindow hosting this Tk window via the public Tk API.     */
    NSWindow *nswin = (NSWindow *)
	Tk_MacOSXGetNSWindowForDrawable(Tk_WindowId(tkwin));
    if (!nswin) {
	bu_log("obol_view(NSOpenGL): Tk_MacOSXGetNSWindowForDrawable returned "
	       "nil — Aqua Tk may not be fully initialised yet\n");
	return false;
    }

    /* Pixel format: hardware-accelerated, RGBA, double-buffered, depth-24. */
    NSOpenGLPixelFormatAttribute attrs[] = {
	NSOpenGLPFADoubleBuffer,
	NSOpenGLPFAAccelerated,
	NSOpenGLPFADepthSize,  (NSOpenGLPixelFormatAttribute)24,
	NSOpenGLPFAColorSize,  (NSOpenGLPixelFormatAttribute)24,
	NSOpenGLPFAAlphaSize,  (NSOpenGLPixelFormatAttribute)8,
	(NSOpenGLPixelFormatAttribute)0
    };
    NSOpenGLPixelFormat *pf =
	[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];

    if (!pf) {
	/* Retry without NSOpenGLPFAAccelerated (may be a software renderer). */
	NSOpenGLPixelFormatAttribute attrs_sw[] = {
	    NSOpenGLPFADoubleBuffer,
	    NSOpenGLPFADepthSize, (NSOpenGLPixelFormatAttribute)24,
	    (NSOpenGLPixelFormatAttribute)0
	};
	pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs_sw];
    }
    if (!pf) {
	bu_log("obol_view(NSOpenGL): NSOpenGLPixelFormat creation failed\n");
	return false;
    }

    /* Determine the initial frame in the NSWindow's contentView coords.  */
    int rx = 0, ry = 0;
    Tk_GetRootCoords(tkwin, &rx, &ry);
    NSRect frame = obol_tk_to_nsrect(nswin, rx, ry, width, height);

    ObolNSGLView *glview =
	[[ObolNSGLView alloc] initWithFrame:frame pixelFormat:pf];
    [pf release];
    if (!glview) {
	bu_log("obol_view(NSOpenGL): ObolNSGLView alloc/initWithFrame failed\n");
	return false;
    }

    /* Use logical pixels (not Retina physical pixels) for simplicity.
     * Set to YES for HiDPI rendering (requires corresponding GL viewport
     * scaling in the render path).                                       */
    [glview setWantsBestResolutionOpenGLSurface:NO];

    /* Place the GL view BELOW any existing Tk subviews (e.g. button bars)
     * so that Tk widgets remain interactive on top of the GL canvas.     */
    [[nswin contentView] addSubview:glview
			 positioned:NSWindowBelow
			 relativeTo:nil];
    [glview release];   /* retained by the superview */

    NSOpenGLContext *ctx = [glview openGLContext];
    if (!ctx) {
	bu_log("obol_view(NSOpenGL): openGLContext is nil\n");
	[[nswin contentView] willRemoveSubview:glview];
	[glview removeFromSuperview];
	return false;
    }

    /* Return opaque handles: ARC-retained view pointer + raw CGLContextObj
     * (used as SoRenderManager cache context tag in obol_view.cpp).      */
    *view_out    = (__bridge_retained void*)glview;
    *cgl_ctx_out = (void*)([ctx CGLContextObj]);

    return true;
}


extern "C" void
obol_nsgl_make_current(void *view_raw)
{
    if (!view_raw) return;
    ObolNSGLView *glview = (__bridge ObolNSGLView*)view_raw;
    [[glview openGLContext] makeCurrentContext];
}


extern "C" void
obol_nsgl_swap_buffers(void *view_raw)
{
    if (!view_raw) return;
    ObolNSGLView *glview = (__bridge ObolNSGLView*)view_raw;
    /* CGLFlushDrawable is preferred over [ctx flushBuffer] for performance
     * (avoids implicit makeCurrentContext call inside flushBuffer).       */
    CGLFlushDrawable([[glview openGLContext] CGLContextObj]);
}


extern "C" void
obol_nsgl_clear_current(void)
{
    [NSOpenGLContext clearCurrentContext];
}


extern "C" void
obol_nsgl_resize(void *view_raw, Tk_Window tkwin, int width, int height)
{
    if (!view_raw || !tkwin) return;
    ObolNSGLView *glview = (__bridge ObolNSGLView*)view_raw;
    NSWindow *nswin = [glview window];
    if (!nswin) return;

    /* Re-query the widget's position so we handle both resize AND reposition
     * (e.g. pane splitter drag, window move).                             */
    int rx = 0, ry = 0;
    Tk_GetRootCoords(tkwin, &rx, &ry);
    NSRect frame = obol_tk_to_nsrect(nswin, rx, ry, width, height);

    [glview setFrame:frame];

    /* Notify the GL context that the drawable surface has changed.        */
    [[glview openGLContext] update];
}


extern "C" void
obol_nsgl_destroy(void *view_raw)
{
    if (!view_raw) return;
    /* Transfer ownership back to ARC so the retain added by
     * __bridge_retained in obol_nsgl_init() is released.               */
    ObolNSGLView *glview = (__bridge_transfer ObolNSGLView*)view_raw;
    [NSOpenGLContext clearCurrentContext];
    [glview removeFromSuperview];
    glview = nil;   /* ARC releases on nil assignment */
}


#pragma clang diagnostic pop

#endif /* BRLCAD_ENABLE_OBOL */

/*
 * Local Variables:
 * mode: ObjC
 * tab-width: 8
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
