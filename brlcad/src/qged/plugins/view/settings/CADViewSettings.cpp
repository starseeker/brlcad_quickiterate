/*             C A D V I E W S E T T I N G S . C P P
 * BRL-CAD
 *
 * Copyright (c) 2023-2025 United States Government as represented by
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
 * Brief description
 *
 */

#include <QVBoxLayout>
#include <QHBoxLayout>

#include "bu/opt.h"
#include "bu/malloc.h"
#include "bu/str.h"
#include "qtcad/QgSignalFlags.h"
#include "QgEdApp.h"

#include "CADViewSettings.h"

/* Helper: connect a checkbox using the signal available in the installed Qt */
static void
ckbx_connect_update(QCheckBox *cb, CADViewSettings *parent)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    QObject::connect(cb, &QCheckBox::checkStateChanged, parent, &CADViewSettings::view_update_int);
#else
    QObject::connect(cb, &QCheckBox::stateChanged, parent, &CADViewSettings::view_update_int);
#endif
}

CADViewSettings::CADViewSettings(QWidget *)
{
    QVBoxLayout *wl = new QVBoxLayout;
    wl->setAlignment(Qt::AlignTop);

    acsg_ckbx = new QCheckBox("Adaptive Plotting (CSG)");
    amesh_ckbx = new QCheckBox("Adaptive Plotting (Mesh)");
    adc_ckbx = new QCheckBox("Angle/Dist. Cursor");
    cdot_ckbx = new QCheckBox("Center Dot");
    grid_ckbx = new QCheckBox("Grid");
    mdlaxes_ckbx = new QCheckBox("Model Axes");
    scale_ckbx = new QCheckBox("Scale");
    viewaxes_ckbx = new QCheckBox("View Axes");

    wl->addWidget(acsg_ckbx);
    ckbx_connect_update(acsg_ckbx, this);

    wl->addWidget(amesh_ckbx);
    ckbx_connect_update(amesh_ckbx, this);

    wl->addWidget(adc_ckbx);
    ckbx_connect_update(adc_ckbx, this);

    wl->addWidget(cdot_ckbx);
    ckbx_connect_update(cdot_ckbx, this);

    // Framebuffer group: FB on/off, with FB Overlay as a sub-option
    fb_ckbx = new QCheckBox("Framebuffer");
    fbo_ckbx = new QCheckBox("FB Overlay");
    fbo_ckbx->setEnabled(false);  // enabled only when Framebuffer is on

    QVBoxLayout *fb_vl = new QVBoxLayout;
    fb_vl->setContentsMargins(0, 0, 0, 0);
    fb_vl->addWidget(fb_ckbx);
    QHBoxLayout *fbo_hl = new QHBoxLayout;
    fbo_hl->setContentsMargins(16, 0, 0, 0);
    fbo_hl->addWidget(fbo_ckbx);
    fb_vl->addLayout(fbo_hl);
    wl->addLayout(fb_vl);

    ckbx_connect_update(fb_ckbx, this);
    ckbx_connect_update(fbo_ckbx, this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    QObject::connect(fb_ckbx, &QCheckBox::checkStateChanged, this, &CADViewSettings::fb_state_changed);
#else
    QObject::connect(fb_ckbx, &QCheckBox::stateChanged, this, &CADViewSettings::fb_state_changed);
#endif

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
    if (!v)
	return;

    acsg_ckbx->blockSignals(true);
    acsg_ckbx->setCheckState(v->gv_s->adaptive_plot_csg ? Qt::Checked : Qt::Unchecked);
    acsg_ckbx->blockSignals(false);

    amesh_ckbx->blockSignals(true);
    amesh_ckbx->setCheckState(v->gv_s->adaptive_plot_mesh ? Qt::Checked : Qt::Unchecked);
    amesh_ckbx->blockSignals(false);

    adc_ckbx->blockSignals(true);
    adc_ckbx->setCheckState(v->gv_s->gv_adc.draw ? Qt::Checked : Qt::Unchecked);
    adc_ckbx->blockSignals(false);

    cdot_ckbx->blockSignals(true);
    cdot_ckbx->setCheckState(v->gv_s->gv_center_dot.gos_draw ? Qt::Checked : Qt::Unchecked);
    cdot_ckbx->blockSignals(false);

    // Framebuffer: update main toggle then enable/disable overlay sub-option
    fb_ckbx->blockSignals(true);
    bool fb_on = (v->gv_s->gv_fb_mode != 0);
    fb_ckbx->setCheckState(fb_on ? Qt::Checked : Qt::Unchecked);
    fb_ckbx->blockSignals(false);

    fbo_ckbx->setEnabled(fb_on);
    fbo_ckbx->blockSignals(true);
    fbo_ckbx->setCheckState((v->gv_s->gv_fb_mode == 1) ? Qt::Checked : Qt::Unchecked);
    fbo_ckbx->blockSignals(false);

    grid_ckbx->blockSignals(true);
    grid_ckbx->setCheckState(v->gv_s->gv_grid.draw ? Qt::Checked : Qt::Unchecked);
    grid_ckbx->blockSignals(false);

    mdlaxes_ckbx->blockSignals(true);
    mdlaxes_ckbx->setCheckState(v->gv_s->gv_model_axes.draw ? Qt::Checked : Qt::Unchecked);
    mdlaxes_ckbx->blockSignals(false);

    struct bv_params_state *pst = &v->gv_s->gv_view_params;

    // Params group: update main toggle and enable/disable sub-options
    params_ckbx->blockSignals(true);
    bool params_on = (pst->draw != 0);
    params_ckbx->setCheckState(params_on ? Qt::Checked : Qt::Unchecked);
    params_ckbx->blockSignals(false);

    params_size_ckbx->setEnabled(params_on);
    params_center_ckbx->setEnabled(params_on);
    params_az_ckbx->setEnabled(params_on);
    params_el_ckbx->setEnabled(params_on);
    params_tw_ckbx->setEnabled(params_on);
    fps_ckbx->setEnabled(params_on);

    params_size_ckbx->blockSignals(true);
    params_size_ckbx->setCheckState(pst->draw_size ? Qt::Checked : Qt::Unchecked);
    params_size_ckbx->blockSignals(false);

    params_center_ckbx->blockSignals(true);
    params_center_ckbx->setCheckState(pst->draw_center ? Qt::Checked : Qt::Unchecked);
    params_center_ckbx->blockSignals(false);

    params_az_ckbx->blockSignals(true);
    params_az_ckbx->setCheckState(pst->draw_az ? Qt::Checked : Qt::Unchecked);
    params_az_ckbx->blockSignals(false);

    params_el_ckbx->blockSignals(true);
    params_el_ckbx->setCheckState(pst->draw_el ? Qt::Checked : Qt::Unchecked);
    params_el_ckbx->blockSignals(false);

    params_tw_ckbx->blockSignals(true);
    params_tw_ckbx->setCheckState(pst->draw_tw ? Qt::Checked : Qt::Unchecked);
    params_tw_ckbx->blockSignals(false);

    fps_ckbx->blockSignals(true);
    fps_ckbx->setCheckState(pst->draw_fps ? Qt::Checked : Qt::Unchecked);
    fps_ckbx->blockSignals(false);

    scale_ckbx->blockSignals(true);
    scale_ckbx->setCheckState(v->gv_s->gv_view_scale.gos_draw ? Qt::Checked : Qt::Unchecked);
    scale_ckbx->blockSignals(false);

    viewaxes_ckbx->blockSignals(true);
    viewaxes_ckbx->setCheckState(v->gv_s->gv_view_axes.draw ? Qt::Checked : Qt::Unchecked);
    viewaxes_ckbx->blockSignals(false);
}

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

    v->gv_s->adaptive_plot_csg = (acsg_ckbx->checkState() == Qt::Checked) ? 1 : 0;
    v->gv_s->adaptive_plot_mesh = (amesh_ckbx->checkState() == Qt::Checked) ? 1 : 0;
    v->gv_s->gv_adc.draw = (adc_ckbx->checkState() == Qt::Checked) ? 1 : 0;
    v->gv_s->gv_center_dot.gos_draw = (cdot_ckbx->checkState() == Qt::Checked) ? 1 : 0;

    if (fb_ckbx->checkState() == Qt::Checked) {
	v->gv_s->gv_fb_mode = (fbo_ckbx->checkState() == Qt::Checked) ? 1 : 2;
    } else {
	v->gv_s->gv_fb_mode = 0;
    }

    v->gv_s->gv_grid.draw = (grid_ckbx->checkState() == Qt::Checked) ? 1 : 0;
    v->gv_s->gv_model_axes.draw = (mdlaxes_ckbx->checkState() == Qt::Checked) ? 1 : 0;

    struct bv_params_state *pst = &v->gv_s->gv_view_params;
    pst->draw = (params_ckbx->checkState() == Qt::Checked) ? 1 : 0;
    pst->draw_size = (params_size_ckbx->checkState() == Qt::Checked) ? 1 : 0;
    pst->draw_center = (params_center_ckbx->checkState() == Qt::Checked) ? 1 : 0;
    pst->draw_az = (params_az_ckbx->checkState() == Qt::Checked) ? 1 : 0;
    pst->draw_el = (params_el_ckbx->checkState() == Qt::Checked) ? 1 : 0;
    pst->draw_tw = (params_tw_ckbx->checkState() == Qt::Checked) ? 1 : 0;
    pst->draw_fps = (fps_ckbx->checkState() == Qt::Checked) ? 1 : 0;

    v->gv_s->gv_view_scale.gos_draw = (scale_ckbx->checkState() == Qt::Checked) ? 1 : 0;
    v->gv_s->gv_view_axes.draw = (viewaxes_ckbx->checkState() == Qt::Checked) ? 1 : 0;

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
