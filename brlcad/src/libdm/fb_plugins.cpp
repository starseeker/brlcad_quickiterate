/*                  F B _ P L U G I N S . C P P
 * BRL-CAD
 *
 * Copyright (c) 2020-2025 United States Government as represented by
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
/** @file fb_plugins.cpp
 *
 * Framebuffer plugin API entry points (fb_open, fb_set_interface, etc.).
 * These use only the fb_backends map and have no dependency on the dm plugin
 * machinery, making them safe to compile in all build configurations including
 * BRLCAD_ENABLE_OBOL builds where no dm rendering plugins are present.
 *
 */

#include "common.h"

#include <cstring>
#include <map>
#include <string>
#include <cstdio>

#include "bu/log.h"
#include "bu/str.h"

#include "dm.h"
#include "./include/private.h"


extern "C" int
fb_set_interface(struct fb *ifp, const char *interface_type)
{
    if (!ifp)
	return 0;

    std::map<std::string, const struct fb *> *fmb = (std::map<std::string, const struct fb *> *)fb_backends;
    std::map<std::string, const struct fb *>::iterator f_it;

    for (f_it = fmb->begin(); f_it != fmb->end(); f_it++) {
	const struct fb *f = f_it->second;
        if (bu_strncmp(interface_type, f->i->if_name+5, strlen(interface_type)) == 0) {
	    /* found it, copy its struct in */
            *ifp->i = *(f->i);
            return 1;
        }
    }

    return 0;
}


extern "C" struct fb_platform_specific *
fb_get_platform_specific(uint32_t magic)
{
    std::map<std::string, const struct fb *> *fmb = (std::map<std::string, const struct fb *> *)fb_backends;
    std::map<std::string, const struct fb *>::iterator f_it;

    for (f_it = fmb->begin(); f_it != fmb->end(); f_it++) {
	const struct fb *f = f_it->second;
        if (magic == f->i->type_magic) {
            /* found it, get its specific struct */
            return f->i->if_existing_get(magic);
        }
    }

    return NULL;
}


extern "C" void
fb_put_platform_specific(struct fb_platform_specific *fb_p)
{
    if (!fb_p) return;

    std::map<std::string, const struct fb *> *fmb = (std::map<std::string, const struct fb *> *)fb_backends;
    std::map<std::string, const struct fb *>::iterator f_it;

    for (f_it = fmb->begin(); f_it != fmb->end(); f_it++) {
	const struct fb *f = f_it->second;
	if (fb_p->magic == f->i->type_magic) {
	    f->i->if_existing_put(fb_p);
	    return;
	}
    }

    return;
}


#define Malloc_Bomb(_bytes_)                                    \
    fb_log("\"%s\"(%d) : allocation of %zu bytes failed.\n",    \
           __FILE__, __LINE__, _bytes_)

/**
 * True if the non-null string s is all digits
 */
static int
fb_totally_numeric(const char *s)
{
    if (!s || s[0] == '\0')
        return 0;

    while (*s) {
        if (*s < '0' || *s > '9')
            return 0;
        s++;
    }

    return 1;
}


struct fb *
fb_open(const char *file, int width, int height)
{
    struct fb *ifp;
    int i = 0;
    const char *b;

    std::map<std::string, const struct fb *> *fmb = (std::map<std::string, const struct fb *> *)fb_backends;
    std::map<std::string, const struct fb *>::iterator f_it;

    if (width < 0 || height < 0)
        return FB_NULL;

    ifp = (struct fb *)calloc(sizeof(struct fb), 1);
    if (ifp == FB_NULL) {
        Malloc_Bomb(sizeof(struct fb));
        return FB_NULL;
    }
    ifp->i = (struct fb_impl *)calloc(sizeof(struct fb_impl), 1);
    if (file == NULL || *file == '\0') {
        /* No name given, check environment variable first.     */
	file = (const char *)getenv("FB_FILE");

        if (!file || file[0] == '\0') {
            /* None set, use first valid device as default */
	    i = 0;
	    static const char *plist[] = {"wgl", "ogl", "X", NULL};
	    char device[1024] = {0};
	    snprintf(device, sizeof(device), "/dev/%s", plist[i]);
	    b = device;

	    while (plist[i]) {
		f_it = fmb->find(std::string(b));
		if (f_it == fmb->end()) {
		    i++;
		    snprintf(device, sizeof(device), "/dev/%s", plist[i]);
		    b = device;
		    continue;
		}
		const struct fb *f = f_it->second;
		*ifp->i = *(f->i);          /* struct copy */
		file = ifp->i->if_name;
		goto found_interface;
	    }

            *ifp->i = *fb_null_interface.i; /* struct copy */
            file = ifp->i->if_name;
            goto found_interface;
        }
    }

    /*
     * Determine what type of hardware the device name refers to.
     *
     * "file" can in general look like: hostname:/pathname/devname#
     *
     * If we have a ':' assume the remote interface
     * (We don't check to see if it's us. Good for debugging.)
     * else strip out "/path/devname" and try to look it up in the
     * device array.  If we don't find it assume it's a file.
     */
    for (f_it = fmb->begin(); f_it != fmb->end(); f_it++) {
	if (bu_strncmp(file, f_it->first.c_str(), strlen(f_it->first.c_str())) == 0) {
	    break;
	}
    }
    if (f_it != fmb->end()) {
	const struct fb *f = f_it->second;
	*ifp->i = *(f->i);        /* struct copy */
	goto found_interface;
    }


    /* Not in list, check special interfaces or disk files */
    /* "/dev/" protection! */
    if (bu_strncmp(file, "/dev/", 5) == 0) {
        fb_log("fb_open: no such device \"%s\".\n", file);
        free((void *) ifp);
        return FB_NULL;
    }

    if (fb_totally_numeric(file) || strchr(file, ':') != NULL) {
        /* We have a remote file name of the form <host>:<file>
         * or a port number (which assumes localhost) */
        *ifp->i = *remote_interface.i;
        goto found_interface;
    }

    /* Assume it's a disk file */
    if (_fb_disk_enable) {
        *ifp->i = *disk_interface.i;
    } else {
        fb_log("fb_open: no such device \"%s\".\n", file);
        free((void *) ifp);
        return FB_NULL;
    }

found_interface:
    /* Mark OK by filling in magic number */
    ifp->i->if_magic = FB_MAGIC;

    i = (*ifp->i->if_open)(ifp, file, width, height);
    if (i != 0) {
        ifp->i->if_magic = 0;           /* sanity */
        free((void *) ifp);

        if (i < 0)
            fb_log("fb_open: can't open device \"%s\", ret=%d.\n", file, i);
        else
            bu_exit(0, "Terminating early by request\n"); /* e.g., zap memory */

        return FB_NULL;
    }
    return ifp;
}


/**
 * Generic Help.
 * Print out the list of available frame buffers.
 */
extern "C" int
fb_genhelp(void)
{
    std::map<std::string, const struct fb *> *fmb = (std::map<std::string, const struct fb *> *)fb_backends;
    std::map<std::string, const struct fb *>::iterator f_it;

    for (f_it = fmb->begin(); f_it != fmb->end(); f_it++) {
	const struct fb *f = f_it->second;
	fb_log("%-12s  %s\n", f->i->if_name, f->i->if_type);
    }

    /* Print the ones not in the device list */
    fb_log("%-12s  %s\n",
           remote_interface.i->if_name,
           remote_interface.i->if_type);

    if (_fb_disk_enable) {
        fb_log("%-12s  %s\n",
               disk_interface.i->if_name,
               disk_interface.i->if_type);
    }

    return 0;
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
