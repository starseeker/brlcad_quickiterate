/*                 Q G E D M A I N W I N D O W . H
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
/** @file QgEdMainWindow.h
 *
 * Defines the toplevel window for the BRL-CAD GUI, into which other windows
 * are docked.
 *
 * This widget is also responsible for connections between widgets - typically
 * this takes the form of Qt signals/slots that pass information to/from
 * widgets and keep the graphically displayed information in sync.
 */

#ifndef QGEDMAINWINDOW_H
#define QGEDMAINWINDOW_H
#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <QSettings>
#include <QStatusBar>
#include <QTreeView>

#include "ged.h"
#include "qtcad/QgAttributesModel.h"
#include "qtcad/QgConsole.h"
#include "qtcad/QgDockWidget.h"
#include "qtcad/QgQuadView.h"
#include "qtcad/QgSignalFlags.h"
#include "qtcad/QgTreeView.h"
#include "qtcad/QgView.h"
#include "qtcad/QgView.h"
#include "qtcad/QgViewCtrl.h"

#include "plugins/plugin.h"
#include "QgEdPalette.h"

#ifdef BRLCAD_ENABLE_OBOL
/* Forward-declare QgObolView so callers of QgEdMainWindow.h do not need to
 * include QgObolView.h (which pulls in QOpenGLWidget / Obol headers).  The
 * full definition is included only in QgEdMainWindow.cpp where the pointer
 * member is actually used. */
class QgObolView;
#endif

class QgEdMainWindow : public QMainWindow
{
    Q_OBJECT
    public:
	QgEdMainWindow(int canvas_type = 0, int quad_view = 0);

	QgConsole *console;

	// Post-show methods for checking validity of OpenGL initialization
	bool isValid3D();

	// Report if the quad/central widget is active
	bool isDisplayActive();

	// Get the currently active view of the quad/central display widget
	QgView * CurrentDisplay();
	bsg_view * CurrentView();

	// Checkpoint display state (used for subsequent diff)
	void DisplayCheckpoint();

	// See if the display state has changed relative to most recent checkpoint
	bool DisplayDiff();

	// Apply visual window changes indicating a raytrace has begun
	void IndicateRaytraceStart(int);

	// Clear visual window changes indicating a raytrace has begun
	void IndicateRaytraceDone();

	// Determine interaction mode based on selected palettes and the supplied point
	int InteractionMode(QPoint &gpos);

	// Utility wrapper for the closeEvent to save windowing dimensions
	void closeEvent(QCloseEvent* e) override;

    public slots:
	//void save_image();
	void do_dm_init();
#ifdef BRLCAD_ENABLE_OBOL
	void do_obol_init();
#endif
        void close();
	// Put central display into Quad mode
	void QuadDisplay();
	// Put central display into Single mode
	void SingleDisplay();

    private:

	void CreateWidgets(int canvas_type);
	void LocateWidgets();
	void ConnectWidgets();
	void SetupMenu();

	// Menu actions
	QAction *cad_open;
	QAction *cad_save_settings;
	//QAction *cad_save_image;
	QAction *cad_exit;

	// Organizational widget
	QWidget *cw = nullptr;

	// Central widgets
	QgViewCtrl *vcw = nullptr;
	QgQuadView *c4 = nullptr;
	QAction *cad_single_view = nullptr;
	QAction *cad_quad_view = nullptr;
#ifdef BRLCAD_ENABLE_OBOL
	QgObolView *obol_view_ = nullptr;
#endif

	// Docked widgets
	QgAttributesModel *stdpropmodel = nullptr;
	QgAttributesModel *userpropmodel = nullptr;
	QgEdPalette *oc = nullptr;
	QgEdPalette *vc = nullptr;
	QgTreeView *treeview = nullptr;

	// Action for toggling treeview's ls or tree view
	QAction *vm_treeview_mode_toggle = nullptr;

	// Docking containers
	QDockWidget *ocd = nullptr;
	QDockWidget *sattrd = nullptr;
	QDockWidget *uattrd = nullptr;
	QDockWidget *vcd = nullptr;
	QMenu *vm_panels = nullptr;
	QgDockWidget *console_dock = nullptr;
	QgDockWidget *tree_dock = nullptr;
};

#endif /* QGEDMAINWINDOW_H */

// Local Variables:
// mode: C++
// tab-width: 8
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

