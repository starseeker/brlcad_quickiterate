/*              Q G E D L E G A C Y L O A D E R . H
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
/** @file QgEdLegacyLoader.h
 *
 * Centralized loader for legacy qged_plugin_info shared-library plugins.
 *
 * Phase 4 of the qged modernization extracts the bu_dlopen / qged_plugin_info
 * scanning that used to live in each QgEdPalette constructor and consolidates
 * it here so that:
 *
 *   - Each plugin file is opened exactly once regardless of the number of
 *     palette categories.
 *   - All dlopen handles are owned by this object and closed when it is
 *     destroyed (i.e. when QgEdMainWindow is destroyed).
 *   - QgEdPalette is reduced to a plain QgToolPalette subclass that does
 *     no scanning of its own.
 *
 * This class intentionally lives in src/qged (not in libqtcad) because it
 * exposes the qged-specific qged_plugin_info ABI which must not pollute the
 * public libqtcad headers.
 *
 * Usage:
 *
 *   QgEdLegacyLoader *loader = new QgEdLegacyLoader(this);
 *   loader->populate(vc, oc);
 *
 * The loader can safely be destroyed before the palettes it populated because
 * QgToolPaletteElement objects are parented to the palette (Qt ownership).
 * The only hard constraint is that the plugin-supplied code (loaded via
 * dlopen) must remain mapped until all QgToolPaletteElement instances that
 * were created from it are destroyed.  Since the loader is owned by the main
 * window (parent = QgEdMainWindow) and the palettes are also owned by the main
 * window, Qt destroys children in construction order, so the loader outlives
 * the palettes — the dlopen handles are closed only after the palette widgets
 * (and their elements) are gone.
 */

#ifndef QGEDLEGACYLOADER_H
#define QGEDLEGACYLOADER_H

#include <vector>
#include <QObject>

class QgEdPalette;

class QgEdLegacyLoader : public QObject
{
    public:
	explicit QgEdLegacyLoader(QObject *parent = nullptr);
	~QgEdLegacyLoader();

	/* Scan LIBEXEC/qged for legacy qged_plugin_info plugins, create palette
	 * elements, and add them to the appropriate palette (vc or oc) based on
	 * each plugin's api_version field.  May be called only once.  Both vc
	 * and oc must be non-NULL. */
	void populate(QgEdPalette *vc, QgEdPalette *oc);

    private:
	std::vector<void *> m_handles;
};

#endif /* QGEDLEGACYLOADER_H */

/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
