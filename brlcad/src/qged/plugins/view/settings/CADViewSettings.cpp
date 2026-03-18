/*             C A D V I E W S E T T I N G S . C P P
 * BRL-CAD
 *
 * Copyright (c) 2023-2026 United States Government as represented by
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
/** @file CADViewSettings.cpp
 *
 * Widget implementation for viewing and controlling faceplate settings,
 * reflecting the current state of bview_settings and bv_params_state.
 *
 */

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "bu/opt.h"
#include "bu/malloc.h"
#include "bu/str.h"
#include "qtcad/QgSignalFlags.h"
#include "QgEdApp.h"

#include "CADViewSettings.h"

/* Helper: update a checkbox to reflect an integer flag, blocking signals
 * to prevent triggering a spurious view_refresh round-trip. */
static void
set_ckbx(QCheckBox *cb, int val)
{
    cb->blockSignals(true);
    cb->setCheckState(val ? Qt::Checked : Qt::Unchecked);
    cb->blockSignals(false);
}

/* Helper: return 1 if the checkbox is checked, 0 otherwise. */
static int
ckbx_val(QCheckBox *cb)
{
    return (cb->checkState() == Qt::Checked) ? 1 : 0;
}

CADViewSettings::CADViewSettings(QWidget *)
{
    QVBoxLayout *wl = new QVBoxLayout;
    wl->setAlignment(Qt::AlignTop);

    /* Top-level faceplate toggles */
    acsg_ckbx = new QCheckBox("Adaptive Plotting (CSG)");
    amesh_ckbx = new QCheckBox("Adaptive Plotting (Mesh)");
    adc_ckbx = new QCheckBox("Angle/Dist. Cursor");
    cdot_ckbx = new QCheckBox("Center Dot");
    grid_ckbx = new QCheckBox("Grid");
    mdlaxes_ckbx = new QCheckBox("Model Axes");
    scale_ckbx = new QCheckBox("Scale");
    viewaxes_ckbx = new QCheckBox("View Axes");

    /* Framebuffer mode selector: Off / Overlay / Underlay */
    QHBoxLayout *fbl = new QHBoxLayout;
    fbl->addWidget(new QLabel("Framebuffer:"));
    fb_mode_combo = new QComboBox;
    fb_mode_combo->addItem("Off");      /* index 0 -> gv_fb_mode = 0 */
    fb_mode_combo->addItem("Overlay");  /* index 1 -> gv_fb_mode = 1 */
    fb_mode_combo->addItem("Underlay"); /* index 2 -> gv_fb_mode = 2 */
    fbl->addWidget(fb_mode_combo);
    fbl->addStretch();

    /* Parameters group: master toggle + per-element sub-flags */
    params_grp = new QGroupBox("Parameters");
    QVBoxLayout *pgl = new QVBoxLayout;
    params_ckbx = new QCheckBox("Enable");
    params_size_ckbx = new QCheckBox("Size");
    params_center_ckbx = new QCheckBox("Center");
    params_az_ckbx = new QCheckBox("Azimuth");
    params_el_ckbx = new QCheckBox("Elevation");
    params_tw_ckbx = new QCheckBox("Twist");
    params_fps_ckbx = new QCheckBox("FPS");
    pgl->addWidget(params_ckbx);
    pgl->addWidget(params_size_ckbx);
    pgl->addWidget(params_center_ckbx);
    pgl->addWidget(params_az_ckbx);
    pgl->addWidget(params_el_ckbx);
    pgl->addWidget(params_tw_ckbx);
    pgl->addWidget(params_fps_ckbx);
    params_grp->setLayout(pgl);

    /* Wire up signals -> view_refresh.
     * QCheckBox::stateChanged(int) works across all Qt5/Qt6 versions.
     * checkStateChanged(Qt::CheckState) was only added in Qt 6.7, so we
     * use the version-specific signal to avoid deprecation warnings when
     * building with a sufficiently new Qt. */
#if QT_VERSION < QT_VERSION_CHECK(6, 7, 0)
    QObject::connect(acsg_ckbx,         &QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(amesh_ckbx,        &QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(adc_ckbx,          &QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(cdot_ckbx,         &QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(grid_ckbx,         &QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(mdlaxes_ckbx,      &QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(scale_ckbx,        &QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(viewaxes_ckbx,     &QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(params_ckbx,       &QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(params_size_ckbx,  &QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(params_center_ckbx,&QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(params_az_ckbx,    &QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(params_el_ckbx,    &QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(params_tw_ckbx,    &QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(params_fps_ckbx,   &QCheckBox::stateChanged, this, &CADViewSettings::view_update_int);
#else
    QObject::connect(acsg_ckbx,         &QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(amesh_ckbx,        &QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(adc_ckbx,          &QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(cdot_ckbx,         &QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(grid_ckbx,         &QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(mdlaxes_ckbx,      &QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(scale_ckbx,        &QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(viewaxes_ckbx,     &QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(params_ckbx,       &QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(params_size_ckbx,  &QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(params_center_ckbx,&QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(params_az_ckbx,    &QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(params_el_ckbx,    &QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(params_tw_ckbx,    &QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
    QObject::connect(params_fps_ckbx,   &QCheckBox::checkStateChanged, this, &CADViewSettings::view_update_int);
#endif
    QObject::connect(fb_mode_combo,
		     QOverload<int>::of(&QComboBox::currentIndexChanged),
		     this, &CADViewSettings::view_update_int);

    /* Assemble the top-level layout */
    wl->addWidget(acsg_ckbx);
    wl->addWidget(amesh_ckbx);
    wl->addWidget(adc_ckbx);
    wl->addWidget(cdot_ckbx);
    wl->addWidget(grid_ckbx);
    wl->addWidget(mdlaxes_ckbx);
    wl->addWidget(scale_ckbx);
    wl->addWidget(viewaxes_ckbx);
    wl->addLayout(fbl);
    wl->addWidget(params_grp);

    wl->addWidget(grid_ckbx);
    ckbx_connect_update(grid_ckbx, this);

    wl->addWidget(mdlaxes_ckbx);
    ckbx_connect_update(mdlaxes_ckbx, this);

    // View Parameters group: overall on/off plus per-element sub-options
    params_grp = new QGroupBox("View Parameters");
    params_ckbx = new QCheckBox("Show Parameters");
    params_size_ckbx = new QCheckBox("Size");
    params_center_ckbx = new QCheckBox("Center");
    params_az_ckbx = new QCheckBox("Azimuth");
    params_el_ckbx = new QCheckBox("Elevation");
    params_tw_ckbx = new QCheckBox("Twist");
    fps_ckbx = new QCheckBox("FPS");

    // Sub-options start disabled; enabled when params is on
    params_size_ckbx->setEnabled(false);
    params_center_ckbx->setEnabled(false);
    params_az_ckbx->setEnabled(false);
    params_el_ckbx->setEnabled(false);
    params_tw_ckbx->setEnabled(false);
    fps_ckbx->setEnabled(false);

    QVBoxLayout *params_vl = new QVBoxLayout;
    params_vl->addWidget(params_ckbx);
    QVBoxLayout *sub_vl = new QVBoxLayout;
    sub_vl->setContentsMargins(16, 0, 0, 0);
    sub_vl->addWidget(params_size_ckbx);
    sub_vl->addWidget(params_center_ckbx);
    sub_vl->addWidget(params_az_ckbx);
    sub_vl->addWidget(params_el_ckbx);
    sub_vl->addWidget(params_tw_ckbx);
    sub_vl->addWidget(fps_ckbx);
    params_vl->addLayout(sub_vl);
    params_grp->setLayout(params_vl);
    wl->addWidget(params_grp);

    ckbx_connect_update(params_ckbx, this);
    ckbx_connect_update(params_size_ckbx, this);
    ckbx_connect_update(params_center_ckbx, this);
    ckbx_connect_update(params_az_ckbx, this);
    ckbx_connect_update(params_el_ckbx, this);
    ckbx_connect_update(params_tw_ckbx, this);
    ckbx_connect_update(fps_ckbx, this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    QObject::connect(params_ckbx, &QCheckBox::checkStateChanged, this, &CADViewSettings::params_state_changed);
#else
    QObject::connect(params_ckbx, &QCheckBox::stateChanged, this, &CADViewSettings::params_state_changed);
#endif

    wl->addWidget(scale_ckbx);
    ckbx_connect_update(scale_ckbx, this);

    wl->addWidget(viewaxes_ckbx);
    ckbx_connect_update(viewaxes_ckbx, this);

    this->setLayout(wl);
}

CADViewSettings::~CADViewSettings()
{
}

void
CADViewSettings::checkbox_update()
{
    checkbox_refresh(0);
}

void
CADViewSettings::view_update()
{
    view_refresh(0);
}

void
CADViewSettings::view_update_int(int)
{
    view_refresh(0);
}

/* Read current view state and update all widgets to match, without
 * triggering a spurious write-back via the signal connections. */
void
CADViewSettings::fb_state_changed(int state)
{
    bool on = (state == Qt::Checked);
    fbo_ckbx->setEnabled(on);
    if (!on)
	fbo_ckbx->setCheckState(Qt::Unchecked);
}

void
CADViewSettings::params_state_changed(int state)
{
    bool on = (state == Qt::Checked);
    params_size_ckbx->setEnabled(on);
    params_center_ckbx->setEnabled(on);
    params_az_ckbx->setEnabled(on);
    params_el_ckbx->setEnabled(on);
    params_tw_ckbx->setEnabled(on);
    fps_ckbx->setEnabled(on);
}

void
CADViewSettings::checkbox_refresh(unsigned long long)
{
    QgModel *m = ((QgEdApp *)qApp)->mdl;
    if (!m)
	return;
    struct ged *gedp = m->gedp;
    if (!gedp)
	return;
    bsg_view *v = gedp->ged_gvp;
    struct bview *v = gedp->ged_gvp;
    if (!v)
	return;

    /* Top-level faceplate elements */
    set_ckbx(acsg_ckbx,     v->gv_s->adaptive_plot_csg);
    set_ckbx(amesh_ckbx,    v->gv_s->adaptive_plot_mesh);
    set_ckbx(adc_ckbx,      v->gv_s->gv_adc.draw);
    set_ckbx(cdot_ckbx,     v->gv_s->gv_center_dot.gos_draw);
    set_ckbx(grid_ckbx,     v->gv_s->gv_grid.draw);
    set_ckbx(mdlaxes_ckbx,  v->gv_s->gv_model_axes.draw);
    set_ckbx(scale_ckbx,    v->gv_s->gv_view_scale.gos_draw);
    set_ckbx(viewaxes_ckbx, v->gv_s->gv_view_axes.draw);

    /* Framebuffer mode (0=off, 1=overlay, 2=underlay) maps directly to
     * combo index. Clamp to a valid range in case of unexpected values. */
    int fb_mode = v->gv_s->gv_fb_mode;
    if (fb_mode < 0 || fb_mode > 2)
	fb_mode = 0;
    fb_mode_combo->blockSignals(true);
    fb_mode_combo->setCurrentIndex(fb_mode);
    fb_mode_combo->blockSignals(false);

    /* Parameters group: master draw toggle + per-element sub-flags */
    struct bv_params_state *pst = &v->gv_s->gv_view_params;
    set_ckbx(params_ckbx,        pst->draw);
    set_ckbx(params_size_ckbx,   pst->draw_size);
    set_ckbx(params_center_ckbx, pst->draw_center);
    set_ckbx(params_az_ckbx,     pst->draw_az);
    set_ckbx(params_el_ckbx,     pst->draw_el);
    set_ckbx(params_tw_ckbx,     pst->draw_tw);
    set_ckbx(params_fps_ckbx,    pst->draw_fps);
}

/* Read all widget states and write them back to the view, then signal
 * that the view needs to be redrawn. */
void
CADViewSettings::view_refresh(unsigned long long)
{
    QgModel *m = ((QgEdApp *)qApp)->mdl;
    if (!m)
	return;
    struct ged *gedp = m->gedp;
    if (!gedp)
	return;
    bsg_view *v = gedp->ged_gvp;
    struct bview *v = gedp->ged_gvp;
    if (!v)
	return;

    /* Top-level faceplate elements */
    v->gv_s->adaptive_plot_csg     = ckbx_val(acsg_ckbx);
    v->gv_s->adaptive_plot_mesh    = ckbx_val(amesh_ckbx);
    v->gv_s->gv_adc.draw           = ckbx_val(adc_ckbx);
    v->gv_s->gv_center_dot.gos_draw = ckbx_val(cdot_ckbx);
    v->gv_s->gv_grid.draw          = ckbx_val(grid_ckbx);
    v->gv_s->gv_model_axes.draw    = ckbx_val(mdlaxes_ckbx);
    v->gv_s->gv_view_scale.gos_draw = ckbx_val(scale_ckbx);
    v->gv_s->gv_view_axes.draw     = ckbx_val(viewaxes_ckbx);

    /* Framebuffer mode: combo index maps directly to gv_fb_mode (0/1/2) */
    v->gv_s->gv_fb_mode = fb_mode_combo->currentIndex();

    /* Parameters: master draw flag + per-element sub-flags */
    struct bv_params_state *pst = &v->gv_s->gv_view_params;
    pst->draw        = ckbx_val(params_ckbx);
    pst->draw_size   = ckbx_val(params_size_ckbx);
    pst->draw_center = ckbx_val(params_center_ckbx);
    pst->draw_az     = ckbx_val(params_az_ckbx);
    pst->draw_el     = ckbx_val(params_el_ckbx);
    pst->draw_tw     = ckbx_val(params_tw_ckbx);
    pst->draw_fps    = ckbx_val(params_fps_ckbx);

    emit settings_changed(QG_VIEW_DRAWN);
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
