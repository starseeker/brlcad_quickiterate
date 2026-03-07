/*                       Q S K E T C H . C P P
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
/** @file qsketch.cpp
 *
 * Standalone BRL-CAD sketch editor demo using libqtcad.
 *
 * Usage:
 *   qsketch  <file.g>  <sketch_name>
 *
 * Opens (or creates) a sketch primitive named <sketch_name> in the
 * given .g database file and presents an interactive editing UI built
 * with libqtcad widgets.  The editing backend uses the librt
 * ECMD_SKETCH_* API (rt/rt_ecmds.h) via the QgSketchFilter event
 * filters provided in libqtcad.
 *
 * UI layout
 * ---------
 *  Left panel  — toolbar with editing-mode buttons
 *  Centre      — QgView (software render) showing the sketch face-on
 *  Right panel — two tables listing vertices and segments
 *  Bottom bar  — status line + Save / Undo / New Line buttons
 *
 * Editing modes
 * -------------
 *  Pick Vertex  (P) — left-click to select nearest vertex
 *  Move Vertex  (M) — left-drag to move selected vertex
 *  Add Vertex   (A) — left-click to add a vertex at cursor
 *  Pick Segment (S) — left-click to select nearest segment
 *  Move Segment (G) — left-drag to move selected segment
 *  Add Line     (L) — click two vertices then press Enter to create a
 *                      line segment between them
 *  Add Arc      (R) — start, end, radius dialog
 *  Add Bezier   (B) — click control points, press Enter to commit
 *
 * Keyboard shortcuts:
 *  Ctrl+S  — save to .g file
 *  Ctrl+Z  — undo (single level)
 *  Escape  — cancel current operation / return to idle
 *  Delete  — delete selected vertex or segment
 */

#include "common.h"

#include <cstring>
#include <string>
#include <vector>

#include "bu/app.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/str.h"
#include "bn/tol.h"
#include "bv.h"
#include "bv/util.h"
#include "raytrace.h"
#include "rt/functab.h"
#include "rt/geom.h"
#include "rt/db_internal.h"
#include "rt/db_io.h"
#include "rt/primitives/sketch.h"
#include "rt/rt_ecmds.h"

#include <QApplication>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include "qtcad/QgSignalFlags.h"
#include "qtcad/QgSketchFilter.h"
#include "qtcad/QgSW.h"
#include "qtcad/QgView.h"

/* ------------------------------------------------------------------ */
/* Helpers: create a minimal in-plane sketch                          */
/* ------------------------------------------------------------------ */

/*
 * Create an empty sketch at the origin, with the u_vec along +X and
 * v_vec along +Y, so it lies in the XY plane.
 */
static struct directory *
sketch_create_empty(struct db_i *dbip, const char *name)
{
    struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_DISK);
    if (!wdbp)
	return NULL;

    struct rt_sketch_internal *skt;
    BU_ALLOC(skt, struct rt_sketch_internal);
    skt->magic = RT_SKETCH_INTERNAL_MAGIC;
    VSET(skt->V,     0.0, 0.0, 0.0);
    VSET(skt->u_vec, 1.0, 0.0, 0.0);
    VSET(skt->v_vec, 0.0, 1.0, 0.0);
    skt->vert_count = 0;
    skt->verts      = NULL;
    skt->curve.count   = 0;
    skt->curve.segment = NULL;
    skt->curve.reverse = NULL;

    wdb_export(wdbp, name, (void *)skt, ID_SKETCH, 1.0);
    wdb_close(wdbp);

    return db_lookup(dbip, name, LOOKUP_QUIET);
}

/* ------------------------------------------------------------------ */
/* Wireframe refresh                                                   */
/* ------------------------------------------------------------------ */

/*
 * Write the current edit state back to the database and redisplay.
 *
 * This is called after every edit operation so the view shows the
 * latest geometry.  In a production qged integration the wireframe
 * would instead be refreshed from the in-memory es_int directly via
 * EDOBJ.ft_plot without touching the database; for this demo the
 * write-then-read round-trip keeps things simple.
 */
static int
sketch_write_to_db(struct rt_edit *es, struct db_i *dbip, struct directory *dp)
{
    if (!es || !dbip || !dp)
	return BRLCAD_ERROR;

    return rt_db_put_internal(dp, dbip, &es->es_int, &rt_uniresource);
}

/* ------------------------------------------------------------------ */
/* Vertex / segment table helpers                                      */
/* ------------------------------------------------------------------ */

static void
populate_vertex_table(QTableWidget *tw, const struct rt_sketch_internal *skt)
{
    tw->setRowCount(0);
    if (!skt)
	return;

    tw->setRowCount((int)skt->vert_count);
    for (size_t i = 0; i < skt->vert_count; i++) {
	char buf[64];
	snprintf(buf, sizeof(buf), "%zu", i);
	tw->setItem((int)i, 0, new QTableWidgetItem(buf));
	snprintf(buf, sizeof(buf), "%.4g", skt->verts[i][0]);
	tw->setItem((int)i, 1, new QTableWidgetItem(buf));
	snprintf(buf, sizeof(buf), "%.4g", skt->verts[i][1]);
	tw->setItem((int)i, 2, new QTableWidgetItem(buf));
    }
}

static void
populate_segment_table(QTableWidget *tw, const struct rt_sketch_internal *skt)
{
    tw->setRowCount(0);
    if (!skt)
	return;

    tw->setRowCount((int)skt->curve.count);
    for (size_t i = 0; i < skt->curve.count; i++) {
	void *seg = skt->curve.segment[i];
	if (!seg) continue;

	char buf[256] = "";
	const char *type_str = "?";

	uint32_t magic = *(uint32_t *)seg;
	if (magic == CURVE_LSEG_MAGIC) {
	    struct line_seg *ls = (struct line_seg *)seg;
	    type_str = "Line";
	    snprintf(buf, sizeof(buf), "%d → %d", ls->start, ls->end);
	} else if (magic == CURVE_CARC_MAGIC) {
	    struct carc_seg *cs = (struct carc_seg *)seg;
	    if (cs->radius < 0.0) {
		type_str = "Circle";
		snprintf(buf, sizeof(buf), "centre=%d r=%.4g",
			 cs->end, -cs->radius);
	    } else {
		type_str = "Arc";
		snprintf(buf, sizeof(buf), "%d→%d r=%.4g",
			 cs->start, cs->end, cs->radius);
	    }
	} else if (magic == CURVE_BEZIER_MAGIC) {
	    struct bezier_seg *bs = (struct bezier_seg *)seg;
	    type_str = "Bezier";
	    std::string pts;
	    for (int k = 0; k <= bs->degree; k++) {
		if (k) pts += '-';
		pts += std::to_string(bs->ctl_points[k]);
	    }
	    snprintf(buf, sizeof(buf), "deg=%d [%s]", bs->degree, pts.c_str());
	} else if (magic == CURVE_NURB_MAGIC) {
	    struct nurb_seg *ns = (struct nurb_seg *)seg;
	    type_str = "NURB";
	    snprintf(buf, sizeof(buf), "order=%d c_size=%d",
		     ns->order, ns->c_size);
	}

	char idx[16];
	snprintf(idx, sizeof(idx), "%zu", i);
	tw->setItem((int)i, 0, new QTableWidgetItem(idx));
	tw->setItem((int)i, 1, new QTableWidgetItem(type_str));
	tw->setItem((int)i, 2, new QTableWidgetItem(buf));
    }
}

/* ------------------------------------------------------------------ */
/* QSketchEditWindow                                                   */
/* ------------------------------------------------------------------ */

/**
 * Main editing window for the qsketch demo.
 *
 * Owns a QgView and the rt_edit context, manages filter switching,
 * and keeps the vertex/segment tables in sync with the sketch data.
 */
class QSketchEditWindow : public QMainWindow
{
    Q_OBJECT

public:
    QSketchEditWindow(struct db_i *dbip,
		      struct directory *dp,
		      QWidget *parent = NULL);
    ~QSketchEditWindow();

private slots:
    void on_sketch_changed();
    void on_save();
    void on_undo();
    void on_mode_pick_vertex();
    void on_mode_move_vertex();
    void on_mode_add_vertex();
    void on_mode_pick_segment();
    void on_mode_move_segment();
    void on_mode_add_line();
    void on_mode_add_arc();
    void on_mode_add_bezier();
    void on_delete_selected();

private:
    void install_filter(QgSketchFilter *f);
    void clear_filter();
    void refresh_tables();
    void refresh_view();
    void set_status(const QString &msg);

    /* Edit-state helpers */
    struct rt_sketch_internal *sketch() const {
	if (!m_es) return NULL;
	return (struct rt_sketch_internal *)m_es->es_int.idb_ptr;
    }
    struct rt_sketch_edit *sketch_edit() const {
	if (!m_es) return NULL;
	return (struct rt_sketch_edit *)m_es->ipe_ptr;
    }

    /* Enum for line/bezier multi-click accumulation */
    enum CreateMode { NONE, LINE, BEZIER };

    /* ---- data ---- */
    struct db_i          *m_dbip = NULL;
    struct directory     *m_dp   = NULL;
    struct bview         *m_bv   = NULL;
    struct rt_edit       *m_es   = NULL;
    struct bn_tol         m_tol;

    /* ---- Qt UI ---- */
    QgView          *m_view   = NULL;
    QTableWidget    *m_vtable = NULL;  /* vertex table */
    QTableWidget    *m_stable = NULL;  /* segment table */
    QLabel          *m_status = NULL;

    /* ---- active filter ---- */
    QgSketchFilter  *m_active_filter = NULL;

    /* ---- multi-click segment creation ---- */
    CreateMode       m_create_mode = NONE;
    std::vector<int> m_pending_verts;   /* vertex indices gathered so far */
};


/* ---------- constructor ---------- */

QSketchEditWindow::QSketchEditWindow(struct db_i *dbip,
				     struct directory *dp,
				     QWidget *parent)
    : QMainWindow(parent), m_dbip(dbip), m_dp(dp)
{
    setWindowTitle(QString("qsketch — %1").arg(dp->d_namep));
    resize(1200, 700);

    /* ---- tolerance ---- */
    BN_TOL_INIT(&m_tol);

    /* ---- bview ---- */
    BU_GET(m_bv, struct bview);
    bv_init(m_bv, NULL);

    /* Look along -Z toward +Z (top view, sketch in XY plane face-on).
     * az=0, el=90 gives view +X→right, +Y→up which matches the sketch
     * u_vec (1,0,0) and v_vec (0,1,0) defaults. */
    VSET(m_bv->gv_aet, 0.0, 90.0, 0.0);
    bv_mat_aet(m_bv);
    m_bv->gv_scale  = 250.0;
    m_bv->gv_size   = 2.0 * m_bv->gv_scale;
    m_bv->gv_isize  = 1.0 / m_bv->gv_size;
    bv_update(m_bv);
    bu_vls_sprintf(&m_bv->gv_name, "qsketch");
    m_bv->gv_width  = 700;
    m_bv->gv_height = 700;

    /* ---- rt_edit ---- */
    struct db_full_path fp;
    db_full_path_init(&fp);
    db_add_node_to_full_path(&fp, dp);
    m_es = rt_edit_create(&fp, dbip, &m_tol, m_bv);
    db_free_full_path(&fp);

    if (!m_es) {
	bu_log("qsketch: rt_edit_create failed\n");
	return;
    }
    m_es->local2base = 1.0;  /* mm database */
    m_es->base2local = 1.0;
    m_es->mv_context = 1;

    /* Take an initial checkpoint for undo */
    rt_edit_checkpoint(m_es);

    /* ----  QgView ---- */
    m_view = new QgView(this, QgView_SW);
    m_view->set_view(m_bv);

    /* ---- vertex table ---- */
    m_vtable = new QTableWidget(this);
    m_vtable->setColumnCount(3);
    m_vtable->setHorizontalHeaderLabels({"#", "U", "V"});
    m_vtable->horizontalHeader()->setStretchLastSection(true);
    m_vtable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_vtable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_vtable->setMinimumWidth(200);

    /* ---- segment table ---- */
    m_stable = new QTableWidget(this);
    m_stable->setColumnCount(3);
    m_stable->setHorizontalHeaderLabels({"#", "Type", "Params"});
    m_stable->horizontalHeader()->setStretchLastSection(true);
    m_stable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_stable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stable->setMinimumWidth(200);

    /* ---- right panel (tables) ---- */
    QSplitter *right_split = new QSplitter(Qt::Vertical, this);
    {
	QGroupBox *vg = new QGroupBox("Vertices");
	QVBoxLayout *vl = new QVBoxLayout(vg);
	vl->addWidget(m_vtable);
	right_split->addWidget(vg);
    }
    {
	QGroupBox *sg = new QGroupBox("Segments");
	QVBoxLayout *sl = new QVBoxLayout(sg);
	sl->addWidget(m_stable);
	right_split->addWidget(sg);
    }
    right_split->setMinimumWidth(210);

    /* ---- central splitter (view | tables) ---- */
    QSplitter *main_split = new QSplitter(Qt::Horizontal, this);
    main_split->addWidget(m_view);
    main_split->addWidget(right_split);
    main_split->setSizes({700, 250});
    setCentralWidget(main_split);

    /* ---- toolbar ---- */
    QToolBar *tb = addToolBar("Edit Modes");
    tb->setOrientation(Qt::Vertical);
    addToolBar(Qt::LeftToolBarArea, tb);

    auto mkbtn = [&](const QString &label, const char *tip, auto slot) {
	QPushButton *b = new QPushButton(label, this);
	b->setToolTip(tip);
	connect(b, &QPushButton::clicked, this, slot);
	QWidgetAction *wa = new QWidgetAction(tb);
	wa->setDefaultWidget(b);
	tb->addAction(wa);
    };

    mkbtn("Pick V",  "Pick Vertex (P)",   &QSketchEditWindow::on_mode_pick_vertex);
    mkbtn("Move V",  "Move Vertex (M)",   &QSketchEditWindow::on_mode_move_vertex);
    mkbtn("Add V",   "Add Vertex (A)",    &QSketchEditWindow::on_mode_add_vertex);
    tb->addSeparator();
    mkbtn("Pick S",  "Pick Segment (S)",  &QSketchEditWindow::on_mode_pick_segment);
    mkbtn("Move S",  "Move Segment (G)",  &QSketchEditWindow::on_mode_move_segment);
    tb->addSeparator();
    mkbtn("Line",    "Add Line (L)",      &QSketchEditWindow::on_mode_add_line);
    mkbtn("Arc",     "Add Arc (R)",       &QSketchEditWindow::on_mode_add_arc);
    mkbtn("Bezier",  "Add Bezier (B)",    &QSketchEditWindow::on_mode_add_bezier);
    tb->addSeparator();
    mkbtn("Delete",  "Delete selected (Del)", &QSketchEditWindow::on_delete_selected);

    /* ---- menu bar ---- */
    QMenu *file_menu = menuBar()->addMenu("&File");
    file_menu->addAction("&Save\tCtrl+S", this,
			  &QSketchEditWindow::on_save);
    file_menu->addSeparator();
    file_menu->addAction("&Quit\tCtrl+Q", this, &QWidget::close);

    QMenu *edit_menu = menuBar()->addMenu("&Edit");
    edit_menu->addAction("&Undo\tCtrl+Z", this,
			  &QSketchEditWindow::on_undo);
    edit_menu->addAction("&Delete Selected\tDel", this,
			  &QSketchEditWindow::on_delete_selected);

    /* ---- status bar ---- */
    m_status = new QLabel("Ready", this);
    statusBar()->addWidget(m_status, 1);

    /* ---- initial display ---- */
    refresh_tables();
    set_status("Sketch loaded — choose an edit mode from the toolbar.");
}

QSketchEditWindow::~QSketchEditWindow()
{
    clear_filter();
    if (m_es)
	rt_edit_destroy(m_es);
    if (m_bv) {
	bv_free(m_bv);
	BU_PUT(m_bv, struct bview);
    }
}

/* ---------- filter management ---------- */

void
QSketchEditWindow::install_filter(QgSketchFilter *f)
{
    clear_filter();
    if (!f) return;

    f->v  = m_bv;
    f->es = m_es;

    connect(f, &QgSketchFilter::view_updated,
	    m_view, &QgView::need_update);
    connect(f, &QgSketchFilter::sketch_changed,
	    this,  &QSketchEditWindow::on_sketch_changed);

    m_view->add_event_filter(f);
    m_active_filter = f;
}

void
QSketchEditWindow::clear_filter()
{
    if (!m_active_filter) return;

    m_view->clear_event_filter(m_active_filter);
    disconnect(m_active_filter, nullptr, this, nullptr);
    disconnect(m_active_filter, nullptr, m_view, nullptr);
    delete m_active_filter;
    m_active_filter = NULL;
}

/* ---------- view / table refresh ---------- */

void
QSketchEditWindow::refresh_tables()
{
    populate_vertex_table(m_vtable, sketch());
    populate_segment_table(m_stable, sketch());

    /* Highlight currently selected vertex row */
    struct rt_sketch_edit *se = sketch_edit();
    if (se && se->curr_vert >= 0
	    && se->curr_vert < m_vtable->rowCount())
	m_vtable->selectRow(se->curr_vert);

    if (se && se->curr_seg >= 0
	    && se->curr_seg < m_stable->rowCount())
	m_stable->selectRow(se->curr_seg);
}

void
QSketchEditWindow::refresh_view()
{
    /* Write edited sketch back to the DB so the view can re-read it */
    sketch_write_to_db(m_es, m_dbip, m_dp);
    m_view->need_update(QG_VIEW_REFRESH);
}

void
QSketchEditWindow::set_status(const QString &msg)
{
    m_status->setText(msg);
}

/* ---------- slots ---------- */

void
QSketchEditWindow::on_sketch_changed()
{
    refresh_tables();
    refresh_view();
}

void
QSketchEditWindow::on_save()
{
    if (!m_es || !m_dbip || !m_dp) {
	QMessageBox::warning(this, "Save", "No sketch open.");
	return;
    }
    int ret = sketch_write_to_db(m_es, m_dbip, m_dp);
    if (ret == BRLCAD_OK) {
	rt_edit_checkpoint(m_es);   /* update undo baseline */
	set_status("Saved.");
    } else {
	QMessageBox::critical(this, "Save", "Failed to write sketch to database.");
    }
}

void
QSketchEditWindow::on_undo()
{
    if (!m_es) return;
    if (rt_edit_revert(m_es) == BRLCAD_OK) {
	on_sketch_changed();
	set_status("Reverted to last checkpoint.");
    } else {
	set_status("Nothing to undo.");
    }
}

/* ---- mode buttons ---- */

void QSketchEditWindow::on_mode_pick_vertex()
{
    m_create_mode = NONE;
    m_pending_verts.clear();
    install_filter(new QgSketchPickVertexFilter());
    set_status("Pick Vertex: click on or near a vertex to select it.");
}

void QSketchEditWindow::on_mode_move_vertex()
{
    m_create_mode = NONE;
    m_pending_verts.clear();
    install_filter(new QgSketchMoveVertexFilter());
    set_status("Move Vertex: hold left button and drag to reposition selected vertex.");
}

void QSketchEditWindow::on_mode_add_vertex()
{
    m_create_mode = NONE;
    m_pending_verts.clear();
    install_filter(new QgSketchAddVertexFilter());
    set_status("Add Vertex: left-click to place a new vertex.");
}

void QSketchEditWindow::on_mode_pick_segment()
{
    m_create_mode = NONE;
    m_pending_verts.clear();
    install_filter(new QgSketchPickSegmentFilter());
    set_status("Pick Segment: click near a segment to select it.");
}

void QSketchEditWindow::on_mode_move_segment()
{
    m_create_mode = NONE;
    m_pending_verts.clear();
    install_filter(new QgSketchMoveSegmentFilter());
    set_status("Move Segment: hold left button and drag to translate selected segment.");
}

/* ---- Add Line: pick two vertices then press Enter ---- */

void QSketchEditWindow::on_mode_add_line()
{
    m_create_mode = LINE;
    m_pending_verts.clear();

    /* Install an AddVertex filter to place/select vertices */
    auto *f = new QgSketchAddVertexFilter();
    install_filter(f);

    /* Connect a one-shot handler to accumulate vertex picks */
    connect(f, &QgSketchFilter::sketch_changed, this, [this]() {
	if (m_create_mode != LINE) return;

	struct rt_sketch_edit *se = sketch_edit();
	if (!se || se->curr_vert < 0) return;

	int vi = se->curr_vert;

	/* Avoid duplicates */
	for (int x : m_pending_verts)
	    if (x == vi) return;

	m_pending_verts.push_back(vi);
	set_status(QString("Add Line: vertex %1 picked (%2/2). "
			   "Pick another or press Enter to finish.")
			   .arg(vi).arg((int)m_pending_verts.size()));

	if ((int)m_pending_verts.size() >= 2)
	    set_status("Add Line: 2 vertices selected — press Enter to create line.");
    });

    set_status("Add Line: click to place/select vertex 1.");
}

/* ---- Add Arc ---- */

void QSketchEditWindow::on_mode_add_arc()
{
    m_create_mode = NONE;
    m_pending_verts.clear();
    clear_filter();

    /* Collect parameters via a dialog */
    QDialog dlg(this);
    dlg.setWindowTitle("Add Circular Arc");
    QFormLayout *fl = new QFormLayout(&dlg);

    QSpinBox *sb_start = new QSpinBox(&dlg);
    sb_start->setRange(0, 9999);
    QSpinBox *sb_end = new QSpinBox(&dlg);
    sb_end->setRange(0, 9999);
    QDoubleSpinBox *sb_r = new QDoubleSpinBox(&dlg);
    sb_r->setRange(-9999, 9999);
    sb_r->setDecimals(4);
    sb_r->setValue(10.0);
    sb_r->setToolTip("Radius in mm.  Negative = full circle (end is centre).");
    QCheckBox *cb_left = new QCheckBox("Centre to left", &dlg);
    QCheckBox *cb_cw   = new QCheckBox("Clockwise",      &dlg);

    fl->addRow("Start vertex index:", sb_start);
    fl->addRow("End vertex index:",   sb_end);
    fl->addRow("Radius (mm):",        sb_r);
    fl->addRow("",                    cb_left);
    fl->addRow("",                    cb_cw);

    QDialogButtonBox *bb = new QDialogButtonBox(
	    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    fl->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
	set_status("Arc creation cancelled.");
	return;
    }

    if (!m_es) return;
    rt_edit_checkpoint(m_es);

    m_es->e_para[0] = (fastf_t)sb_start->value();
    m_es->e_para[1] = (fastf_t)sb_end->value();
    m_es->e_para[2] = sb_r->value();
    m_es->e_para[3] = cb_left->isChecked() ? 1.0 : 0.0;
    m_es->e_para[4] = cb_cw->isChecked()   ? 1.0 : 0.0;
    m_es->e_inpara  = 5;

    EDOBJ[m_es->es_int.idb_type].ft_set_edit_mode(m_es,
	    ECMD_SKETCH_APPEND_ARC);
    rt_edit_process(m_es);

    on_sketch_changed();
    set_status("Arc added.");
}

/* ---- Add Bezier ---- */

void QSketchEditWindow::on_mode_add_bezier()
{
    m_create_mode = BEZIER;
    m_pending_verts.clear();
    clear_filter();

    /* Install an AddVertex filter; accumulate until Enter */
    auto *f = new QgSketchAddVertexFilter();
    install_filter(f);

    connect(f, &QgSketchFilter::sketch_changed, this, [this]() {
	if (m_create_mode != BEZIER) return;

	struct rt_sketch_edit *se = sketch_edit();
	if (!se || se->curr_vert < 0) return;

	int vi = se->curr_vert;
	for (int x : m_pending_verts)
	    if (x == vi) return;

	m_pending_verts.push_back(vi);
	set_status(QString("Add Bezier: %1 control point(s) — "
			   "click more or press Enter to commit.")
			   .arg((int)m_pending_verts.size()));
    });

    set_status("Add Bezier: click to place control points, then press Enter.");
}

/* ---- Delete ---- */

void QSketchEditWindow::on_delete_selected()
{
    if (!m_es) return;
    struct rt_sketch_edit *se = sketch_edit();
    if (!se) return;

    if (se->curr_seg >= 0) {
	rt_edit_checkpoint(m_es);
	m_es->e_inpara = 0;
	EDOBJ[m_es->es_int.idb_type].ft_set_edit_mode(m_es,
		ECMD_SKETCH_DELETE_SEGMENT);
	rt_edit_process(m_es);
	on_sketch_changed();
	set_status("Segment deleted.");
	return;
    }

    if (se->curr_vert >= 0) {
	rt_edit_checkpoint(m_es);
	m_es->e_inpara = 0;
	EDOBJ[m_es->es_int.idb_type].ft_set_edit_mode(m_es,
		ECMD_SKETCH_DELETE_VERTEX);
	rt_edit_process(m_es);
	on_sketch_changed();
	set_status("Vertex deleted (only if unreferenced by any segment).");
	return;
    }

    set_status("Nothing selected to delete.");
}

/* ---------- keyboard event ---------- */

void
QSketchEditWindow::keyPressEvent(QKeyEvent *ev)
{
    switch (ev->key()) {
	case Qt::Key_P:
	    on_mode_pick_vertex();
	    break;
	case Qt::Key_M:
	    on_mode_move_vertex();
	    break;
	case Qt::Key_A:
	    on_mode_add_vertex();
	    break;
	case Qt::Key_S:
	    if (ev->modifiers() & Qt::ControlModifier)
		on_save();
	    else
		on_mode_pick_segment();
	    break;
	case Qt::Key_G:
	    on_mode_move_segment();
	    break;
	case Qt::Key_L:
	    on_mode_add_line();
	    break;
	case Qt::Key_R:
	    on_mode_add_arc();
	    break;
	case Qt::Key_B:
	    on_mode_add_bezier();
	    break;
	case Qt::Key_Z:
	    if (ev->modifiers() & Qt::ControlModifier)
		on_undo();
	    break;
	case Qt::Key_Delete:
	case Qt::Key_Backspace:
	    on_delete_selected();
	    break;

	case Qt::Key_Return:
	case Qt::Key_Enter:
	    /* Commit pending line / bezier creation */
	    if (m_create_mode == LINE && (int)m_pending_verts.size() >= 2) {
		if (m_es) {
		    rt_edit_checkpoint(m_es);
		    m_es->e_para[0] = (fastf_t)m_pending_verts[0];
		    m_es->e_para[1] = (fastf_t)m_pending_verts[1];
		    m_es->e_inpara  = 2;
		    EDOBJ[m_es->es_int.idb_type].ft_set_edit_mode(m_es,
			    ECMD_SKETCH_APPEND_LINE);
		    rt_edit_process(m_es);
		    on_sketch_changed();
		    set_status(QString("Line added (%1 → %2).")
				       .arg(m_pending_verts[0])
				       .arg(m_pending_verts[1]));
		}
		m_create_mode = NONE;
		m_pending_verts.clear();
		clear_filter();
	    } else if (m_create_mode == BEZIER
		       && (int)m_pending_verts.size() >= 2) {
		if (m_es) {
		    rt_edit_checkpoint(m_es);
		    int n = (int)m_pending_verts.size();
		    for (int i = 0; i < n; i++)
			m_es->e_para[i] = (fastf_t)m_pending_verts[i];
		    m_es->e_inpara = n;
		    EDOBJ[m_es->es_int.idb_type].ft_set_edit_mode(m_es,
			    ECMD_SKETCH_APPEND_BEZIER);
		    rt_edit_process(m_es);
		    on_sketch_changed();
		    set_status(QString("Bezier added (degree %1).")
				       .arg(n - 1));
		}
		m_create_mode = NONE;
		m_pending_verts.clear();
		clear_filter();
	    }
	    break;

	case Qt::Key_Escape:
	    m_create_mode = NONE;
	    m_pending_verts.clear();
	    clear_filter();
	    set_status("Idle.");
	    break;

	default:
	    QMainWindow::keyPressEvent(ev);
	    break;
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

#include "qsketch.moc"

int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);

    QApplication app(argc, argv);

    /* We need exactly 2 positional arguments: <file.g> <sketch_name> */
    if (argc != 3) {
	bu_exit(1,
	    "Usage: qsketch <file.g> <sketch_name>\n"
	    "\n"
	    "Opens or creates a sketch primitive in the given .g file\n"
	    "and presents an interactive Qt editing window.\n");
    }

    const char *g_file   = argv[1];
    const char *sk_name  = argv[2];

    /* ---- open database ---- */
    struct db_i *dbip = db_open(g_file, DB_OPEN_READWRITE);
    if (dbip == DBI_NULL) {
	/* Try creating it */
	struct rt_wdb *wdbp = wdb_fopen(g_file);
	if (!wdbp)
	    bu_exit(1, "qsketch: cannot open or create '%s'\n", g_file);
	wdb_close(wdbp);
	dbip = db_open(g_file, DB_OPEN_READWRITE);
    }
    if (dbip == DBI_NULL)
	bu_exit(1, "qsketch: failed to open '%s'\n", g_file);

    if (db_dirbuild(dbip) < 0)
	bu_exit(1, "qsketch: db_dirbuild failed\n");

    /* ---- look up or create sketch ---- */
    struct directory *dp = db_lookup(dbip, sk_name, LOOKUP_QUIET);
    if (!dp) {
	bu_log("qsketch: '%s' not found — creating empty sketch.\n", sk_name);
	dp = sketch_create_empty(dbip, sk_name);
	if (!dp)
	    bu_exit(1, "qsketch: failed to create sketch '%s'\n", sk_name);
    }

    if (dp->d_minor_type != ID_SKETCH) {
	bu_exit(1, "qsketch: '%s' exists but is not a sketch (type %d)\n",
		sk_name, dp->d_minor_type);
    }

    /* ---- show window ---- */
    QSketchEditWindow win(dbip, dp);
    win.show();

    int ret = app.exec();

    db_close(dbip);
    return ret;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
