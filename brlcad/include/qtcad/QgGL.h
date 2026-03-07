/*                        Q G G L . H
 * BRL-CAD
 *
 * Copyright (c) 2021-2025 United States Government as represented by
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
/** @file QgGL.h
 *
 */

#ifndef QGGL_H
#define QGGL_H

#include "common.h"

#include <QKeyEvent>
#include <QImage>
#include <QMouseEvent>
#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPainter>
#include <QWheelEvent>
#include <QWidget>

extern "C" {
#include "bu/ptbl.h"
#include "bsg.h"
#define DM_WITH_RT
#include "dm.h"
}

#include "qtcad/defines.h"

// Use QOpenGLFunctions so we don't have to prefix all OpenGL calls with "f->"
class QTCAD_EXPORT QgGL : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

    public:
	explicit QgGL(QWidget *parent = nullptr, struct fb *fbp = nullptr);
	~QgGL();

	void stash_hashes(); // Store current dmp and v hash values
	bool diff_hashes();  // Set dmp dirty flag if current hashes != stashed hashes.  (Does not update stored hash values - use stash_hashes for that operation.)

	void aet(double a, double e, double t);
	void save_image();

	int current = 1;
	bsg_view *v = nullptr;
	struct dm *dmp = nullptr;
	struct fb *ifp = nullptr;
	struct bu_ptbl *dm_set = nullptr;

	void (*draw_custom)(bsg_view *, void *) = nullptr;
	void *draw_udata = nullptr;

	void enableDefaultKeyBindings();
	void disableDefaultKeyBindings();

	void enableDefaultMouseBindings();
	void disableDefaultMouseBindings();

    signals:
	void changed();
	void init_done();

    public slots:
	void need_update();
        void set_lmouse_move_default(int);

    protected:
	void paintGL() override;
	void resizeGL(int w, int h) override;
	void resizeEvent(QResizeEvent *e) override;

	void keyPressEvent(QKeyEvent *k) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;

    private:
	unsigned long long prev_dhash = 0;
	unsigned long long prev_vhash = 0;

	bool use_default_keybindings = true;
	bool use_default_mousebindings = true;
	int lmouse_mode = BSG_SCALE;

	bool m_init = false;
	int x_prev = -INT_MAX;
	int y_prev = -INT_MAX;
	double x_press_pos = -INT_MAX;
	double y_press_pos = -INT_MAX;

	bsg_view *local_v = nullptr;
};

#endif /* QGGL_H */

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

