/*                         Q G M O D E L . C P P
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
/** @file QgModel.cpp
 *
 * QAbstractItemModel of a BRL-CAD .g database.
 *
 *
 * We make instances from the tops objects, as well as all immediate children
 * of all combs, sketches, etc.  We store these in an unordered map with their
 * hash as the key.  The old and new tops lists become the seed vectors used to
 * check what has changed in the tree.  QgItems will store hashes to their
 * instances, and we can tell via a failed lookup whether a particular instance
 * in the QgItem is still valid.
 *
 * QgItems form the explicit hierarchical representation implied by the instances,
 * which means instance changes have potentially global impacts on the QgItems
 * hierarchy and it must be fully checked for validity (and repaired as
 * appropriate) after each cycle.  A validity check would be something like:
 * 1) check the hash of the QgItem against the DbiState sets to see if it is
 * still valid.  If not, it must be either removed or replaced
 */

#include "common.h"

#include <algorithm>
#include <set>
#include <unordered_set>
#include <vector>
#include <QAction>
#include <QFileInfo>

#include "bu/env.h"
#include "bu/hash.h"
#include "bu/sort.h"
#include "bsg/lod.h"
#include "bsg/util.h"
#include "raytrace.h"
#define ALPHANUM_IMPL
#include "../libged/alphanum.h"

// QgModel uses the DBI layer (dbi.h) from libged as the authoritative
// source of truth for the database hierarchy.  DbiState, DrawList, and
// SelectionSet provide explicit, hash-keyed representations of comb
// instances, draw state, and selection state that are suitable for
// backing a QAbstractItemModel without requiring extra copies of librt
// data structures.
#include "../libged/dbi.h"


#include "qtcad/QgModel.h"
#include "qtcad/QgUtil.h"
#include "qtcad/QgSignalFlags.h"

struct QgItem_cmp {
    inline bool operator() (const QgItem *i1, const QgItem *i2)
    {
	if (!i1 && !i2)
	    return false;
	if (!i1 && i2)
	    return true;
	if (i1 && !i2)
	    return false;
	if (!i1->ihash && !i2->ihash)
	    return false;
	if (!i1->ihash && i2->ihash)
	    return true;
	if (i1->ihash && !i2->ihash)
	    return false;

	struct directory *inst1 = nullptr;
	struct directory *inst2 = nullptr;
	DbiState *ctx1 = (DbiState *)i1->mdl->gedp->dbi_state;
	DbiState *ctx2 = (DbiState *)i2->mdl->gedp->dbi_state;
	if (ctx1->d_map.find(i1->ihash) != ctx1->d_map.end()) {
	    inst1 = ctx1->d_map[i1->ihash];
	}
	if (ctx2->d_map.find(i2->ihash) != ctx2->d_map.end()) {
	    inst2 = ctx2->d_map[i2->ihash];
	}

	if (!inst1 && !inst2)
	    return false;
	if (!inst1 && inst2)
	    return true;
	if (inst1 && !inst2)
	    return false;

	const char *n1 = inst1->d_namep;
	const char *n2 = inst2->d_namep;
	if (alphanum_impl(n1, n2, nullptr) < 0)
	    return true;

	return false;
    }
};

QgItem::QgItem(unsigned long long hash, QgModel *ictx)
{
    mdl = ictx;
    DbiState *ctx = (DbiState *)mdl->gedp->dbi_state;
    ihash = hash;
    parentItem = nullptr;
    if (!ctx)
	return;

    // Get the child count from the .g info
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    pc_it = ctx->p_c.find(ihash);
    if (pc_it != ctx->p_c.end()) {
	c_count = pc_it->second.size();
    } else {
	c_count = 0;
    }

    // Local item information
    ctx->print_hash(&name, ihash);
    dp = ctx->get_hdp(ihash);
    icon = QgIcon(dp, ictx->gedp->dbip);
}

QgItem::~QgItem()
{
    bu_vls_free(&name);
}

void
QgItem::open()
{
    if (!mdl)
	return;
    open_itm = true;
    QModelIndex ind = mdl->NodeIndex(this);
    mdl->fetchMore(ind);
}


void
QgItem::close()
{
    open_itm = false;
}

void
QgItem::appendChild(QgItem *c)
{
    children.push_back(c);
}

QgItem *
QgItem::child(int n)
{
    if (n < 0 || n >= (int)children.size())
	return nullptr;

    return children[n];
}

int
QgItem::childCount() const
{
    if (!mdl)
	return 0;

    // Return the count of actually-loaded children for all items.
    // For the root item this is always all top-level items; for other items
    // it is 0 until fetchMore() has been called.  This is the standard Qt
    // lazy-loading pattern: rowCount() reflects loaded rows, and
    // canFetchMore() signals whether more rows are available.
    return (int)children.size();
}

int
QgItem::columnCount() const
{
    return 1;
}

QgItem *
QgItem::parent()
{
    return parentItem;
}

int
QgItem::childNumber() const
{
    if (parentItem) {
	for (size_t i = 0; i < parentItem->children.size(); i++) {
	    if (parentItem->children[i] == this)
		return i;
	}
	bu_log("WARNING - invalid parent/child inquiry\n");
    }

    return 0;
}

std::vector<unsigned long long>
QgItem::path_items()
{
    std::vector<unsigned long long> pitems;
    QgItem *citem = this;
    pitems.push_back(this->ihash);
    while (citem->parent()) {
	citem = citem->parent();
	if (citem->ihash)
	    pitems.push_back(citem->ihash);
    }
    std::reverse(pitems.begin(), pitems.end());
    return pitems;
}

unsigned long long
QgItem::path_hash()
{
    std::vector<unsigned long long> pitems = path_items();
    DbiState *dbis = (DbiState *)mdl->gedp->dbi_state;
    unsigned long long phash = dbis->path_hash(pitems, 0);
    return phash;
}

extern "C" void
qgmodel_update_nref_callback(struct db_i *UNUSED(dbip), struct directory *parent_dp, struct directory *child_dp, const char *child_name, db_op_t op, matp_t m, void *u_data)
{
    // If all the input parameters other than dbip/op are NULL and op is set to
    // subtraction, the update_nref logic has completed its work and is making
    // its final callback call.  Because in this use case we need a fully
    // updated data state before doing our processing (as opposed to, for
    // example, triggering events during the update treewalk) we only process
    // when the termination conditions are fully set.
    if (!parent_dp && !child_dp && !child_name && m == nullptr && op == DB_OP_SUBTRACT) {

	std::cout << "update nref callback\n";

	// Cycle complete, nref count is current.  Start analyzing.
	QgModel *ctx = (QgModel *)u_data;

	// If anybody was requesting an nref update, the fact that we're here
	// means we've done it.
	ctx->need_update_nref = false;
    }
}

extern "C" void
qgmodel_changed_callback(struct db_i *UNUSED(dbip), struct directory *dp, int mode, void *u_data)
{
    unsigned long long hash;
    QgModel *mdl = (QgModel *)u_data;
    DbiState *ctx = (DbiState *)mdl->gedp->dbi_state;
    mdl->need_update_nref = true;
    mdl->changed_db_flag = 1;

    // Clear cached GED drawing data and update
    ctx->clear_cache(dp);

    // Need to invalidate any LoD caches associated with this dp
    if (dp->d_minor_type == DB5_MINORTYPE_BRLCAD_BOT && ctx->gedp) {
	unsigned long long key = bsg_mesh_lod_key_get(ctx->gedp->ged_lod, dp->d_namep);
	if (key) {
	    bsg_mesh_lod_clear_cache(ctx->gedp->ged_lod, key);
	    bsg_mesh_lod_key_put(ctx->gedp->ged_lod, dp->d_namep, 0);
	}
    }

    switch(mode) {
	case 0:
	    ctx->changed.insert(dp);
	    break;
	case 1:
	    ctx->added.insert(dp);
	    break;
	case 2:
	    // When this callback is made, dp is still valid, but in subsequent
	    // processing it will not be.  We need to capture everything we
	    // will need from this dp now, for later use when updating state
	    hash = bu_data_hash(dp->d_namep, strlen(dp->d_namep)*sizeof(char));
	    ctx->removed.insert(hash);
	    ctx->old_names[hash] = std::string(dp->d_namep);
	    break;
	default:
	    bu_log("changed callback mode error: %d\n", mode);
    }
}


QgModel::QgModel(QObject *p, const char *npath)
: QAbstractItemModel(p)
{
    // There are commands such as open that we want to work even without
    // a database instance opened - rather than using ged_open to create
    // a gedp, be sure one is always available - callers should use the
    // libged "open" command to open a .g file.
    BU_GET(gedp, struct ged);
    ged_init(gedp);

    // Set up new cmd data (not yet done by default in ged_init
    gedp->dbi_state = new DbiState(gedp);
    gedp->new_cmd_forms = 1;
    bu_setenv("DM_SWRAST", "1", 1);

    // Register as DbiState observer so we receive change notifications
    {
	DbiState *dbis = (DbiState *)gedp->dbi_state;
	dbis->add_observer(this);
	observed_dbi_state_ = dbis;
    }

    // By default we will use this built-in view, to guarantee that ged_gvp is
    // always valid.  It will usually be overridden by application provided views,
    // but this is our hard guarantee that a QgModel will always be able to work
    // with commands needing a view.
    BU_GET(empty_gvp, bsg_view);
    bsg_view_init(empty_gvp, &gedp->ged_views);
    bsg_scene_root_create(empty_gvp);
    bsg_scene_add_view(&gedp->ged_views, empty_gvp);
    gedp->ged_gvp = empty_gvp;
    bu_vls_sprintf(&gedp->ged_gvp->gv_name, "default");
    gedp->ged_gvp->independent = 0;

    // Set up the root item
    rootItem = new QgItem(0, this);
    rootItem->mdl = this;

    items = new std::unordered_set<QgItem *>;
    items->clear();


    // If there's no path, at least for the moment we have nothing to model.
    // In the future we may want to consider setting up a default environment
    // backed by a temp file...
    if (!npath)
	return;

    if (npath) {
	int ac = 2;
	const char *av[3];
	av[0] = "open";
	av[1] = npath;
	av[2] = nullptr;
	run_cmd(gedp->ged_result_str, ac, (const char **)av);
    }
}

QgModel::~QgModel()
{
    if (gedp && gedp->dbi_state) {
	DbiState *dbis = (DbiState *)gedp->dbi_state;
	dbis->remove_observer(this);
	observed_dbi_state_ = nullptr;
    }

    delete items;

    bsg_view_free(empty_gvp);
    BU_PUT(empty_gvp, bsg_view);
    ged_close(gedp);
    delete rootItem;
}

void
QgModel::on_dbi_changed(const std::vector<DbiChangeEvent> &events)
{
    if (events.empty()) return;
    // Collect events.  g_update() reads pending_dbi_events_ after calling
    // dbis->update() to decide between targeted and full-reset paths.
    // If we are not currently in g_update() (e.g., some future async path
    // calls dbis->update() directly), fall through to full g_update().
    if (in_g_update_) {
	// Normal path: just store the events; g_update() will process them.
	pending_dbi_events_.insert(pending_dbi_events_.end(),
	                            events.begin(), events.end());
	return;
    }
    // Called outside g_update(): store events, mark db changed, then delegate.
    // The changed_db_flag ensures g_update() processes the update even if
    // the pending_dbi_events_ accumulated here are the only signal.
    pending_dbi_events_.insert(pending_dbi_events_.end(),
	                        events.begin(), events.end());
    changed_db_flag = 1;
    g_update(gedp ? gedp->dbip : nullptr);
}

// Note - this is a private method and must be run from within g_update's
// begin/end RebuildModel block.  Also, it assumes all invalid QgItem pointers
// have been removed from the children arrays. (The ability to recognize an
// invalid QgItem is defined at that level - we don't want to re-create it
// here.)
void
QgModel::item_rebuild(QgItem *item)
{
    // Top level is a special case and is handled separately
    if (item == rootItem) {
	return;
    }

    unsigned long long chash = item->ihash;
    DbiState *dbis = (DbiState *)gedp->dbi_state;
    if (dbis->p_v.find(chash) == dbis->p_v.end()) {
	bu_log("QgModel::item_rebuild: invalid hash 0x%llx - item may have been removed from the database\n", (unsigned long long)chash);
	return;
    }

    // Get the current child instances
    std::vector<unsigned long long> &nh = dbis->p_v[chash];

    // If we have no cached children, update only the child count - fetchMore
    // will populate it if and when it is expanded, and there will be no
    // QgItems we need to reuse.  However, an edit operation may have
    // invalidated the original c_count stored when the item was created.
    if (!item->children.size()) {
	item->c_count = dbis->p_v[chash].size();
	return;
    }

    std::unordered_map<unsigned long long, QgItem *> oc;
    for (size_t i = 0; i < item->children.size(); i++) {
	QgItem *qii = item->children[i];
	oc[qii->ihash] = qii;
    }

    // Iterate the instance's array of child hashes, building up the
    // corresponding QgItems array using either the stored QgItems from the
    // previous state or new items.
    std::vector<QgItem *> nc;
    std::vector<unsigned long long>::reverse_iterator nh_it;
    for (nh_it = nh.rbegin(); nh_it != nh.rend(); nh_it++) {
	// For each new child, look up its instance in the original data to see
	// if we have a corresponding QgItem available.
	if (oc.find(*nh_it) != oc.end()) {
	    // We have a viable QgItem from the previous state - reuse it
	    nc.push_back(oc[*nh_it]);
	} else {
	    // Previous tree did not have an appropriate QgItem -
	    // make a new one
	    QgItem *nitem = new QgItem(*nh_it, this);
	    nitem->parentItem = item;
	    nitem->op = dbis->bool_op(item->ihash, *nh_it);
	    nc.push_back(nitem);
	    items->insert(nitem);
	}
    }
    item->c_count = nc.size();

    // If anything changed since the last time children was built, we need to
    // replace children's contents with the new vector.  This is being run
    // inside g_update's beginRebuildModel block, so we don't need to try and
    // manage individual rows here.
     if (nc != item->children) {
	item->children = nc;
	// define a map for quick QgItem * -> index lookups
	item->c_noderow.clear();
	for (size_t i = 0; i < nc.size(); i++) {
	    item->c_noderow[nc[i]] = i;
	}
    }
}


/* full_model_reset: complete beginResetModel/endResetModel cycle.
 * Assumes dbis->update() has already been called. */
void
QgModel::full_model_reset(DbiState *dbis)
{
    beginResetModel();

    // Clear out any QgItems with invalid info.  We need to be fairly
    // aggressive here - first we find all the existing invalid ones, and
    // then we invalidate all their children recursively - any item with an
    // invalid parent is invalid.
    std::queue<QgItem *> to_clear;
    std::unordered_set<QgItem *> invalid;
    std::unordered_set<QgItem *>::iterator s_it;
    for (s_it = items->begin(); s_it != items->end(); s_it++) {
	QgItem *itm = *s_it;
	if (dbis->p_v.find(itm->ihash) == dbis->p_v.end() &&
		dbis->d_map.find(itm->ihash) == dbis->d_map.end())
	    to_clear.push(itm);
	if (!itm->dp && dbis->get_hdp(itm->ihash))
	    to_clear.push(itm);
    }
    while (!to_clear.empty()) {
	QgItem *i_itm = to_clear.front();
	to_clear.pop();
	invalid.insert(i_itm);
	for (size_t i = 0; i < i_itm->children.size(); i++)
	    to_clear.push(i_itm->children[i]);
    }

    for (s_it = items->begin(); s_it != items->end(); s_it++) {
	if (invalid.find(*s_it) != invalid.end())
	    continue;
	QgItem *i_itm = *s_it;
	// Remove any invalid QgItem references from the children arrays
	std::vector<QgItem *> vchildren;
	for (size_t i = 0; i < i_itm->children.size(); i++) {
	    QgItem *itm = i_itm->children[i];
	    if (invalid.find(itm) == invalid.end()) {
		// Valid - keep it
		vchildren.push_back(itm);
	    }
	}
	i_itm->children = vchildren;
	// Child QgItem pointers are now all valid - rebuild full children
	// array to match current .g state
	item_rebuild(i_itm);
    }

    // Validate existing tops QgItems based on the tops data.
    std::vector<unsigned long long> tops = dbis->tops(true);
    std::unordered_set<unsigned long long> tset(tops.begin(), tops.end());
    std::unordered_map<unsigned long long, QgItem *> vtops_items;
    for (size_t i = 0; i < tops_items.size(); i++) {
	QgItem *titem = tops_items[i];
	if (tset.find(titem->ihash) != tset.end()) {
	    // Still a tops item
	    vtops_items[titem->ihash] = titem;
	}
    }

    // Using tops, construct a new tops vector.  Reuse any still valid
    // QgItems, and make new ones.
    std::vector<QgItem *> ntops_items;

    for (size_t i = 0; i < tops.size(); i++) {
	std::unordered_map<unsigned long long, QgItem *>::iterator v_it;
	v_it = vtops_items.find(tops[i]);
	if (v_it != vtops_items.end()) {
	    ntops_items.push_back(v_it->second);
	} else {
	    QgItem *nitem = new QgItem(tops[i], this);
	    nitem->parentItem = rootItem;
	    nitem->op = dbis->bool_op(0, tops[i]);
	    ntops_items.push_back(nitem);
	    items->insert(nitem);
	}
    }

    // Set the new tops items as children of the rootItem.
    std::sort(ntops_items.begin(), ntops_items.end(), QgItem_cmp());
    tops_items = ntops_items;
    rootItem->children.clear();
    for (size_t i = 0; i < tops_items.size(); i++) {
	rootItem->appendChild(tops_items[i]);
    }
    rootItem->c_noderow.clear();
    for (size_t i = 0; i < tops_items.size(); i++) {
	rootItem->c_noderow[tops_items[i]] = i;
    }

    // Finally, delete the invalid QgItems
    std::unordered_set<QgItem *>::iterator iv_it;
    for (iv_it = invalid.begin(); iv_it != invalid.end(); iv_it++) {
	QgItem *iv_itm = *iv_it;
	items->erase(iv_itm);
	delete iv_itm;
    }

    endResetModel();
}


/* reconcile_tops: compare tops_items against dbis->tops() and emit per-row
 * beginInsertRows/endInsertRows or beginRemoveRows/endRemoveRows signals.
 * Preserves expanded state for unchanged rows. */
void
QgModel::reconcile_tops(DbiState *dbis)
{
    std::vector<unsigned long long> new_tops = dbis->tops(true);
    std::unordered_set<unsigned long long> new_tops_set(new_tops.begin(), new_tops.end());

    // Remove items that are no longer at tops (iterate backwards for stable indices)
    for (int i = (int)tops_items.size() - 1; i >= 0; i--) {
	if (new_tops_set.count(tops_items[i]->ihash)) continue;
	beginRemoveRows(QModelIndex(), i, i);
	QgItem *to_del = tops_items[i];
	tops_items.erase(tops_items.begin() + i);
	rootItem->children.erase(rootItem->children.begin() + i);
	endRemoveRows();
	items->erase(to_del);
	delete to_del;
    }
    // Rebuild c_noderow after all removals
    rootItem->c_noderow.clear();
    for (size_t i = 0; i < tops_items.size(); i++)
	rootItem->c_noderow[tops_items[i]] = (int)i;

    // Collect the set of hashes already in tops_items after removals
    std::unordered_set<unsigned long long> existing_set;
    for (auto *ti : tops_items) existing_set.insert(ti->ihash);

    // Insert new items one at a time to keep rootItem->children in sync.
    // Using individual beginInsertRows/endInsertRows per item (sorted order)
    // is correct and avoids layoutAboutToBeChanged/layoutChanged complications.
    for (auto h : new_tops) {
	if (existing_set.count(h)) continue;

	QgItem *nitem = new QgItem(h, this);
	nitem->parentItem = rootItem;
	nitem->op = dbis->bool_op(0, h);
	items->insert(nitem);

	// Append to tops_items then sort to find the insertion position
	tops_items.push_back(nitem);
	std::sort(tops_items.begin(), tops_items.end(), QgItem_cmp());
	int ins_pos = 0;
	for (int j = 0; j < (int)tops_items.size(); j++) {
	    if (tops_items[j] == nitem) { ins_pos = j; break; }
	}

	// Insert into rootItem->children at the precise sorted position and
	// emit the corresponding row-level signal.  The children vector must
	// be updated INSIDE the begin/end bracket so that rowCount() returns
	// the updated value when endInsertRows() fires the signal.
	beginInsertRows(QModelIndex(), ins_pos, ins_pos);
	rootItem->children.insert(rootItem->children.begin() + ins_pos, nitem);
	rootItem->c_noderow.clear();
	for (size_t j = 0; j < tops_items.size(); j++)
	    rootItem->c_noderow[tops_items[j]] = (int)j;
	endInsertRows();

	existing_set.insert(h);
    }
}


/* rebuild_item_children: rebuild an already-expanded QgItem's child rows with
 * individual begin/endRemoveRows + begin/endInsertRows signals so the tree view
 * keeps its collapsed/expanded state for items that have not changed.
 * Must be called only while in_g_update_ == true (no re-entrancy). */
void
QgModel::rebuild_item_children(QgItem *item, DbiState *UNUSED(dbis))
{
    if (!item || !item->ihash || item->children.empty())
	return;

    QModelIndex parent_idx = NodeIndex(item);
    if (!parent_idx.isValid())
	return;

    /* Snapshot the old child list before item_rebuild() modifies it. */
    std::vector<QgItem *> old_children = item->children;
    int old_count = (int)old_children.size();

    /* Rebuild children using the updated DbiState.
     * item_rebuild() replaces item->children in place. */
    item_rebuild(item);
    std::vector<QgItem *> new_children = item->children;
    int new_count = (int)new_children.size();

    if (new_count == old_count && new_children == old_children)
	return; /* Nothing actually changed */

    /* Remove old rows. Restore old_children so Qt sees the right count
     * for rowCount() inside the begin/end bracket. */
    item->children = old_children;
    item->c_noderow.clear();
    for (int j = 0; j < old_count; j++)
	item->c_noderow[old_children[j]] = j;
    if (old_count > 0) {
	beginRemoveRows(parent_idx, 0, old_count - 1);
	item->children.clear();
	item->c_noderow.clear();
	endRemoveRows();
    }

    /* Remove any orphaned QgItems that item_rebuild() replaced. */
    for (QgItem *oc : old_children) {
	if (!oc) continue;
	bool reused = false;
	for (QgItem *nc : new_children)
	    if (nc == oc) { reused = true; break; }
	if (!reused) {
	    items->erase(oc);
	    delete oc;
	}
    }

    /* Insert new rows. */
    if (new_count > 0) {
	beginInsertRows(parent_idx, 0, new_count - 1);
	item->children = new_children;
	item->c_noderow.clear();
	for (int j = 0; j < new_count; j++)
	    item->c_noderow[new_children[j]] = j;
	endInsertRows();
    }
}


/* apply_incremental_updates: handle DBI events with targeted Qt model signals.
 * Handles ObjectAdded/Removed via reconcile_tops(), ObjectModified/AttributeChanged
 * via dataChanged() + per-item child rebuild (no full model reset). */
void
QgModel::apply_incremental_updates(DbiState *dbis, const std::vector<DbiChangeEvent> &events)
{
    bool has_structural = false;

    for (const auto &ev : events) {
	switch (ev.kind) {
	    case DbiChangeKind::ObjectAdded:
	    case DbiChangeKind::ObjectRemoved:
		has_structural = true;
		break;
	    case DbiChangeKind::ObjectModified:
	    case DbiChangeKind::AttributeChanged: {
		/* Update cached display data and emit dataChanged() for every
		 * visible QgItem that represents this object.  For expanded
		 * combs, also rebuild the child row list in place. */
		unsigned long long obj_hash = ev.object.v;
		/* Collect items to rebuild (avoid modifying *items during iteration) */
		std::vector<QgItem *> to_rebuild;
		for (QgItem *item : *items) {
		    /* Resolve instance hash to object hash */
		    unsigned long long h = item->ihash;
		    auto im = dbis->i_map.find(h);
		    if (im != dbis->i_map.end()) h = im->second;
		    if (h != obj_hash) continue;

		    /* Refresh cached dp / name */
		    struct directory *dp = dbis->get_hdp(item->ihash);
		    if (dp) {
			item->dp = dp;
			bu_vls_sprintf(&item->name, "%s", dp->d_namep);
		    }
		    /* Refresh child count for combs */
		    auto pv_it = dbis->p_v.find(item->ihash);
		    if (pv_it != dbis->p_v.end())
			item->c_count = pv_it->second.size();

		    QModelIndex idx = NodeIndex(item);
		    if (idx.isValid())
			emit dataChanged(idx, idx);

		    /* Queue expanded combs for child rebuild */
		    if (!item->children.empty())
			to_rebuild.push_back(item);
		}
		for (QgItem *item : to_rebuild)
		    rebuild_item_children(item, dbis);
		break;
	    }
	    default:
		/* Unexpected kind: trigger a full reset at end */
		full_model_reset(dbis);
		return;
	}
    }

    /* If there were any structural changes (adds/removes), reconcile tops. */
    if (has_structural)
	reconcile_tops(dbis);

    emit check_highlights();
}

void
QgModel::g_update(struct db_i *n_dbip)
{
    // Re-entrancy guard: on_dbi_changed() is called inside dbis->update(),
    // which g_update() calls below.  Without this guard the observer callback
    // would re-enter g_update() from within a beginResetModel() block, causing
    // Qt model corruption.
    if (in_g_update_) return;
    in_g_update_ = true;

    // In case we have opened a completely new .g file, set the callbacks
    if (n_dbip && !BU_PTBL_LEN(&n_dbip->dbi_changed_clbks)) {
	// Primary driver of model updates is when individual objects are changed
	db_add_changed_clbk(n_dbip, &qgmodel_changed_callback, (void *)this);

	// If the tops list changes, we need to update that vector as well.  Unlike
	// local dp changes, we can only (re)build the tops list after an
	// update_nref pass is complete.
	db_add_update_nref_clbk(n_dbip, &qgmodel_update_nref_callback, (void *)this);
    }

    if (!n_dbip) {
	// if we have no dbip, clear out everything
	beginResetModel();
	std::unordered_set<QgItem *>::iterator s_it;
	for (s_it = items->begin(); s_it != items->end(); s_it++) {
	    QgItem *itm = *s_it;
	    delete itm;
	}
	// Deleted all items, but we need a root item regardless
	// of whether a .g is open - recreate it
	rootItem = new QgItem(0, this);
	rootItem->mdl = this;

	items->clear();
	tops_items.clear();
	emit mdl_changed_db((void *)gedp);
	emit view_change(QG_VIEW_DRAWN);
	// endResetModel() already implies layout change via modelReset() signal.
	// Do NOT emit layoutChanged() without a preceding layoutAboutToBeChanged().
	changed_db_flag = 0;
	endResetModel();
	in_g_update_ = false;
	return;
    }

    DbiState *dbis = (DbiState *)gedp->dbi_state;

    // If the DbiState was replaced (e.g. open/closedb recreated it), re-register
    // as an observer so on_dbi_changed() receives future events.
    if (dbis && dbis != observed_dbi_state_) {
	dbis->add_observer(this);
	observed_dbi_state_ = dbis;
    }

    // If we have a dbip and the changed flag is set, figure out what's different
    if (changed_db_flag) {
	// Step 1: update DbiState flat maps.
	// on_dbi_changed() will collect events into pending_dbi_events_ while
	// in_g_update_ is true — without calling g_update() again.
	pending_dbi_events_.clear();
	dbis->update();

	// Step 2: decide between targeted row operations and a full reset.
	// Use targeted updates for simple ObjectAdded/Removed/Modified events.
	// Full reset is required for: batch events, CombTreeChanged, or when no
	// events were collected (e.g. a new file was opened and the DbiState
	// constructor already populated its state without firing per-object events).
	bool needs_full_reset = false;
	if (pending_dbi_events_.empty()) {
	    // No events: full rebuild required (new file or constructor-time init)
	    needs_full_reset = true;
	} else {
	    for (const auto &ev : pending_dbi_events_) {
		if (ev.batch || ev.kind == DbiChangeKind::CombTreeChanged) {
		    needs_full_reset = true;
		    break;
		}
	    }
	}

	if (needs_full_reset) {
	    full_model_reset(dbis);
	} else {
	    apply_incremental_updates(dbis, pending_dbi_events_);
	}

	pending_dbi_events_.clear();
    }

    // If we did change something, we need to let the application know
    if (changed_db_flag) {
	emit mdl_changed_db((void *)gedp);
	// NOTE: layoutChanged() must only follow layoutAboutToBeChanged().
	// full_model_reset() uses beginResetModel/endResetModel which implies
	// layout change.  apply_incremental_updates() uses per-row signals.
	// Do NOT emit layoutChanged() here as an unpaired call is a protocol
	// violation detected by QAbstractItemModelTester.
	emit check_highlights();
    }

    // Reset flag - we're in sync now
    changed_db_flag = 0;
    in_g_update_ = false;
}

int
QgModel::NodeRow(QgItem *node) const
{
    QgItem *np = node->parent();
    if (!np)
	return -1;
    if (np->c_noderow.find(node) == np->c_noderow.end())
	return -1;
    return np->c_noderow[node];
}


QModelIndex
QgModel::NodeIndex(QgItem *node) const
{
    if (node == rootItem)
	return QModelIndex();
    int nr = NodeRow(node);
    if (nr == -1)
	return QModelIndex();
    return createIndex(nr, 0, node);
}


bool
QgModel::canFetchMore(const QModelIndex &idx) const
{
    if (!idx.isValid())
	return false;

    QgItem *item = static_cast<QgItem*>(idx.internalPointer());
    if (item == rootItem)
       	return false;

    // canFetchMore() returns true when DB says there are children but we
    // haven't loaded them yet.  Since childCount() now returns children.size()
    // (the already-loaded count), this correctly signals lazy-loading readiness.
    if (item->children.size())
	return false;  // Already fetched, nothing more to load

    DbiState *dbis = (DbiState *)gedp->dbi_state;
    if (dbis->p_v.find(item->ihash) != dbis->p_v.end()) {
	if (dbis->p_v[item->ihash].size())
	    return true;
    }

    return false;
}

void
QgModel::fetchMore(const QModelIndex &idx)
{
    if (!idx.isValid())
	return;

    QgItem *item = static_cast<QgItem*>(idx.internalPointer());

    if (UNLIKELY(item == rootItem)) {
	return;
    }

    // If we're already populated, don't need to do it again
    if (item->children.size())
	return;

    DbiState *dbis = (DbiState *)gedp->dbi_state;
    unsigned long long chash = item->ihash;
    if (dbis->p_v.find(chash) == dbis->p_v.end()) {
	bu_log("QgModel::fetchMore: invalid hash 0x%llx - item may have been removed from the database\n", (unsigned long long)chash);
	return;
    }

    std::vector<unsigned long long> &nh = dbis->p_v[chash];
    std::vector<QgItem *> nc;
    std::vector<unsigned long long>::reverse_iterator nh_it;
    for (nh_it = nh.rbegin(); nh_it != nh.rend(); nh_it++) {
	QgItem *nitem = new QgItem(*nh_it, this);
	nitem->parentItem = item;
	nitem->op = dbis->bool_op(item->ihash, *nh_it);
	nc.push_back(nitem);
	items->insert(nitem);
    }

    // All done - let the Qt model know
    beginInsertRows(idx, 0, nc.size() - 1);
    item->children = nc;
    // define a map for quick QgItem * -> index lookups
    item->c_noderow.clear();
    for (size_t i = 0; i < nc.size(); i++) {
	item->c_noderow[nc[i]] = i;
    }
    endInsertRows();
    emit check_highlights();
    emit item->mdl->opened_item(item);
}


///////////////////////////////////////////////////////////////////////
//          Qt abstract model interface implementation
///////////////////////////////////////////////////////////////////////

QgItem *
QgModel::root()
{
    return rootItem;
}

QgItem *
QgModel::getItem(const QModelIndex &index) const
{
    if (index.isValid()) {
        QgItem *item = static_cast<QgItem*>(index.internalPointer());
        if (item)
            return item;
    }
    return rootItem;
}

QModelIndex
QgModel::index(int row, int column, const QModelIndex &parent_idx) const
{
    if (parent_idx.isValid() && parent_idx.column() != 0)
	return QModelIndex();

    QgItem *parentItem = getItem(parent_idx);
    if (!parentItem)
	return QModelIndex();

    QgItem *childItem = parentItem->child(row);
    if (childItem)
	return createIndex(row, column, childItem);

    return QModelIndex();
}

QModelIndex
QgModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
	return QModelIndex();

    QgItem *childItem = getItem(index);
    QgItem *parentItem = childItem ? childItem->parent() : nullptr;

    if (parentItem == rootItem || !parentItem)
	return QModelIndex();

    return createIndex(parentItem->childNumber(), 0, parentItem);
}

Qt::ItemFlags
QgModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
	return Qt::NoItemFlags;

    Qt::ItemFlags flags = QAbstractItemModel::flags(index);

    // flags |= Qt::ItemIsEditable;

    return flags;
}

QVariant
QgModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
	return QVariant();
    QgItem *qi= getItem(index);
    DbiState *dbis = (DbiState *)qi->mdl->gedp->dbi_state;

    // Column 0 — object name and all non-display roles
    if (index.column() == 0) {
	if (role == Qt::DisplayRole)
	    return QVariant(bu_vls_cstr(&qi->name));
	if (role == BoolInternalRole)
	    return QVariant(qi->op);
	if (role == DirectoryInternalRole)
	    return QVariant::fromValue((void *)(qi->dp));
	if (role == DrawnDisplayRole) {
	    BViewState *vs = dbis->get_view_state(gedp->ged_gvp);
	    return QVariant(vs->is_hdrawn(-1, qi->path_hash()));
	}
	if (role == SelectDisplayRole) {
	    SelectionSet *ss = dbis->get_selection_set(nullptr);
	    if (ss)
		return QVariant(ss->is_selected(qi->path_hash()));
	}
	if (role == TypeIconDisplayRole)
	    return QVariant(qi->icon);
	if (role == HighlightDisplayRole) {
	    SelectionSet *ss = dbis->get_selection_set(nullptr);
	    if (!ss)
		return QVariant();
	    switch (qi->mdl->interaction_mode) {
		case 0:
		    if (qi->open_itm == false && ss->is_ancestor(qi->path_hash()))
			return QVariant(1);
		    return QVariant(0);
		case 1:
		    if (ss->is_obj_immediate_parent(qi->ihash) || ss->is_obj_ancestor(qi->ihash))
			return QVariant(2);
		    return QVariant(0);
		case 2:
		    if (ss->is_obj_immediate_parent(qi->ihash))
			return QVariant(3);
		    if (ss->is_obj_ancestor(qi->ihash))
			return QVariant(2);
		    return QVariant(0);
		default:
		    return QVariant(0);
	    }
	}
	return QVariant();
    }

    // Columns > 0 — attribute values; only Qt::DisplayRole is served
    if (role != Qt::DisplayRole)
	return QVariant();

    int ac = index.column() - 1;
    if (ac < 0 || ac >= attribute_columns_.count())
	return QVariant();

    const QString key = attribute_columns_.at(ac);
    const GObj *go = dbis->get_gobj(qi->ihash);

    if (key == QLatin1String("region")) {
	if (go && go->region_flag)
	    return QVariant(QLatin1String("R"));
	return QVariant(QLatin1String(""));
    }
    if (key == QLatin1String("region_id")) {
	if (go && go->region_id >= 0)
	    return QVariant(go->region_id);
	return QVariant(QLatin1String(""));
    }
    if (key == QLatin1String("color") || key == QLatin1String("rgb")) {
	auto it = dbis->rgb.find(qi->ihash);
	if (it != dbis->rgb.end()) {
	    unsigned int cval = it->second;
	    int r = (int)(cval & 0xFFu);
	    int g = (int)((cval >> 8) & 0xFFu);
	    int b = (int)((cval >> 16) & 0xFFu);
	    return QVariant(QString("%1/%2/%3").arg(r).arg(g).arg(b));
	}
	return QVariant(QLatin1String(""));
    }

    // General attribute: live AVS lookup.
    // NOTE: This is a synchronous db5_get_attributes() call per visible cell.
    // For performance-sensitive use with many visible rows, prefer the built-in
    // keys ("region", "region_id", "color") which use cached DbiState maps.
    if (qi->dp && dbis->dbip) {
	struct bu_attribute_value_set avs;
	BU_AVS_INIT(&avs);
	db5_get_attributes(dbis->dbip, &avs, qi->dp);
	const char *val = bu_avs_get(&avs, key.toUtf8().constData());
	QVariant result(val ? QString::fromUtf8(val) : QString());
	bu_avs_free(&avs);
	return result;
    }
    return QVariant();
}

QVariant
QgModel::headerData(int section, Qt::Orientation UNUSED(orientation), int role) const
{
    if (role != Qt::DisplayRole)
	return QVariant();
    if (section == 0)
	return QString("Object Names");
    if (section > 0 && section - 1 < attribute_columns_.count())
	return attribute_columns_.at(section - 1);
    return QVariant();
}

int
QgModel::rowCount(const QModelIndex &p) const
{
    QgItem *pItem;
    if (p.column() > 0)
	return 0;

    if (!p.isValid())
	pItem = rootItem;
    else
	pItem = static_cast<QgItem*>(p.internalPointer());

    return pItem->childCount();
}

int
QgModel::columnCount(const QModelIndex &UNUSED(p)) const
{
    return 1 + attribute_columns_.count();
}

void
QgModel::set_attribute_columns(const QStringList &keys)
{
    if (attribute_columns_ == keys)
	return;
    beginResetModel();
    attribute_columns_ = keys;
    endResetModel();
}

///////////////////////////////////////////////////////////////////////
//                  .g Centric Methods
///////////////////////////////////////////////////////////////////////
int
QgModel::run_cmd(struct bu_vls *msg, int argc, const char **argv)
{
    model_dbip = gedp->dbip;

    changed_dp.clear();

    if (!ged_cmd_exists(argv[0])) {
	const char *ccmd = nullptr;
	int edist = ged_cmd_lookup(&ccmd, argv[0]);
	if (edist) {
	    if (msg)
		bu_vls_sprintf(msg, "Command %s not found, did you mean %s (edit distance %d)?\n", argv[0],   ccmd, edist);
	}
	return BRLCAD_ERROR;
    }

    int ret = ged_exec(gedp, argc, argv);

    // If this is one of the GED commands supporting incremental input, this
    // return code means we need more input from the application before any
    // command can be run - no need to do the remainder of the logic below.
    if (ret & GED_MORE)
	return ret;

    // If we have the need_update_nref flag set, we need to do db_update_nref
    // ourselves - the backend logic made a dp add/remove but didn't trigger
    // the nref updates (can that happen?).
    if (gedp->dbip && need_update_nref) {
	// bu_log("missing callback in librt?\n");
	db_update_nref(gedp->dbip, &rt_uniresource);
    }

    // If we have a new .g file, set the changed flag
    if (model_dbip != gedp->dbip)
	changed_db_flag = 1;

    // Assuming we're not doing a full rebuild, trigger the post-cmd updates
    g_update(gedp->dbip);

    model_dbip = gedp->dbip;

    if (msg && gedp)
	bu_vls_printf(msg, "%s", bu_vls_cstr(gedp->ged_result_str));

    return ret;
}

// Normally the model won't define methods for specific GED commands, but there
// are a few exceptions related to common, standard operations like drawing that
// are potentially triggered from QActions.  TODO - it might be better to have
// these live in the selection proxy model...
int
QgModel::draw_action()
{
    QTCAD_SLOT("QgModel::draw_action", 1);
    // https://stackoverflow.com/a/28647342/2037687
    QAction *a = qobject_cast<QAction *>(sender());
    QVariant v = a->data();
    QgItem *cnode  = (QgItem *) v.value<void *>();
    if (!cnode)
	return BRLCAD_ERROR;
    std::vector<unsigned long long> path_hashes = cnode->path_items();
    struct bu_vls path_str = BU_VLS_INIT_ZERO;
    DbiState *dbis = (DbiState *)gedp->dbi_state;
    dbis->print_path(&path_str, path_hashes);
    int ret = draw(bu_vls_cstr(&path_str));
    bu_vls_free(&path_str);
    return ret;
}

int
QgModel::draw(const char *inst_path)
{
    QTCAD_SLOT("QgModel::draw", 1);
    const char *argv[2];
    argv[0] = "draw";
    argv[1] = inst_path;

    int ret = ged_exec_draw(gedp, 2, argv);

    emit view_change(QG_VIEW_DRAWN);
    return ret;
}

int
QgModel::erase_action()
{
    QTCAD_SLOT("QgModel::erase_action", 1);
    // https://stackoverflow.com/a/28647342/2037687
    QAction *a = qobject_cast<QAction *>(sender());
    QVariant v = a->data();
    QgItem *cnode  = (QgItem *) v.value<void *>();
    if (!cnode)
	return BRLCAD_ERROR;
    std::vector<unsigned long long> path_hashes = cnode->path_items();
    struct bu_vls path_str = BU_VLS_INIT_ZERO;
    DbiState *dbis = (DbiState *)gedp->dbi_state;
    dbis->print_path(&path_str, path_hashes);
    int ret = erase(bu_vls_cstr(&path_str));
    bu_vls_free(&path_str);
    return ret;
}

int
QgModel::erase(const char *inst_path)
{
    QTCAD_SLOT("QgModel::erase", 1);
    const char *argv[2];
    argv[0] = "erase";
    argv[1] = inst_path;

    int ret = ged_exec_erase(gedp, 2, argv);

    emit view_change(QG_VIEW_DRAWN);
    return ret;
}

void
QgModel::toggle_hierarchy()
{
    QTCAD_SLOT("QgModel::toggle_hierarchy", 1);
    if (!gedp || !gedp->dbip)
	return;

    flatten_hierarchy = !flatten_hierarchy;
    changed_db_flag = 1;
    g_update(gedp->dbip);
}

void
QgModel::item_collapsed(const QModelIndex &index)
{
    QTCAD_SLOT("QgModel::item_collapsed", 1);
    QgItem *itm = getItem(index);
    itm->open_itm = false;
}

void
QgModel::item_expanded(const QModelIndex &index)
{
    QTCAD_SLOT("QgModel::item_expanded", 1);
    QgItem *itm = getItem(index);
    itm->open_itm = true;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

