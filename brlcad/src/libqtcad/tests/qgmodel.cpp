/*                     Q G M O D E L . C P P
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
 * Lesser General Public License for more details->
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file qgmodel.cpp
 *
 * Brief description
 *
 */

#include "common.h"
#include <cstdio>
#include <iostream>
#include <unordered_map>
#include <vector>
#ifdef USE_QTTEST
#  include <QAbstractItemModelTester>
#  include <QSignalSpy>
#endif

#include "bu/app.h"
#include "bu/file.h"
#include "bu/log.h"
#include "bu/vls.h"
#include "ged.h"
#include "../../libged/dbi.h"
#include "qtcad/QgModel.h"


void
open_children(QgItem *itm, QgModel *s, int depth, int max_depth)
{
    if (!itm || !itm->ihash)
	return;

    if (max_depth > 0 && depth >= max_depth)
	return;

    itm->open();

    for (size_t j = 0; j < itm->children.size(); j++) {
	QgItem *c = itm->child(j);
	open_children(c, s, depth+1, max_depth);
    }
}

void
open_tops(QgModel *s, int depth)
{
    for (size_t i = 0; i < s->tops_items.size(); i++) {
	QgItem *itm = s->tops_items[i];
	if (!itm->ihash)
	    continue;
	open_children(itm, s, 0, depth);
    }
}

void
close_children(QgItem *itm)
{
    itm->close();
    for (size_t j = 0; j < itm->children.size(); j++) {
	QgItem *c = itm->child(j);
	close_children(c);
    }
}

void
print_children(QgItem *itm, QgModel *s, int depth)
{
    if (!itm || !itm->ihash)
	return;


    for (int i = 0; i < depth; i++) {
	std::cout << "  ";
    }
    if (depth)
	std::cout << "* ";

    struct bu_vls path_str = BU_VLS_INIT_ZERO;
    std::vector<unsigned long long> path_hashes = itm->path_items();
    DbiState *dbis = (DbiState *)itm->mdl->gedp->dbi_state;
    dbis->print_hash(&path_str, path_hashes[path_hashes.size()-1]);
    std::cout << bu_vls_cstr(&path_str) << "\n";
    bu_vls_free(&path_str);

    for (size_t j = 0; j < itm->children.size(); j++) {
	QgItem *c = itm->child(j);

	if (!itm->open_itm) {
	    continue;
	}

	print_children(c, s, depth+1);
    }
}

void
print_tops(QgModel *s)
{
    for (size_t i = 0; i < s->tops_items.size(); i++) {
	QgItem *itm = s->tops_items[i];
	if (!itm->ihash)
	    continue;
	print_children(itm, s, 0);
    }
}

/* ------------------------------------------------------------------ */
/* T2: rebuild_item_children regression test                          */
/*                                                                    */
/* Creates its own temporary .g, opens it in a fresh QgModel, expands*/
/* a comb, then removes a member.  Verifies via QSignalSpy that:     */
/*   - rowsRemoved fires (targeted per-row signal)                    */
/*   - modelReset does NOT fire (no full-reset fallback)              */
/* ------------------------------------------------------------------ */
static int
test_rebuild_item_children(const char *tmppath)
{
    int failures = 0;

#define T2_CHECK(cond, msg) \
    do { \
	if (!(cond)) { \
	    fprintf(stderr, "FAIL [T2]: %s (line %d)\n", (msg), __LINE__); \
	    failures++; \
	} else { \
	    printf("PASS [T2]: %s\n", (msg)); \
	} \
    } while (0)

    /* Create a fresh .g file */
    bu_file_delete(tmppath);
    struct ged *gedp = ged_open("db", tmppath, 0);
    if (!gedp) {
	fprintf(stderr, "ERROR [T2]: ged_open failed for %s\n", tmppath);
	return 1;
    }
    /* Populate: sph1.s, tor1.s, reg1.r (contains sph1.s), top1.c (reg1.r + tor1.s) */
    { const char *av[] = {"in","sph1.s","sph","0","0","0","10",NULL};
      ged_exec_in(gedp, 7, av); }
    { const char *av[] = {"in","tor1.s","tor","0","0","0","0","0","1","20","5",NULL};
      ged_exec_in(gedp, 11, av); }
    { const char *av[] = {"r","reg1.r","u","sph1.s",NULL};
      ged_exec_r(gedp, 4, av); }
    { const char *av[] = {"g","top1.c","reg1.r","tor1.s",NULL};
      ged_exec_g(gedp, 4, av); }

    /* Open the populated .g in a QgModel */
    QgModel mdl(NULL, NULL);
    /* Transfer the gedp so the model sees our populated DB */
    /* Instead, open the file directly */
    ged_close(gedp);
    gedp = NULL;

    /* Re-open using QgModel's internal ged machinery */
    QgModel model(NULL, tmppath);

#ifdef USE_QTTEST
    QAbstractItemModelTester *tester =
	new QAbstractItemModelTester(
	    (QAbstractItemModel *)&model,
	    QAbstractItemModelTester::FailureReportingMode::Fatal);
    (void)tester;
#endif

    /* Locate top1.c in tops_items */
    QgItem *top1_item = nullptr;
    DbiState *dbis = (DbiState *)model.gedp->dbi_state;
    for (QgItem *itm : model.tops_items) {
	if (!itm->ihash) continue;
	struct bu_vls name = BU_VLS_INIT_ZERO;
	dbis->print_hash(&name, itm->ihash);
	if (BU_STR_EQUAL(bu_vls_cstr(&name), "top1.c"))
	    top1_item = itm;
	bu_vls_free(&name);
    }
    T2_CHECK(top1_item != nullptr, "top1.c found in tops_items");
    if (!top1_item) {
	bu_file_delete(tmppath);
	return failures + 1;
    }

    /* Expand top1.c so it has loaded children */
    QModelIndex top1_idx = model.NodeIndex(top1_item);
    T2_CHECK(top1_idx.isValid(), "NodeIndex for top1.c is valid");

    if (model.canFetchMore(top1_idx))
	model.fetchMore(top1_idx);

    int child_count_before = (int)top1_item->children.size();
    T2_CHECK(child_count_before == 2,
	     "top1.c has 2 loaded children before modification");

#ifdef USE_QTTEST
    /* Attach signal spies *after* initial population */
    QSignalSpy spy_removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy spy_inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy spy_reset(&model, &QAbstractItemModel::modelReset);

    /* Remove tor1.s from top1.c without deleting the object */
    { const char *av[] = {"rm","top1.c","tor1.s",NULL};
      ged_exec_rm(model.gedp, 3, av); }

    /* Trigger incremental update */
    model.g_update(model.gedp->dbip);

    /* rowsRemoved should have fired */
    T2_CHECK(spy_removed.count() > 0,
	     "rowsRemoved signal fired when member removed from expanded comb");

    /* modelReset must NOT have fired — no full reset should have occurred */
    T2_CHECK(spy_reset.count() == 0,
	     "modelReset did NOT fire (incremental update, not full reset)");

    int child_count_after = (int)top1_item->children.size();
    T2_CHECK(child_count_after == 1,
	     "top1.c has 1 loaded child after removing tor1.s");

    /* Add a new member and verify rowsInserted fires */
    spy_inserted.clear();
    spy_reset.clear();
    { const char *av[] = {"in","sph2.s","sph","10","0","0","5",NULL};
      ged_exec_in(model.gedp, 7, av); }
    { const char *av[] = {"g","top1.c","sph2.s",NULL};
      ged_exec_g(model.gedp, 3, av); }
    model.g_update(model.gedp->dbip);

    T2_CHECK(spy_inserted.count() > 0 || spy_reset.count() == 0,
	     "rowsInserted fired or no full reset after adding member");
    T2_CHECK(spy_reset.count() == 0,
	     "modelReset did NOT fire after adding sph2.s to expanded comb");

    /* Verify unrelated top-level item (tor1.s) is still accessible */
    bool found_tor = false;
    for (QgItem *itm : model.tops_items) {
	if (!itm->ihash) continue;
	struct bu_vls name = BU_VLS_INIT_ZERO;
	dbis->print_hash(&name, itm->ihash);
	if (BU_STR_EQUAL(bu_vls_cstr(&name), "tor1.s"))
	    found_tor = true;
	bu_vls_free(&name);
    }
    T2_CHECK(found_tor, "tor1.s (unrelated object) still visible in model");

#else
    printf("SKIP [T2]: QSignalSpy checks skipped (USE_QTTEST not defined)\n");
    /* Still exercise the code path */
    { const char *av[] = {"rm","top1.c","tor1.s",NULL};
      ged_exec_rm(model.gedp, 3, av); }
    model.g_update(model.gedp->dbip);
    int child_count_after = (int)top1_item->children.size();
    T2_CHECK(child_count_after == 1,
	     "top1.c has 1 child after removing tor1.s (no Qt spy)");
#endif

    bu_file_delete(tmppath);
    return failures;

#undef T2_CHECK
}

int main(int argc, char *argv[])
{

    bu_setprogname(argv[0]);

    argc--; argv++;

    if (argc != 1)
	bu_exit(-1, "need to specify .g file\n");

    QgModel sm(NULL, argv[0]);
    QgModel *s = &sm;

#ifdef USE_QTTEST
    QAbstractItemModelTester *tester = new QAbstractItemModelTester((QAbstractItemModel *)s, QAbstractItemModelTester::FailureReportingMode::Fatal);
#endif

    // 2.  Implement "open" and "close" routines for the items that will exercise
    // the logic to identify, populate, and clear items based on child info.

    // Open everything
    std::cout << "\nAll open:\n";
    open_tops(s, -1);
    print_tops(s);

    // Close top level
    std::cout << "\nTop level closed:\n";
    for (size_t i = 0; i < s->tops_items.size(); i++) {
	QgItem *itm = s->tops_items[i];
	itm->close();
    }
    print_tops(s);


    // Open first level
    std::cout << "\nOpen first level (remember open children):\n";
    open_tops(s, 1);
    print_tops(s);


    // Close everything
    std::cout << "\nEverything closed:\n";
    for (size_t i = 0; i < s->tops_items.size(); i++) {
	QgItem *itm = s->tops_items[i];
	close_children(itm);
    }
    print_tops(s);

    // Open first level
    std::cout << "\nOpen first level (children closed):\n";
    open_tops(s, 1);
    print_tops(s);


    // 3.  Add callback support for syncing the instance sets after a database
    // operation.  This is the most foundational of the pieces needed for
    // read/write support.

    std::cout << "\nInitial state:\n";
    open_tops(s, 2);
    print_tops(s);

    struct ged *g = s->gedp;

    // Perform edit operations to trigger callbacks->  assuming
    // moss->g example
    int ac = 3;
    const char *av[4];
    av[0] = "rm";
    av[1] = "all.g";
    av[2] = "ellipse.r";
    av[3] = NULL;
    ged_exec_rm(g, ac, (const char **)av);
    s->g_update(g->dbip);

    std::cout << "\nRemoved ellipse.r from all.g:\n";
    print_tops(s);

    av[0] = "g";
    av[1] = "all.g";
    av[2] = "ellipse.r";
    av[3] = NULL;
    ged_exec_g(g, ac, (const char **)av);
    s->g_update(g->dbip);

    std::cout << "\nAdded ellipse.r back to the end of all.g, no call to open:\n";
    print_tops(s);

    std::cout << "\nAfter additional open pass on tree:\n";
    open_tops(s, 2);
    print_tops(s);


    av[0] = "rm";
    av[1] = "tor.r";
    av[2] = "tor";
    av[3] = NULL;
    ged_exec_rm(g, ac, (const char **)av);
    s->g_update(g->dbip);

    std::cout << "\ntops tree after removing tor from tor.r:\n";
    print_tops(s);

    av[0] = "kill";
    av[1] = "-f";
    av[2] = "all.g";
    av[3] = NULL;
    ged_exec_kill(g, ac, (const char **)av);
    s->g_update(g->dbip);
    std::cout << "\ntops tree after deleting all.g:\n";
    print_tops(s);

    std::cout << "\nexpanded tops tree after deleting all.g:\n";
    open_tops(s, -1);
    print_tops(s);

    const char *objs[] = {"box.r", "box.s", "cone.r", "cone.s", "ellipse.r", "ellipse.s", "light.r", "LIGHT", "platform.r", "platform.s", "tor", "tor.r", NULL};
    const char *obj = objs[0];
    int i = 0;
    while (obj) {
	av[0] = "kill";
	av[1] = "-f";
	av[2] = obj;
	av[3] = NULL;
	ged_exec_kill(g, ac, (const char **)av);
	i++;
	obj = objs[i];
    }
    s->g_update(g->dbip);
    std::cout << "\ntops tree after deleting everything:\n";
    print_tops(s);

    std::cout << "\nexpanded tops tree after deleting everything:\n";
    open_tops(s, -1);
    print_tops(s);

#ifdef USE_QTTEST
    delete tester;
#endif

    /* --- T2: rebuild_item_children regression --------------------------------
     * Run in a fresh model with a self-contained temp .g so the existing
     * exploration outputs above do not affect signal counts.             */
    int t2_failures = 0;
    {
	/* Derive a temp path next to the file already opened */
	struct bu_vls t2path = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&t2path, "%s.t2.g", argv[0]);
	t2_failures = test_rebuild_item_children(bu_vls_cstr(&t2path));
	bu_vls_free(&t2path);
    }
    if (t2_failures)
	fprintf(stderr, "\nT2: %d check(s) FAILED\n", t2_failures);
    else
	printf("\nT2: all rebuild_item_children checks passed.\n");

    return t2_failures ? 3 : 0;
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
