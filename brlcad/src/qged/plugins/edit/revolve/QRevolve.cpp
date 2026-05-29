/*                   Q R E V O L V E . C P P
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
/** @file QRevolve.cpp
 *
 * Phase D6 (drawing_modernization): stub revolve-solid editor.
 *
 * Live-source contract (D6) — identical pattern to edit/extrude
 * -------------------------------------------------------------
 * The working edit node `p` carries a BSG_PL_LIVE payload created by
 * bsg_payload_live_create(this, NULL) with revision_cb and update_cb wired.
 * bounds_cb / pick_cb / snap_cb are left NULL until interactive handles are
 * added to the revolve editor.
 */

#include "common.h"
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QGroupBox>
#include <QVBoxLayout>
#include "ged.h"
#include "rt/db_io.h"
#include "rt/directory.h"
#include "bsg/draw_source.h"
#include "bsg/overlay.h"
#include "bsg/payload.h"
#include "bsg/payload_typed.h"
#include "qtcad/QgPluginContext.h"
#include "qtcad/QgSignalFlags.h"
#include "ged/bsg_ged_draw.h"
#include "QRevolve.h"


/* ---- Phase D6: live-source callbacks ------------------------------------ */

static uint64_t
_revolve_live_revision(void *UNUSED(live_ctx))
{
    return 0;
}

static int
_revolve_live_update(void *live_ctx, struct bsg_view *UNUSED(v))
{
    QRevolve *self = (QRevolve *)live_ctx;
    if (!self)
	return 0;
    QMetaObject::invokeMethod(self, "update_obj_wireframe", Qt::DirectConnection);
    return 1;
}

/* ---- QRevolve constructor ----------------------------------------------- */

QRevolve::QRevolve()
    : QWidget()
{
    rev.magic = RT_REVOLVE_INTERNAL_MAGIC;

    QVBoxLayout *l = new QVBoxLayout;

    QLabel *name_label = new QLabel("Object name:");
    l->addWidget(name_label);
    revolve_name = new QLineEdit();
    l->addWidget(revolve_name);

    QGroupBox *pbox = new QGroupBox("Parameters");
    QVBoxLayout *pbl = new QVBoxLayout;
    pbl->setAlignment(Qt::AlignTop);
    QLabel *a_label = new QLabel("Angle (deg):");
    pbl->addWidget(a_label);
    angle = new QLineEdit();
    pbl->addWidget(angle);
    pbox->setLayout(pbl);
    l->addWidget(pbox);

    QGroupBox *ac_box = new QGroupBox("Actions");
    QVBoxLayout *acl = new QVBoxLayout;
    write_edit = new QPushButton("Apply");
    acl->addWidget(write_edit);
    reset_values = new QPushButton("Reset");
    acl->addWidget(reset_values);
    ac_box->setLayout(acl);
    l->addWidget(ac_box);

    l->setAlignment(Qt::AlignTop);
    this->setLayout(l);

    QObject::connect(revolve_name, &QLineEdit::textChanged,
		     this, &QRevolve::update_viewobj_name);
    QObject::connect(write_edit, &QPushButton::clicked,
		     this, &QRevolve::write_to_db);
    QObject::connect(reset_values, &QPushButton::clicked,
		     this, &QRevolve::read_from_db);
}

QRevolve::~QRevolve()
{
    struct bsg_view *v = getView();
    if (p && v) {
	bsg_obj_put(p);
	p = NULL;
    }
    bu_vls_free(&oname);
}

struct ged *
QRevolve::getGed() const
{
    if (!m_ctx)
	return nullptr;
    return m_ctx->ged();
}

struct bsg_view *
QRevolve::getView() const
{
    if (!m_ctx)
	return nullptr;
    return m_ctx->view();
}

void
QRevolve::read_from_db()
{
    struct ged *gedp = getGed();
    if (!gedp || !gedp->dbip || !bu_vls_strlen(&oname))
	return;

    struct directory *ldp = db_lookup(gedp->dbip, bu_vls_cstr(&oname), LOOKUP_QUIET);
    if (!ldp || ldp->d_minor_type != DB5_MINORTYPE_BRLCAD_REVOLVE)
	return;

    struct rt_db_internal intern = RT_DB_INTERNAL_INIT_ZERO;
    if (rt_db_get_internal(&intern, ldp, gedp->dbip, NULL, &rt_uniresource) < 0)
	return;

    struct rt_revolve_internal *rp = (struct rt_revolve_internal *)intern.idb_ptr;
    RT_REVOLVE_CK_MAGIC(rp);

    rev = *rp;

    rt_db_free_internal(&intern);
    update_obj_wireframe();
    emit view_updated(QG_VIEW_REFRESH);
}

void
QRevolve::write_to_db()
{
    struct ged *gedp = getGed();
    if (!gedp || !gedp->dbip || !bu_vls_strlen(&oname))
	return;

    struct directory *ldp = db_lookup(gedp->dbip, bu_vls_cstr(&oname), LOOKUP_QUIET);
    if (!ldp || ldp->d_minor_type != DB5_MINORTYPE_BRLCAD_REVOLVE)
	return;

    struct rt_db_internal intern = RT_DB_INTERNAL_INIT_ZERO;
    intern.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    intern.idb_type = ID_REVOLVE;
    intern.idb_ptr = &rev;
    intern.idb_meth = &OBJ[ID_REVOLVE];

    if (rt_db_put_internal(ldp, gedp->dbip, &intern, &rt_uniresource) < 0)
	return;

    emit view_updated(QG_VIEW_DB);
}

void
QRevolve::update_obj_wireframe()
{
    struct ged *gedp = getGed();
    if (!gedp)
	return;
    struct bsg_view *v = getView();
    if (!v)
	return;

    p = bsg_view_obj_find(v, "_revolve_edit");
    if (!p) {
	p = bsg_view_obj_overlay_create(v, "_revolve_edit", 1/*local*/);
	if (p)
	    bsg_overlay_register_owner(p, this,
		    BSG_OVERLAY_ROLE_MODEL,
		    BSG_OVERLAY_CLASS_EDIT_HANDLE,
		    BSG_OVERLAY_LC_PER_TOOL,
		    BSG_OVERLAY_ORDER_POST_TRANSPARENT,
		    NULL, 0);

	/* Phase D6: attach BSG_PL_LIVE payload for live-source contract. */
	if (p) {
	    struct bsg_payload *pl = bsg_payload_live_create(this, NULL);
	    if (pl) {
		bsg_payload_live_set_ops(pl,
			NULL,              /* live_ctx — defaults to editor_ctx */
			0,                 /* owns_live_ctx */
			_revolve_live_revision,
			_revolve_live_update,
			NULL, NULL, NULL, NULL);
		bsg_node_set_payload(p, pl);
	    }
	}
    }
    if (!p)
	return;

    if (!gedp->dbip || !bu_vls_strlen(&oname)) {
	bsg_obj_reset(p);
	p->s_flag = DOWN;
	return;
    }

    dp = db_lookup(gedp->dbip, bu_vls_cstr(&oname), LOOKUP_QUIET);
    if (!dp || dp->d_minor_type != DB5_MINORTYPE_BRLCAD_REVOLVE) {
	bsg_obj_reset(p);
	p->s_flag = DOWN;
	return;
    }

    bsg_obj_reset(p);
    p->s_v = v;

    struct rt_db_internal intern = RT_DB_INTERNAL_INIT_ZERO;
    intern.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    intern.idb_type = ID_REVOLVE;
    intern.idb_ptr = &rev;
    intern.idb_meth = &OBJ[ID_REVOLVE];
    if (!intern.idb_meth->ft_plot)
	return;

    struct rt_wdb *wdbp = wdb_dbopen(gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);
    if (!wdbp)
	return;
    intern.idb_meth->ft_plot(bsg_node_vlist_head(p), &intern,
			     &wdbp->wdb_ttol, &wdbp->wdb_tol, p->s_v);

    const char *wcolor = "255/255/255";
    const char *av[2] = {wcolor, NULL};
    struct bu_color cval;
    bu_opt_color(NULL, 1, (const char **)&av[0], (void *)&cval);
    bu_color_to_rgb_chars(&cval, p->s_color);

    if (p->pl)
	bsg_payload_bump_revision(p->pl);
}

void
QRevolve::update_viewobj_name(const QString &ostr)
{
    bu_vls_sprintf(&oname, "%s", ostr.toLocal8Bit().data());
    update_obj_wireframe();
    emit view_updated(QG_VIEW_REFRESH);
}

bool
QRevolve::eventFilter(QObject *, QEvent *)
{
    return false;
}

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
