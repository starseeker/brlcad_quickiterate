/*                        Q G E D A P P . H
 * BRL-CAD
 *
 * Copyright (c) 2014-2025 United States Government as represented by
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
/** @file QgEdApp.h
 *
 * Specialization of QApplication that adds information specific
 * to BRL-CAD's data and functionality
 */

#ifndef QGEDAPP_H
#define QGEDAPP_H

#include <QApplication>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>
#include <QModelIndex>
#include <QTimer>

#include "bsg.h"
#include "raytrace.h"
#include "ged.h"
#include "qtcad/QgModel.h"
#include "qtcad/QgTreeView.h"
#include "qtcad/QgSignalFlags.h"

#include "QgEdMainWindow.h"

/* Command type for application level commands */

/* Derive the core application class from QApplication.  This is central to
 * QGED, as the primary application state is managed in this container.
 * The main window is primarily responsible for graphical windows, but
 * application wide facilities like the current GED pointer and the ability
 * to run commands are defined here. */

class QgEdApp : public QApplication
{
    Q_OBJECT

    public:
	QgEdApp(int &argc, char *argv[], int swrast_mode = 0, int quad_mode = 0);
	~QgEdApp();

	[[nodiscard]] int run_cmd(struct bu_vls *msg, int argc, const char **argv);
	[[nodiscard]] int load_g_file(const char *gfile = nullptr, bool do_conversion = true);

	QgModel *mdl = nullptr;

    signals:
	void view_update(unsigned long long);
	void dbi_update(struct db_i *dbip);

        /* Menu slots */
    public slots:
	void open_file();
        void write_settings();

	/* GUI/View connection slots */
    public slots:

	// "Universal" slot to be connected to for widgets altering the view.
	// Will emit the view_update signal to notify all concerned widgets in
	// the application of the change.  Note that nothing attached to that
	// signal should trigger ANY logic (directly OR indirectly) that leads
	// back to this slot being called again, or an infinite loop may
	// result.
	void do_view_changed(unsigned long long);

	// This slot is used for quad view configurations - it is called if the
	// user uses the mouse to select one of multiple views.  This slot has
	// the responsibility to notify GUI elements of a view change via
	// do_view_change, after updating the current gedp->ged_gvp pointer to
	// refer to the now-current view.
	void do_quad_view_change(QgView *);

       	/* Utility slots */
    public slots:
	void run_qcmd(const QString &command);
	void element_selected(QgToolPaletteElement *el);

	// Slot connected to the background-geometry drain timer.
	// Called every ~100 ms; drains DbiState::drain_geom_results() and
	// emits view_update when new bounding-box data has arrived.
	// This is the notification-bundling mechanism: multiple background bbox
	// results that arrive within one timer interval are coalesced into a
	// single view_update emission rather than triggering a repaint for each
	// individual result.
	void drain_background_geom();

    public:
	QgEdMainWindow *w = nullptr;

    private slots:
	// Internal slot that performs the actual work once the event loop is
	// re-entered after a batch of coalesced do_view_changed calls.
	void flush_view_changed_();

    private:
	std::vector<char *> tmp_av;
	unsigned long long select_hash = 0;
	long history_mark_start = -1;
	long history_mark_end = -1;

	// Periodic timer for draining background geometry results.
	// Fires every BG_GEOM_DRAIN_INTERVAL_MS milliseconds.
	static constexpr int BG_GEOM_DRAIN_INTERVAL_MS = 100;
	QTimer *geom_drain_timer_ = nullptr;

	// Tracks how many LoD results have been processed across all drain
	// calls so far for the current file.  Used to detect when new LoD
	// data has arrived and a full scene redraw is needed.
	size_t last_lod_count_ = 0;

	// Coalescing flags for do_view_changed.  When a do_view_changed call
	// arrives while a queued invocation is already pending, the new flags
	// are OR-ed in here so that the single queued call handles everything.
	unsigned long long pending_view_flags_ = 0;
	bool view_change_pending_ = false;
};

#endif // QGEDAPP_H

// Local Variables:
// mode: C++
// tab-width: 8
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

