/*                        Q G M O D E L . H
 * BRL-CAD
 *
 * Copyright (c) 2014-2026 United States Government as represented by
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
/** @file QgModel.h
 *
 * Abstract Qt model of a BRL-CAD .g database
 *
 * Qt uses a Model/View approach to visualizing information such as trees, and
 * for BRL-CAD hierarchy visualization the most natural fit in Qt is to define
 * a Qt Model that corresponds to the .g data.  There are challenges in
 * defining such a model that aren't immediately apparent, and they complicate
 * the implication considerably.  In essence it is necessary to translate
 * BRL-CAD's data concepts to those needed by Qt and vice versa.  To attempt
 * to articulate the motivations for the model as defined here, the following
 * discussion tries to detail the issues encountered.
 *
 * The most fundamental difficulty is that comb object instances - the primary
 * hierarchical unit defined in .g files - are not necessarily globally unique
 * in the .g file. To illustrate this, consider a combination A that has
 * underneath it two different instances of a combination B using different
 * placement matrices (M1 and M2). B is in turn defined using combination C
 * which is defined using object D:
 *
 * A
 *   u [M1] B
 *            u C
 *                u D
 *   u [M2] B
 *            u C
 *                u D
 *
 * The objects A, B, C and D are unique (all .g objects are uniquely named,
 * which ensures uniqueness in at least one sense) but the Qt model hierarchy
 * requires the expression of comb INSTANCES, not objects.  Moreover, it needs
 * those instances to be not just locally unique, but GLOBALLY unique.  The
 * union of D into C using an identity matrix is unique with respect to B, but
 * not when B is combined with A multiple times.  That inclusion of B into A
 * (which is completely non-local as far as C's or B's definitions are
 * concerned) results in TWO manifestations of CuD in the overall model that
 * are NOT the same model item, despite mapping to exactly the same .g data.
 * Or, stated another way, we have multiple items of CuD in the hierarchy:
 * Au[M1]BuCuD and Au[M2]BuCuD.  However, from the .g data perspective, CuD IS
 * a unique combination instance - it is stored only once, and any edit to C or
 * D will impact BOTH Au[M1]BuCuD and Au[M2]BuCuD. This means that .g comb
 * instances cannot, by themselves, map directly to items in a Qt model.
 *
 * TODO - investigate https://wiki.qt.io/Model_Test to see if it may be
 * useful for this code.
 */

#ifndef QGMODEL_H
#define QGMODEL_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <QAbstractItemModel>
#include <QImage>
#include <QModelIndex>

#include "qtcad/defines.h"
#include "qtcad/QgRoles.h"
#include "qtcad/QgSession.h"
#include "qtcad/QgTypes.h"

/* Forward declarations — the implementation includes the full headers. */
struct bu_vls;
struct bview;
struct db_i;
struct directory;
struct ged;

// QgItems correspond to the actual Qt entries displayed in the view.  If a
// comb is reused in multiple places in the tree the same comb child instance
// definition may be referenced by multiple QgItems, but each QgItem
// corresponds uniquely to a single string of directory pointers, ops, matrices
// and (if needed) icnt values from a series of instance structures generated
// by walking comb trees.  A single QgItem being selected may activate multiple
// QgItems by virtue of those other QgItems using the same comb instance as the
// selected QgItem (this reflects the impact any changes will have on the
// database contents) but from the Qt perspective the unique selection item is
// the QgItem.  If the QgItem is instructed to conduct an operation with
// implications at the instance level (modification of primitive parameters,
// for example) other QgItems with the same instance will have to be
// separately handled to reflect the same changes.
//
// A QgItem will store parents and children, but remember that both of those
// relationships are informed by the .g relationships rather than directly
// representing them.  A QgItem always has a unique parent, even if its
// instance is associated with multiple active QgItems elsewhere in the tree.
// The children array is initially empty even though the comb it corresponds to
// may have many children.  It is only when the QgItem is opened that the
// DbiState is queried for its children and new QgItems based on the children
// are created.  Because we don't want to collapse all opened/closed state in a
// subtree if we close a parent QgItem, the children QgItems remain populated
// through a close to persist that state.

class QTCAD_EXPORT QgModel;

class QTCAD_EXPORT QgItem {
public:
	explicit QgItem(unsigned long long hash, QgModel *ictx);
	~QgItem();

	// Testing functions to simulate GUI activities
	void open();
	void close();

	/* To a BRL-CAD developer's eyes this is rather abstract, but the
	 * combination of the ihash and ctx points to the BRL-CAD specific data
	 * for this instance.
	 */
	/* Return the unique instance hash represented by this item. */
	unsigned long long instanceHash() const
	{
		return ihash;
	}
	/* Return the model that owns this item. */
	QgModel *model() const
	{
		return mdl;
	}

	std::vector<unsigned long long> path_items();
	unsigned long long path_hash();

	/* Flag to determine if this QgItem should be viewed as opened or closed -
	 * in order to preserve subtree state, we don't want to obliterate the
	 * subtree related data structures, so we can't test for an empty children
	 * vector to make this determination.
	 *
	 * This is less than ideal in that it represents a bit of a violation
	 * of the separation between view and model, but the alternative -
	 * storing a map in the view class or the selection proxy model
	 * where we can set and store these flags - is a fair bit more
	 * involved.  Give we already have some separation between this logic
	 * and the instance logic which is the real wrapper around the .g
	 * information, going with the simple approach for now. */
	/* Return true if this item is currently marked open in the Qt tree. */
	bool isOpen() const
	{
		return open_itm;
	}
	/* Set the cached Qt tree open/closed state for this item. */
	void setOpen(bool open)
	{
		open_itm = open;
	}

	// Qt related elements
	QgItem *parent();
	int childNumber() const;
	void appendChild(QgItem *C);
	QgItem *child(int n);
	int childCount() const;
	/* Return the currently loaded child-item collection. */
	const std::vector<QgItem *> &childItems() const
	{
		return children;
	}
	// NOTE - for now this is 1 - the model will have to
	// incorporate some notion of exposed attributes and
	// their ordering before that changes
	int columnCount() const;

private:
	friend class QgModel;

	unsigned long long ihash = 0;
	QgModel *mdl = nullptr;
	QgItem *parentItem = nullptr;
	std::vector<QgItem *> children;
	std::unordered_map<QgItem *, int> c_noderow;
	size_t c_count = 0;

	// Cached data from the instance, so we can keep
	// displaying while librt does work on the instances.
	struct bu_vls *name_ptr = nullptr;  /* heap-allocated; init/freed in ctor/dtor */
	int op = 'u';                       /* matches db_op_t DB_OP_UNION = 'u' */
	struct directory *dp = nullptr;
	QImage icon;
	bool open_itm = false;
	//int draw_state = 0;
	//bool select_state = false;
};

/* The primary expression in a Qt context of a .g database and its contents.
 * This is an important data structure for the construction of .g graphical
 * interfaces.
 *
 * The concept of "columns" in this model best maps to attributes on .g objects
 * - by default, the only attribute data visible is implicit in boolean labels
 * and icons, so there is only one column in the model.  That said, there is
 * definitely interest in an ability for users to visualize and modify
 * attributes as additional columns in a tree view, so we need to design and
 * maintain this code with that eventual use case in mind.
 *
 * Hierarchy items
 *
 * One of the subtle points of a .g hierarchy representation is that an
 * instance may be locally unique, but not globally unique.  For example, if
 * comb A has underneath it two different instances of comb B using different
 * placement matrices, and B is defined using C:
 *
 * A
 *   u [M1] B
 *            u C
 *                u D
 *   u [M2] B
 *            u C
 *                u D
 *
 * D has only one unique instance definition in this hierarchy - IDN matrix
 * union of D into C - but that instance has TWO manifestations in the overall
 * tree that are NOT the same model items by virtue of having different B
 * parent instances further up in the hierarchy: Au[M1]B and Au[M2]B.
 *
 * To with this the primary model hierarchy is an items hierarchy which holds
 * QgItems that encode a reference to an instance and the parent/child data
 * specific to an individual place in the hierarchy.  It is a QgItem, not an
 * instance, which can correspond to a row in a Qt abstract model.  QgItems are
 * created lazily in response to view requests, working from a seed set created
 * from the top level objects in a database.
 */
class QTCAD_EXPORT QgModel : public QAbstractItemModel {
	Q_OBJECT
	Q_DISABLE_COPY_MOVE(QgModel)


public:
	explicit QgModel(QObject *p = nullptr, const char *npath = nullptr);
	~QgModel();

	// .g Db interface and containers
	int run_cmd(struct bu_vls *msg, int argc, const char **argv);

	/* Return the session that owns the GED context for this model. */
	QgSession *session() const
	{
		return m_session;
	}

	/* Return the GED context backing this model (delegates to session). */
	struct ged *ged() const
	{
		return m_session ? m_session->ged() : nullptr;
	}

	// Updates to .g models are potentially far-reaching - in principle, a
	// single GED command execution can change every item in the database.
	// This method is intended to be run after a GED command execution to
	// update the model, or upon startup.
	void g_update(struct db_i *n_dbip);

	// This is a bit of a cheat in that an interaction mode isn't really
	// part of the .g state, but we want the tree highlighting logic to
	// reflect such a state and to have that logic reusable at the library
	// level it needs to be available in a container readily accessible at
	// that level.
	/* Return the current tree/highlight interaction mode. */
	int interactionMode() const
	{
		return interaction_mode;
	}
	/* Update the current tree/highlight interaction mode. */
	void setInteractionMode(int mode)
	{
		interaction_mode = mode;
	}

	// Qt Model interface

	// Get the root QgItem
	QgItem *root();

	// Qt often needs to work in terms of index values, but to manipulate
	// data more directly we need to get the QgItem pointer itself.  These
	// convenience functions will translate each reference type.
	QModelIndex NodeIndex(QgItem *node) const;
	QgItem *getItem(const QModelIndex &index) const;

	enum CADDataRoles {
		BoolInternalRole      = QgDataRoles::BoolInternalRole,
		BoolDisplayRole       = QgDataRoles::BoolDisplayRole,
		DirectoryInternalRole = QgDataRoles::DirectoryInternalRole,
		TypeIconDisplayRole   = QgDataRoles::TypeIconDisplayRole,
		HighlightDisplayRole  = QgDataRoles::HighlightDisplayRole,
		DrawnDisplayRole      = QgDataRoles::DrawnDisplayRole,
		SelectDisplayRole     = QgDataRoles::SelectDisplayRole
	};

	// Return data used for displaying each individual entry
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

	// Get data for labeling column headers (for the moment this is just
	// the object name label - if/when we add support for attribute display
	// in columns it will need to get more sophisticated.)
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

	// This is 1 until we add support for attribute reporting
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;

	// The number of available children.  This will correspond to the
	// number of lines printed by the "l" command to show the immediate
	// children of a comb (indeed, the tree view of the model can be
	// thought of in some ways as a graphical version of the "l" command's
	// textual output.)
	int rowCount(const QModelIndex &parent = QModelIndex()) const override;

	// These functions tell the model and view that an entry has children
	// to display and how to retrieve them.
	bool canFetchMore(const QModelIndex &idx) const override;
	void fetchMore(const QModelIndex &idx) override;

	void setChangedDatabaseFlag(int flag)
	{
		changed_db_flag = flag;
	}
	void setNeedUpdateNref(bool update)
	{
		need_update_nref = update;
	}
	/* Return all currently active QgItems owned by this model. */
	const std::unordered_set<QgItem *> &allItems() const
	{
		return items ? *items : emptyItems();
	}
	bool hasItem(QgItem *item) const
	{
		return (items && item && items->find(item) != items->end());
	}
	/* Return the sorted QgItems corresponding to top-level instances. */
	const std::vector<QgItem *> &topItems() const
	{
		return tops_items;
	}
	/* Return true when the model is presenting a flattened object list. */
	bool flattenHierarchy() const
	{
		return flatten_hierarchy != 0;
	}

signals:
	// Emitted if the commands think they may have changed the database
	// structure in some way.
	void mdl_changed_db(void *);

	// Emitted if a method thinks it may have changed the view.  Normally
	// this is dealt with at a higher level by checking the view state
	// (it is sometimes extremely difficult to know if a complex command
	// will alter the view) but if a particular method knows it will
	// do so, it may emit this signal
	void view_changed(QgViewUpdateFlags);

	// Let the tree view know it has highlighting work to do it wouldn't
	// otherwise see
	void check_highlights();

	// Signal emitted when a model item is opened (there is work that
	// needs to be done in the view after this happens...)
	void opened_item(QgItem *);


public slots:
	int draw_action();
	int draw(const char *path);
	int erase_action();
	int erase(const char *path);
	void toggle_hierarchy();
	void item_collapsed(const QModelIndex &index);
	void item_expanded(const QModelIndex &index);

private:
	int NodeRow(QgItem *node) const;
	QModelIndex index(int row, int column, const QModelIndex &p) const override;
	QModelIndex parent(const QModelIndex &child) const override;
	Qt::ItemFlags flags(const QModelIndex &index) const override;

	void item_rebuild(QgItem *item);
	static const std::unordered_set<QgItem *> &emptyItems()
	{
		static const std::unordered_set<QgItem *> empty_items;
		return empty_items;
	}

	// A flag for callbacks to set if they alter the database in some way.
	// Used to determine whether to emit the mdl_changed_db signal once
	// after a GED command processing call is complete. If emitted, the
	// interface will know to take certain steps when updating views.
	int changed_db_flag = 0;

	// It's unclear if we need this, but allow callbacks to insist on a
	// post-command running of update_nref - in principle this should be
	// already handled by command and/or librt logic, but not sure if we
	// can count on that...
	bool need_update_nref = false;

	/* Used by callers to identify which objects need to be redrawn when
	 * scene views are updated. */
	std::unordered_set<struct directory *> changed_dp;

	int interaction_mode = 0;

	// Convenience container holding all active QgItems
	std::unordered_set<QgItem *> *items = nullptr;

	// Sorted QgItem pointers corresponding to the tops instances
	std::vector<QgItem *> tops_items;

	// Toggle for whether or not the model should be viewed using a tops
	// listing or the full object listing as the "seed" set
	int flatten_hierarchy = 0;

	QgSession *m_session = nullptr;
	QgItem *rootItem;
	struct db_i *model_dbip = nullptr;
};

#endif //QGMODEL_H

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
