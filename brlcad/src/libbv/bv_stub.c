/*                       B V _ S T U B . C
 * BRL-CAD
 *
 * Copyright (c) 2004-2025 United States Government as represented by
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
 * @file bv_stub.c
 *
 * @brief libbv backward-compatibility stub.
 *
 * All implementation has moved to libbsg (BSG modernization Step 7).
 * This translation unit exists solely to satisfy the build system's
 * requirement for at least one source file.  The library itself just
 * re-exports every symbol from libbsg.
 */

/* intentionally minimal */
/* ISO C requires at least one external declaration */
extern int bv_stub_unused;
int bv_stub_unused = 0;

/* Reference a symbol from libbsg to ensure the dynamic linker records
 * libbsg.so as a NEEDED dependency of libbv.so, so that code linking
 * against -lbv continues to find all bv_* symbols at runtime. */
struct bsg_scene;
extern void bsg_scene_init(struct bsg_scene *);
void (*bv_libbsg_anchor)(struct bsg_scene *) = bsg_scene_init;
