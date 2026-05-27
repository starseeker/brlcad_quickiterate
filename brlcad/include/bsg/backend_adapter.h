/*                B A C K E N D _ A D A P T E R . H
 * BRL-CAD
 *
 * Copyright (c) 2026 United States Government as represented by
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
/** @addtogroup libbsg
 *
 * @brief
 * Backend adapter interface — the contract between BSG render execution
 * and a concrete drawing backend (Phase D5).
 *
 * A backend (libdm GL, swrast, qtgl, or a future Obol renderer) fills in
 * a `bsg_backend_adapter` and attaches it to a `bsg_render_request` via
 * the `adapter` field.  During `bsg_render_request_execute` the traversal
 * calls the adapter callbacks once per resolved `bsg_render_item` rather
 * than calling `bsg_payload_dispatch` directly.
 *
 * The adapter owns all backend-side state; libbsg never reads or frees it.
 * `dmp` is the same opaque display-manager handle stored in the render
 * request and is forwarded to each callback.
 *
 * Null function pointers are silently skipped; a backend only needs to
 * implement the callbacks it uses.
 *
 * Typical lifetime per frame:
 *   1. Caller attaches an initialised bsg_backend_adapter* to the request.
 *   2. bsg_render_request_execute calls prepare() → draw() per item.
 *   3. When geometry changes the caller calls invalidate() per affected item.
 *   4. When a node is deleted the caller calls free() to release GPU resources.
 */
/** @{ */
/* @file bsg/backend_adapter.h */

#ifndef BSG_BACKEND_ADAPTER_H
#define BSG_BACKEND_ADAPTER_H

#include "common.h"
#include "bsg/defines.h"
#include "bsg/render_item.h"

__BEGIN_DECLS

/* -----------------------------------------------------------------------
 * Capability flags returned by bsg_backend_adapter::capabilities()
 * ----------------------------------------------------------------------- */

/** Backend supports per-object transparency (alpha blending). */
#define BSG_ADAPTER_CAP_TRANSPARENCY  0x01u

/** Backend can render wireframe display modes. */
#define BSG_ADAPTER_CAP_WIREFRAME     0x02u

/** Backend can render shaded (BoT/mesh) display modes. */
#define BSG_ADAPTER_CAP_SHADED        0x04u

/** Backend can render HUD / faceplate overlay items. */
#define BSG_ADAPTER_CAP_HUD           0x08u

/** Backend supports GPU-side back-to-front sort for transparent items. */
#define BSG_ADAPTER_CAP_SORTED_ALPHA  0x10u

/** Backend supports BSG_PAYLOAD_BREP (NURBS surface) payloads. */
#define BSG_ADAPTER_CAP_BREP          0x20u


/* -----------------------------------------------------------------------
 * Adapter struct
 * ----------------------------------------------------------------------- */

/**
 * Backend adapter — a vtable of callbacks that a concrete drawing backend
 * implements to consume BSG render items.
 *
 * All callback pointers may be NULL; missing callbacks are silently skipped.
 * The `dmp` argument to each callback is the display-manager handle from the
 * owning `bsg_render_request`.
 */
struct bsg_backend_adapter {

    /**
     * Prepare backend resources for @p item before the first draw call.
     *
     * Called by `bsg_render_request_execute` for each item that is about to
     * be drawn.  Backends use this to allocate GPU buffers, compile shaders,
     * or upload mesh data.  May be NULL when no per-item preparation is
     * needed.
     *
     * Returns non-zero on success, 0 on failure (the item will still be
     * passed to draw() — the backend decides how to handle a prepare failure).
     */
    int (*prepare)(void *dmp, const struct bsg_render_item *item);

    /**
     * Issue the draw call for @p item.
     *
     * Called after prepare() (when set) for each item in phase order.
     * The draw call must not modify @p item.  May be NULL (drawing
     * is silently skipped for this backend).
     */
    void (*draw)(void *dmp, const struct bsg_render_item *item);

    /**
     * Mark cached backend resources for @p item as stale.
     *
     * Called when a node's geometry or appearance has changed and the backend
     * needs to re-upload data on the next draw.  May be NULL.
     */
    void (*invalidate)(void *dmp, const struct bsg_render_item *item);

    /**
     * Release all backend resources associated with @p item.
     *
     * Called when a node is removed from the scene.  The adapter must not
     * access @p item->node after this call.  May be NULL.
     */
    void (*free)(void *dmp, const struct bsg_render_item *item);

    /**
     * Return the set of `BSG_ADAPTER_CAP_*` flags this backend supports.
     *
     * May be NULL (treated as returning 0 — no declared capabilities).
     * `bsg_render_request_execute` does not consult capabilities itself;
     * callers may use them to choose between adapters or skip unsupported
     * render passes.
     */
    unsigned int (*capabilities)(void *dmp);
};

__END_DECLS

#endif /* BSG_BACKEND_ADAPTER_H */

/** @} */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
