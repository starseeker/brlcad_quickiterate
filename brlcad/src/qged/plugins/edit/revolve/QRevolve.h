/*                       Q R E V O L V E . H
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
/** @file QRevolve.h
 *
 * Phase D6 (drawing_modernization): stub revolve-solid editor that
 * demonstrates the bsg_live_source payload contract for solid-editing plugins.
 *
 * Workflow (same contract as edit/extrude)
 * ----------------------------------------
 * 1. An overlay node (_revolve_edit) is created and a BSG_PL_LIVE payload
 *    is attached via bsg_payload_live_create().
 * 2. The update_cb advances the revision whenever edit parameters change.
 * 3. Payload teardown follows the standard bsg_obj_put → bsg_payload_free
 *    chain, respecting owns_live_ctx.
 */

#ifndef QREVOLVE_H
#define QREVOLVE_H

#include <QLineEdit>
#include <QPushButton>
#include <QGroupBox>
#include "raytrace.h"
#include "qtcad/QgTypes.h"

class QgPluginContext;

class QRevolve : public QWidget
{
    Q_OBJECT

    public:
	QRevolve();
	~QRevolve();

	void setContext(QgPluginContext *ctx) { m_ctx = ctx; }

	/* Revolve angle (degrees) */
	QLineEdit *angle;
	/* Primitive name */
	QLineEdit *revolve_name;
	QPushButton *write_edit;
	QPushButton *reset_values;

    signals:
	void view_updated(QgViewUpdateFlags);

    private slots:
	void read_from_db();
	void write_to_db();
	void update_obj_wireframe();
	void update_viewobj_name(const QString &);

    protected:
	bool eventFilter(QObject *, QEvent *);

    private:
	struct directory *dp = NULL;
	struct rt_revolve_internal rev;
	/* Phase D6: edit-scope overlay node carrying a BSG_PL_LIVE payload */
	struct bsg_node *p = NULL;
	struct bu_vls oname = BU_VLS_INIT_ZERO;
	QgPluginContext *m_ctx = nullptr;

	struct ged *getGed() const;
	struct bsg_view *getView() const;
};

#endif /* QREVOLVE_H */

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
