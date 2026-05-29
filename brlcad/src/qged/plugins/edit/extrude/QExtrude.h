/*                      Q E X T R U D E . H
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
/** @file QExtrude.h
 *
 * Phase D6 (drawing_modernization): stub extrude-solid editor that
 * demonstrates the bsg_live_source payload contract for solid-editing plugins.
 *
 * Workflow
 * --------
 * 1. When an extrusion is selected for editing the widget creates an overlay
 *    view node (_extrude_edit) and attaches a BSG_PL_LIVE payload via
 *    bsg_payload_live_create().
 * 2. The update_cb advances the revision whenever the edit parameters change
 *    so that downstream renderers can detect the modification without polling
 *    s_changed directly.
 * 3. On deselection / destructor the live payload is freed through
 *    bsg_obj_put → bsg_payload_free, which honours owns_live_ctx.
 */

#ifndef QEXTRUDE_H
#define QEXTRUDE_H

#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QGroupBox>
#include "raytrace.h"
#include "qtcad/QgTypes.h"

class QgPluginContext;

class QExtrude : public QWidget
{
    Q_OBJECT

    public:
	QExtrude();
	~QExtrude();

	void setContext(QgPluginContext *ctx) { m_ctx = ctx; }

	/* Extrusion direction length */
	QLineEdit *h_len;
	/* Primitive name */
	QLineEdit *extrude_name;
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
	struct rt_extrude_internal extr;
	/* Phase D6: edit-scope overlay node carrying a BSG_PL_LIVE payload */
	struct bsg_node *p = NULL;
	struct bu_vls oname = BU_VLS_INIT_ZERO;
	QgPluginContext *m_ctx = nullptr;

	struct ged *getGed() const;
	struct bsg_view *getView() const;
};

#endif /* QEXTRUDE_H */

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
