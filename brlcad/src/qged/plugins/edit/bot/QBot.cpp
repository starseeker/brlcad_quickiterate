/*                       Q B O T . C P P
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
/** @file QBot.cpp
 *
 * Phase D6 (drawing_modernization): stub BOT editor.
 *
 * Live-source contract (D6)
 * -------------------------
 * The edit-scope overlay node `p` carries a BSG_PL_LIVE payload with:
 *   revision_cb  — placeholder; will return face/vertex-change epoch once
 *                  mesh-editing state is tracked.
 *   update_cb    — rebuilds the wireframe vlist and bumps pl_revision.
 *   pick_cb      — stub for future face/vertex ray-pick selection.
 *
 * bounds_cb and snap_cb are left NULL for the initial stub.
 */

#include "common.h"
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
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
#include "QBot.h"
#include "bsg/node_private.h"


/* ---- Phase D6: live-source callbacks ------------------------------------ */

static uint64_t
_bot_live_revision(void *UNUSED(live_ctx))
{
    /* Placeholder — a real implementation would track the edit epoch. */
    return 0;
}

static int
_bot_live_update(void *live_ctx, struct bsg_view *UNUSED(v))
{
    QBot *self = (QBot *)live_ctx;
    if (!self)
	return 0;
    QMetaObject::invokeMethod(self, "update_obj_wireframe", Qt::DirectConnection);
    return 1;
}

/* pick_cb stub: ray-pick for face / vertex selection.
 * Filled in once the BOT editor gains interactive selection logic. */
static int
_bot_live_pick(void *UNUSED(live_ctx), struct bsg_view *UNUSED(v),
	       int UNUSED(x), int UNUSED(y), void *UNUSED(pick_out))
{
    /* TODO: intersect sample ray against BOT faces/vertices. */
    return 0;
}


/* ---- QBot constructor --------------------------------------------------- */

QBot::QBot()
    : QWidget()
{
    QVBoxLayout *l = new QVBoxLayout;

    QLabel *name_label = new QLabel("Object name:");
    l->addWidget(name_label);
    bot_name = new QLineEdit();
    l->addWidget(bot_name);

    QGroupBox *mode_box = new QGroupBox("Edit mode");
    QVBoxLayout *mbl = new QVBoxLayout;
    edit_mode = new QComboBox();
    edit_mode->addItem("Vertex");
    edit_mode->addItem("Face");
    edit_mode->addItem("Edge");
    mbl->addWidget(edit_mode);
    mode_box->setLayout(mbl);
    l->addWidget(mode_box);

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

    QObject::connect(bot_name, &QLineEdit::textChanged,
		     this, &QBot::update_viewobj_name);
    QObject::connect(write_edit, &QPushButton::clicked,
		     this, &QBot::write_to_db);
    QObject::connect(reset_values, &QPushButton::clicked,
		     this, &QBot::read_from_db);
}

QBot::~QBot()
{
    struct bsg_view *v = getView();
    if (p && v) {
	bsg_obj_put(p);
	p = NULL;
    }
    bu_vls_free(&oname);
}

struct ged *
QBot::getGed() const
{
    if (!m_ctx)
	return nullptr;
    return m_ctx->ged();
}

struct bsg_view *
QBot::getView() const
{
    if (!m_ctx)
	return nullptr;
    return m_ctx->view();
}

void
QBot::read_from_db()
{
    struct ged *gedp = getGed();
    if (!gedp || !gedp->dbip || !bu_vls_strlen(&oname))
	return;

    /* BOT loading is deferred until full edit logic is implemented. */
    update_obj_wireframe();
    emit view_updated(QG_VIEW_REFRESH);
}

void
QBot::write_to_db()
{
    /* BOT writing deferred until full edit logic is implemented. */
    emit view_updated(QG_VIEW_DB);
}

void
QBot::update_obj_wireframe()
{
    struct ged *gedp = getGed();
    if (!gedp)
	return;
    struct bsg_view *v = getView();
    if (!v)
	return;

    p = bsg_view_obj_find(v, "_bot_edit");
    if (!p) {
	p = bsg_view_obj_overlay_create(v, "_bot_edit", 1/*local*/);
	if (p)
	    bsg_overlay_register_owner(p, this,
		    BSG_OVERLAY_ROLE_MODEL,
		    BSG_OVERLAY_CLASS_EDIT_HANDLE,
		    BSG_OVERLAY_LC_PER_TOOL,
		    BSG_OVERLAY_ORDER_POST_TRANSPARENT,
		    NULL, 0);

	/* Phase D6: attach BSG_PL_LIVE payload.
	 * pick_cb is registered as a stub to mark the future extension point
	 * for face/vertex ray-pick selection. */
	if (p) {
	    struct bsg_payload *pl = bsg_payload_live_create(this, NULL);
	    if (pl) {
		bsg_payload_live_set_ops(pl,
			NULL,          /* live_ctx — defaults to editor_ctx */
			0,             /* owns_live_ctx */
			_bot_live_revision,
			_bot_live_update,
			NULL,          /* bounds_cb */
			_bot_live_pick,
			NULL,          /* snap_cb */
			NULL);         /* free_cb */
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
    if (!dp || dp->d_minor_type != DB5_MINORTYPE_BRLCAD_BOT) {
	bsg_obj_reset(p);
	p->s_flag = DOWN;
	return;
    }

    bsg_obj_reset(p);
    p->s_v = v;

    /* Load and plot the BOT geometry. */
    struct rt_db_internal intern = RT_DB_INTERNAL_INIT_ZERO;
    if (rt_db_get_internal(&intern, dp, gedp->dbip, NULL, &rt_uniresource) < 0)
	return;
    if (intern.idb_minor_type != DB5_MINORTYPE_BRLCAD_BOT) {
	rt_db_free_internal(&intern);
	return;
    }

    struct rt_wdb *wdbp = wdb_dbopen(gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);
    if (!wdbp) {
	rt_db_free_internal(&intern);
	return;
    }
    if (intern.idb_meth->ft_plot)
	intern.idb_meth->ft_plot(bsg_node_vlist_head(p), &intern,
				 &wdbp->wdb_ttol, &wdbp->wdb_tol, p->s_v);
    rt_db_free_internal(&intern);

    const char *wcolor = "255/255/255";
    const char *av[2] = {wcolor, NULL};
    struct bu_color cval;
    bu_opt_color(NULL, 1, (const char **)&av[0], (void *)&cval);
    bu_color_to_rgb_chars(&cval, p->s_color);

    if (p->pl)
	bsg_payload_bump_revision(p->pl);
}

void
QBot::update_viewobj_name(const QString &ostr)
{
    bu_vls_sprintf(&oname, "%s", ostr.toLocal8Bit().data());
    update_obj_wireframe();
    emit view_updated(QG_VIEW_REFRESH);
}

bool
QBot::eventFilter(QObject *, QEvent *)
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
