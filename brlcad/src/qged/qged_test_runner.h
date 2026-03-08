/*                 Q G E D _ T E S T _ R U N N E R . H
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/** @file qged_test_runner.h
 *
 * QgedTestRunner Qt class declaration for the qged automated draw test.
 */

#ifndef QGED_TEST_RUNNER_H
#define QGED_TEST_RUNNER_H

#include <QObject>
#include <QString>

#include "QgEdApp.h"

class QgedTestRunner : public QObject
{
    Q_OBJECT
public:
    explicit QgedTestRunner(QgEdApp *app, const char *gfile,
    const QString &outdir, QObject *parent = nullptr);

public slots:
    void run();

public:
    bool m_pass = false;

private:
    QgEdApp    *m_app;
    const char *m_gfile;
    QString     m_outdir;
};

#endif /* QGED_TEST_RUNNER_H */

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
