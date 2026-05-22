/*  T E S T _ Q G M O D E L _ M O D E L _ T E S T E R . C P P
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
/** @file test_qgmodel_model_tester.cpp
 *
 * Phase 8 model test coverage for QgModel:
 *  - headless QApplication initialization
 *  - QAbstractItemModelTester invariant checks
 *  - QSignalSpy coverage for model-open and layout-change signals
 */

#include "common.h"

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "bu/app.h"
#include "bu/log.h"

#include "qtcad/QgModel.h"

static int g_fail = 0;

#define TCHECK(cond, msg) \
    do { \
	if (!(cond)) { \
	    bu_log("FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg)); \
	    g_fail++; \
	} else { \
	    bu_log("OK   %s\n", (msg)); \
	} \
    } while (0)

int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);

    if (!qgetenv("QT_QPA_PLATFORM").size())
	qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    if (argc < 2) {
	bu_log("Usage: %s <path-to-moss.g>\n", argv[0]);
	return 1;
    }
    const char *g_path = argv[1];

    qRegisterMetaType<QgItem *>("QgItem *");

    QgModel model(nullptr, g_path);
    QAbstractItemModel *amodel = &model;

    QAbstractItemModelTester tester(amodel, QAbstractItemModelTester::FailureReportingMode::Warning);

    TCHECK(amodel->columnCount(QModelIndex()) == 1, "QgModel reports one display column");

    int top_rows = amodel->rowCount(QModelIndex());
    TCHECK(top_rows > 0, "QgModel has top-level rows for moss.g");

    if (top_rows > 0) {
	QSignalSpy opened_spy(&model, SIGNAL(opened_item(QgItem *)));
	QSignalSpy layout_spy(&model, SIGNAL(layoutChanged()));
	TCHECK(opened_spy.isValid(), "opened_item spy is valid");
	TCHECK(layout_spy.isValid(), "layoutChanged spy is valid");

	QModelIndex first = amodel->index(0, 0, QModelIndex());
	TCHECK(first.isValid(), "first top-level index is valid");

	bool can_fetch = amodel->canFetchMore(first);
	if (can_fetch)
	    amodel->fetchMore(first);
	QCoreApplication::processEvents();
	QTest::qWait(1);

	if (can_fetch) {
	    TCHECK(opened_spy.count() > 0, "fetchMore emits opened_item");
	    TCHECK(amodel->rowCount(first) > 0, "fetchMore populates first top-level item children");
	} else {
	    TCHECK(true, "first top-level item has no fetchable children");
	}

	model.toggle_hierarchy();
	QCoreApplication::processEvents();
	QTest::qWait(1);
	TCHECK(layout_spy.count() > 0, "toggle_hierarchy emits layoutChanged");
    }

    if (g_fail) {
	bu_log("\ntest_qgmodel_model_tester: FAILED (%d check(s))\n", g_fail);
	return 1;
    }
    bu_log("\ntest_qgmodel_model_tester: PASSED\n");
    return 0;
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
