/*               C A D V I E W S E T T I N G S . H
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
/** @file CADViewSettings.h
 *
 * Widget for controlling bview faceplate display settings.
 * FPS and other per-parameter flags are sub-options nested
 * under the main Parameters checkbox; FB Overlay is nested
 * under the Framebuffer checkbox.
 *
 */

#include <QWidget>
#include <QGroupBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include "bsg/defines.h"

class CADViewSettings : public QWidget
{
    Q_OBJECT

    public:
	CADViewSettings(QWidget *p = 0);
	~CADViewSettings();

	// Top-level toggles
	QCheckBox *acsg_ckbx;
	QCheckBox *amesh_ckbx;
	QCheckBox *adc_ckbx;
	QCheckBox *cdot_ckbx;
	QCheckBox *fb_ckbx;
	QCheckBox *fbo_ckbx;    // sub-option: only enabled when fb_ckbx is on
	QCheckBox *grid_ckbx;
	QCheckBox *mdlaxes_ckbx;
	QCheckBox *scale_ckbx;
	QCheckBox *viewaxes_ckbx;

	// View parameters group
	QGroupBox *params_grp;
	QCheckBox *params_ckbx;     // overall params on/off
	QCheckBox *params_size_ckbx;
	QCheckBox *params_center_ckbx;
	QCheckBox *params_az_ckbx;
	QCheckBox *params_el_ckbx;
	QCheckBox *params_tw_ckbx;
	QCheckBox *fps_ckbx;        // sub-option of params

    signals:
	void settings_changed(unsigned long long);

    public slots:
	void checkbox_refresh(unsigned long long);
	void checkbox_update();
	void view_refresh(unsigned long long);
	void view_update_int(int);
	void view_update();
	void fb_state_changed(int);
	void params_state_changed(int);
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
