/*                L E G A C Y _ C O M P A T . H
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
/** @file bsg/legacy_compat.h
 *
 * Compatibility aliases for legacy BV_* names.
 *
 * New code should use BSG_* names directly.  This header is enabled by
 * default; define BSG_ENABLE_LEGACY_BV_ALIASES to 0 before including BSG
 * headers to disable the aliases in new code.
 */

#ifndef BSG_LEGACY_COMPAT_H
#define BSG_LEGACY_COMPAT_H

#if !defined(BSG_ENABLE_LEGACY_BV_ALIASES)
#  define BSG_ENABLE_LEGACY_BV_ALIASES 1
#endif

#if BSG_ENABLE_LEGACY_BV_ALIASES

#ifndef BV_EXPORT
#  define BV_EXPORT BSG_EXPORT
#endif

#ifdef BSG_VIEW_MAX
#  ifndef BV_MAX
#    define BV_MAX BSG_VIEW_MAX
#  endif
#  ifndef BV_MIN
#    define BV_MIN BSG_VIEW_MIN
#  endif
#  ifndef BV_RANGE
#    define BV_RANGE BSG_VIEW_RANGE
#  endif
#endif

#ifdef BSG_MINVIEWSIZE
#  ifndef BV_MINVIEWSIZE
#    define BV_MINVIEWSIZE BSG_MINVIEWSIZE
#  endif
#  ifndef BV_MINVIEWSCALE
#    define BV_MINVIEWSCALE BSG_MINVIEWSCALE
#  endif
#endif

#ifdef BSG_ANCHOR_AUTO
#  ifndef BV_ANCHOR_AUTO
#    define BV_ANCHOR_AUTO BSG_ANCHOR_AUTO
#    define BV_ANCHOR_BOTTOM_LEFT BSG_ANCHOR_BOTTOM_LEFT
#    define BV_ANCHOR_BOTTOM_CENTER BSG_ANCHOR_BOTTOM_CENTER
#    define BV_ANCHOR_BOTTOM_RIGHT BSG_ANCHOR_BOTTOM_RIGHT
#    define BV_ANCHOR_MIDDLE_LEFT BSG_ANCHOR_MIDDLE_LEFT
#    define BV_ANCHOR_MIDDLE_CENTER BSG_ANCHOR_MIDDLE_CENTER
#    define BV_ANCHOR_MIDDLE_RIGHT BSG_ANCHOR_MIDDLE_RIGHT
#    define BV_ANCHOR_TOP_LEFT BSG_ANCHOR_TOP_LEFT
#    define BV_ANCHOR_TOP_CENTER BSG_ANCHOR_TOP_CENTER
#    define BV_ANCHOR_TOP_RIGHT BSG_ANCHOR_TOP_RIGHT
#  endif
#endif

#ifdef BSG_OBJ_SETTINGS_INIT
#  ifndef BV_OBJ_SETTINGS_INIT
#    define BV_OBJ_SETTINGS_INIT BSG_OBJ_SETTINGS_INIT
#  endif
#endif

#ifdef BSG_SHAPE_DBOBJ
#  ifndef BV_DBOBJ_BASED
#    define BV_DBOBJ_BASED BSG_SHAPE_DBOBJ
#    define BV_VIEWONLY BSG_SHAPE_VIEWONLY
#    define BV_LINES BSG_SHAPE_LINES
#    define BV_LABELS BSG_SHAPE_LABELS
#    define BV_AXES BSG_SHAPE_AXES
#    define BV_POLYGONS BSG_SHAPE_POLYGONS
#  endif
#endif

#ifdef BSG_OBJ_DB
#  ifndef BV_DB_OBJS
#    define BV_DB_OBJS BSG_OBJ_DB
#    define BV_VIEW_OBJS BSG_OBJ_VIEW
#    define BV_LOCAL_OBJS BSG_OBJ_LOCAL
#    define BV_CHILD_OBJS BSG_OBJ_CHILD
#  endif
#endif

#ifdef BSG_BACKEND_NONE
#  ifndef BV_BACKEND_NONE
#    define BV_BACKEND_NONE BSG_BACKEND_NONE
#    define BV_BACKEND_GL BSG_BACKEND_GL
#  endif
#endif

#ifdef BSG_SNAP_SHARED
#  ifndef BV_SNAP_SHARED
#    define BV_SNAP_SHARED BSG_SNAP_SHARED
#    define BV_SNAP_LOCAL BSG_SNAP_LOCAL
#    define BV_SNAP_DB BSG_SNAP_DB
#    define BV_SNAP_VIEW BSG_SNAP_VIEW
#    define BV_SNAP_TCL BSG_SNAP_TCL
#  endif
#endif

#ifdef BSG_POLYGON_GENERAL
#  ifndef BV_POLYGON_GENERAL
#    define BV_POLYGON_GENERAL BSG_POLYGON_GENERAL
#    define BV_POLYGON_CIRCLE BSG_POLYGON_CIRCLE
#    define BV_POLYGON_ELLIPSE BSG_POLYGON_ELLIPSE
#    define BV_POLYGON_RECTANGLE BSG_POLYGON_RECTANGLE
#    define BV_POLYGON_SQUARE BSG_POLYGON_SQUARE
#  endif
#endif

#ifdef BSG_POLYGON_UPDATE_DEFAULT
#  ifndef BV_POLYGON_UPDATE_DEFAULT
#    define BV_POLYGON_UPDATE_DEFAULT BSG_POLYGON_UPDATE_DEFAULT
#    define BV_POLYGON_UPDATE_PROPS_ONLY BSG_POLYGON_UPDATE_PROPS_ONLY
#    define BV_POLYGON_UPDATE_PT_SELECT BSG_POLYGON_UPDATE_PT_SELECT
#    define BV_POLYGON_UPDATE_PT_SELECT_CLEAR BSG_POLYGON_UPDATE_PT_SELECT_CLEAR
#    define BV_POLYGON_UPDATE_PT_MOVE BSG_POLYGON_UPDATE_PT_MOVE
#    define BV_POLYGON_UPDATE_PT_APPEND BSG_POLYGON_UPDATE_PT_APPEND
#  endif
#endif

#ifdef BSG_POLY_CIRCLE_MODE
#  ifndef BV_POLY_CIRCLE_MODE
#    define BV_POLY_CIRCLE_MODE BSG_POLY_CIRCLE_MODE
#    define BV_POLY_CONTOUR_MODE BSG_POLY_CONTOUR_MODE
#  endif
#endif

#ifdef BSG_AUTOVIEW_SCALE_DEFAULT
#  ifndef BV_AUTOVIEW_SCALE_DEFAULT
#    define BV_AUTOVIEW_SCALE_DEFAULT BSG_AUTOVIEW_SCALE_DEFAULT
#  endif
#endif

#ifdef BSG_KNOBS_ALL
#  ifndef BV_KNOBS_ALL
#    define BV_KNOBS_ALL BSG_KNOBS_ALL
#    define BV_KNOBS_RATE BSG_KNOBS_RATE
#    define BV_KNOBS_ABS BSG_KNOBS_ABS
#  endif
#endif

#ifdef BSG_IDLE
#  ifndef BV_IDLE
#    define BV_IDLE BSG_IDLE
#    define BV_ROT BSG_ROT
#    define BV_TRANS BSG_TRANS
#    define BV_SCALE BSG_SCALE
#    define BV_CENTER BSG_CENTER
#    define BV_CON_X BSG_CON_X
#    define BV_CON_Y BSG_CON_Y
#    define BV_CON_Z BSG_CON_Z
#    define BV_CON_GRID BSG_CON_GRID
#    define BV_CON_LINES BSG_CON_LINES
#  endif
#endif

#ifdef BSG_VIEW_OBJ_OPTS_INIT
#  ifndef BV_VIEW_OBJ_OPTS_INIT
#    define BV_VIEW_OBJ_OPTS_INIT BSG_VIEW_OBJ_OPTS_INIT
#  endif
#endif

#ifdef BSG_VIEW_OBJ_SCOPE_SHARED
#  ifndef BV_VIEW_OBJ_SCOPE_SHARED
#    define BV_VIEW_OBJ_SCOPE_SHARED BSG_VIEW_OBJ_SCOPE_SHARED
#    define BV_VIEW_OBJ_SCOPE_LOCAL BSG_VIEW_OBJ_SCOPE_LOCAL
#    define BV_VIEW_OBJ_SCOPE_ALL BSG_VIEW_OBJ_SCOPE_ALL
#  endif
#endif

#ifdef BSG_ENABLE_ENV_LOGGING
#  ifndef BV_ENABLE_ENV_LOGGING
#    define BV_ENABLE_ENV_LOGGING BSG_ENABLE_ENV_LOGGING
#  endif
#endif

#ifdef BSG_KEY_PRESS
#  ifndef BV_KEY_PRESS
#    define BV_KEY_PRESS BSG_KEY_PRESS
#    define BV_KEY_RELEASE BSG_KEY_RELEASE
#    define BV_LEFT_MOUSE_PRESS BSG_LEFT_MOUSE_PRESS
#    define BV_LEFT_MOUSE_RELEASE BSG_LEFT_MOUSE_RELEASE
#    define BV_RIGHT_MOUSE_PRESS BSG_RIGHT_MOUSE_PRESS
#    define BV_RIGHT_MOUSE_RELEASE BSG_RIGHT_MOUSE_RELEASE
#    define BV_MIDDLE_MOUSE_PRESS BSG_MIDDLE_MOUSE_PRESS
#    define BV_MIDDLE_MOUSE_RELEASE BSG_MIDDLE_MOUSE_RELEASE
#    define BV_CTRL_MOD BSG_CTRL_MOD
#    define BV_SHIFT_MOD BSG_SHIFT_MOD
#    define BV_ALT_MOD BSG_ALT_MOD
#  endif
#endif

#endif /* BSG_ENABLE_LEGACY_BV_ALIASES */

#endif /* BSG_LEGACY_COMPAT_H */
