/*            Q G T R E E S E L E C T I O N M O D E L . C P P
 * BRL-CAD
 *
 * Copyright (c) 2014-2025 United States Government as represented by
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
/** @file QgTreeSelectionModel.cpp
 *
 */

#include "common.h"
#include <algorithm>
#include <queue>
#include <unordered_set>
#include <stack>
#include <vector>
#include <QGuiApplication>
#include "qtcad/QgUtil.h"
#include "qtcad/QgModel.h"
#include "qtcad/QgTreeSelectionModel.h"
#include "qtcad/QgSignalFlags.h"

#include "../libged/dbi.h"

void
QgTreeSelectionModel::clear_all()
{
    QgModel *m = treeview->m;

    DbiState *dbis = (DbiState *)m->gedp->dbi_state;
    SelectionSet *ss = dbis->get_selection_set(nullptr);
    ss->clear();
}

void
QgTreeSelectionModel::select(const QItemSelection &selection, QItemSelectionModel::SelectionFlags flags)
{
    QTCAD_SLOT("QgTreeSelectionModel::select QItemSelection", 1);
    QgModel *m = treeview->m;
    struct ged *gedp = m->gedp;

    DbiState *dbis = (DbiState *)gedp->dbi_state;
    SelectionSet *ss = dbis->get_selection_set(nullptr);

    // Materialise the index list on the stack to avoid per-element heap
    // allocation; using a local value also fixes the Windows crash described at
    // https://stackoverflow.com/q/15123109/2037687 because the QModelIndexList
    // outlives any internal Qt temporary that selection.indexes() might create.
    QModelIndexList dl = selection.indexes();
    for (const QModelIndex &idx : dl) {
	QgItem *snode = static_cast<QgItem *>(idx.internalPointer());

	// If we are selecting an already selected node, clear it
	if (flags & QItemSelectionModel::Select && ss->is_selected(snode->path_hash())) {
	    if (!(QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))) {
		if (flags & QItemSelectionModel::Clear && ss->selected().size() > 1) {
		    ss->clear();
		    ss->select(DbiPath(snode->path_items()), false);
		} else {
		    ss->deselect(DbiPath(snode->path_items()), false);
		}
	    }
	} else {
	    if (flags & QItemSelectionModel::Clear)
		ss->clear();

	    ss->select(DbiPath(snode->path_items()), false);
	}
    }

    // Done manipulating paths - update metadata
    ss->recompute_hierarchy();

    unsigned long long sflags = QG_VIEW_SELECT;
    if (ss->sync_to_all_views())
	sflags |= QG_VIEW_REFRESH;

    emit treeview->view_changed(sflags);
    emit treeview->m->layoutChanged();
}

void
QgTreeSelectionModel::select(const QModelIndex &index, QItemSelectionModel::SelectionFlags flags)
{
    QTCAD_SLOT("QgTreeSelectionModel::select QModelIndex", 1);
    QgModel *m = treeview->m;
    struct ged *gedp = m->gedp;

    DbiState *dbis = (DbiState *)gedp->dbi_state;
    SelectionSet *ss = dbis->get_selection_set(nullptr);

    if (flags & QItemSelectionModel::Clear)
	ss->clear();

    QgItem *snode = static_cast<QgItem *>(index.internalPointer());
    if (!snode) {
	ss->clear();

	// Done manipulating paths - update metadata
	ss->recompute_hierarchy();

	unsigned long long sflags = QG_VIEW_SELECT;
	if (ss->sync_to_all_views())
	    sflags |= QG_VIEW_REFRESH;

	emit treeview->view_changed(sflags);
	emit treeview->m->layoutChanged();
	return;
    }

    if (!(flags & QItemSelectionModel::Deselect)) {

	// If we are selecting an already selected node, clear it
	if (flags & QItemSelectionModel::Select && ss->is_selected(snode->path_hash())) {
	    ss->deselect(DbiPath(snode->path_items()), false);
	    // Done manipulating paths - update metadata
	    ss->recompute_hierarchy();
	    unsigned long long sflags = QG_VIEW_SELECT;
	    if (ss->sync_to_all_views())
		sflags |= QG_VIEW_REFRESH;
	    emit treeview->view_changed(sflags);
	    emit treeview->m->layoutChanged();
	    return;
	}

	ss->select(DbiPath(snode->path_items()), false);

    } else {

	ss->deselect(DbiPath(snode->path_items()), false);

    }

    // Done manipulating paths - update metadata
    ss->recompute_hierarchy();

    unsigned long long sflags = QG_VIEW_SELECT;
    if (ss->sync_to_all_views())
	sflags |= QG_VIEW_REFRESH;

    emit treeview->view_changed(sflags);
    emit treeview->m->layoutChanged();
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

