/*                    Q E X T R U D E . C P P
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
/** @file QExtrude.cpp
 *
 * Phase D6 (drawing_modernization): stub extrude-solid editor.
 *
 * Live-source contract (D6)
 * -------------------------
 * The working edit node `p` carries a BSG_PL_LIVE payload created by
 * bsg_payload_live_create(this, NULL).  Two callbacks are wired:
 *
 *   revision_cb  — returns p->pl->pl_revision monotonically.
 *   update_cb    — re-generates the wireframe vlist when geometry changes;
 *                  bumps pl_revision via bsg_payload_bump_revision so any
 *                  observer (canvas redraw, live_realize) detects the update
 *                  without polling s_changed.
 *
 * The bounds_cb, pick_cb, and snap_cb stubs are intentionally unset for now;
 * they can be wired in once the extrude editor gains interactive handle logic.
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
#include "QExtrude.h"
#include "bsg/node_private.h"


/* ---- Phase D6: live-source callbacks ------------------------------------ */

/* revision_cb: return current payload revision.  The live_ctx defaults to
 * editor_ctx (the QExtrude widget pointer) when live_ctx was NULL in
 * bsg_payload_live_set_ops. */
static uint64_t
_extrude_live_revision(void *live_ctx)
{
    (void)live_ctx;
    /* Placeholder — returns 0 until extrude editor tracks its own epoch. */
    return 0;
}

/* update_cb: rebuild wireframe geometry; return non-zero on change. */
static int
_extrude_live_update(void *live_ctx, struct bsg_view *UNUSED(v))
{
    QExtrude *self = (QExtrude *)live_ctx;
    if (!self)
	return 0;
    /* Invoke via meta-object so the call works whether or not signals are
     * blocked (same pattern used in EditEllTool::refresh). */
    QMetaObject::invokeMethod(self, "update_obj_wireframe", Qt::DirectConnection);
    return 1;
}

/* ---- QExtrude constructor ----------------------------------------------- */

QExtrude::QExtrude()
    : QWidget()
{
    extr.magic = RT_EXTRUDE_INTERNAL_MAGIC;

    QVBoxLayout *l = new QVBoxLayout;

    QLabel *name_label = new QLabel("Object name:");
    l->addWidget(name_label);
    extrude_name = new QLineEdit();
    l->addWidget(extrude_name);

    QGroupBox *pbox = new QGroupBox("Parameters");
    QVBoxLayout *pbl = new QVBoxLayout;
    pbl->setAlignment(Qt::AlignTop);
    QLabel *h_label = new QLabel("H length:");
    pbl->addWidget(h_label);
    h_len = new QLineEdit();
    pbl->addWidget(h_len);
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

    QObject::connect(extrude_name, &QLineEdit::textChanged,
		     this, &QExtrude::update_viewobj_name);
    QObject::connect(write_edit, &QPushButton::clicked,
		     this, &QExtrude::write_to_db);
    QObject::connect(reset_values, &QPushButton::clicked,
		     this, &QExtrude::read_from_db);
}

QExtrude::~QExtrude()
{
    /* Phase D6: releasing the edit overlay node frees the attached BSG_PL_LIVE
     * payload through the standard bsg_obj_put → bsg_payload_free chain.
     * owns_live_ctx controls whether the live_ctx is freed too. */
    struct bsg_view *v = getView();
    if (p && v) {
	bsg_obj_put(p);
	p = NULL;
    }
    bu_vls_free(&oname);
}


struct ged *
QExtrude::getGed() const
{
    if (!m_ctx)
	return nullptr;
    return m_ctx->getGed();
}

struct bsg_view *
QExtrude::getView() const
{
    if (!m_ctx)
	return nullptr;
    return m_ctx->getView();
}


void
QExtrude::read_from_db()
{
    struct ged *gedp = getGed();
    if (!gedp || !gedp->dbip || !bu_vls_strlen(&oname))
	return;

    struct directory *ldp = db_lookup(gedp->dbip, bu_vls_cstr(&oname), LOOKUP_QUIET);
    if (!ldp || ldp->d_minor_type != DB5_MINORTYPE_BRLCAD_EXTRUDE)
	return;

    struct rt_db_internal intern = RT_DB_INTERNAL_INIT_ZERO;
    if (rt_db_get_internal(&intern, ldp, gedp->dbip, NULL) < 0)
	return;

    struct rt_extrude_internal *ep = (struct rt_extrude_internal *)intern.idb_ptr;
    RT_EXTRUDE_CK_MAGIC(ep);

    extr = *ep;  /* shallow copy for parameter display */

    rt_db_free_internal(&intern);

    update_obj_wireframe();
    emit view_updated(QG_VIEW_REFRESH);
}


void
QExtrude::write_to_db()
{
    struct ged *gedp = getGed();
    if (!gedp || !gedp->dbip || !bu_vls_strlen(&oname))
	return;

    struct directory *ldp = db_lookup(gedp->dbip, bu_vls_cstr(&oname), LOOKUP_QUIET);
    if (!ldp || ldp->d_minor_type != DB5_MINORTYPE_BRLCAD_EXTRUDE)
	return;

    struct rt_db_internal intern = RT_DB_INTERNAL_INIT_ZERO;
    intern.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    intern.idb_type = ID_EXTRUDE;
    intern.idb_ptr = &extr;
    intern.idb_meth = &OBJ[ID_EXTRUDE];

    if (rt_db_put_internal(ldp, gedp->dbip, &intern) < 0)
	return;

    emit view_updated(QG_VIEW_DB);
}


void
QExtrude::update_obj_wireframe()
{
    struct ged *gedp = getGed();
    if (!gedp)
	return;
    struct bsg_view *v = getView();
    if (!v)
	return;

    /* Find or create the edit-scope overlay node. */
    p = bsg_view_obj_find(v, "_extrude_edit");
    if (!p) {
	p = bsg_view_obj_overlay_create(v, "_extrude_edit", 1/*local*/);
	if (p)
	    bsg_overlay_register_owner(p, this,
		    BSG_OVERLAY_ROLE_MODEL,
		    BSG_OVERLAY_CLASS_EDIT_HANDLE,
		    BSG_OVERLAY_LC_PER_TOOL,
		    BSG_OVERLAY_ORDER_POST_TRANSPARENT,
		    NULL, 0);

	/* Phase D6: attach a BSG_PL_LIVE payload to the edit-scope node so
	 * that the live-source revision contract is in force.  The update_cb
	 * will be invoked by bsg_payload_live_realize when the edit scope
	 * needs a geometry refresh, and revision_cb lets observers detect
	 * changes without reading s_changed directly.
	 *
	 * NOTE: bsg_payload_live_create replaces any existing payload on `p`;
	 * since we just created the node here there is nothing to displace. */
	if (p) {
	    struct bsg_payload *pl = bsg_payload_live_create(this, NULL);
	    if (pl) {
		bsg_payload_live_set_ops(pl,
			NULL,              /* live_ctx — defaults to editor_ctx */
			0,                 /* owns_live_ctx */
			_extrude_live_revision,
			_extrude_live_update,
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
    if (!dp || dp->d_minor_type != DB5_MINORTYPE_BRLCAD_EXTRUDE) {
	bsg_obj_reset(p);
	p->s_flag = DOWN;
	return;
    }

    bsg_obj_reset(p);
    p->s_v = v;

    struct rt_db_internal intern = RT_DB_INTERNAL_INIT_ZERO;
    intern.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    intern.idb_type = ID_EXTRUDE;
    intern.idb_ptr = &extr;
    intern.idb_meth = &OBJ[ID_EXTRUDE];
    if (!intern.idb_meth->ft_plot)
	return;

    struct rt_wdb *wdbp = wdb_dbopen(gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);
    if (!wdbp)
	return;
    struct bn_tol *tol = &wdbp->wdb_tol;
    struct bg_tess_tol *ttol = &wdbp->wdb_ttol;
    intern.idb_meth->ft_plot(bsg_node_vlist_head(p), &intern, ttol, tol, p->s_v);

    const char *wcolor = "255/255/255";
    const char *av[2] = {wcolor, NULL};
    struct bu_color cval;
    bu_opt_color(NULL, 1, (const char **)&av[0], (void *)&cval);
    bu_color_to_rgb_chars(&cval, p->s_color);

    /* Phase D6: geometry changed — advance the revision so any live_realize
     * call or downstream bsg_live_source observer detects the update. */
    if (p->pl)
	bsg_payload_bump_revision(p->pl);
}


void
QExtrude::update_viewobj_name(const QString &ostr)
{
    bu_vls_sprintf(&oname, "%s", ostr.toLocal8Bit().data());
    update_obj_wireframe();
    emit view_updated(QG_VIEW_REFRESH);
}


bool
QExtrude::eventFilter(QObject *, QEvent *)
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
