/*                  Q G S E L E C T F I L T E R . H
 * BRL-CAD
 *
 * Copyright (c) 2021-2026 United States Government as represented by
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
/** @file QgMeasureFilter.h
 *
 *  Qt mouse filters for graphically selecting elements in a view.
 */

#ifndef QGSELECTFILTER_H
#define QGSELECTFILTER_H

#include "common.h"

extern "C" {
#include "bu/color.h"
#include "bu/ptbl.h"
#include "bg/polygon.h"
#include "bv.h"
#include "raytrace.h"
}

#include <QBoxLayout>
#include <QEvent>
#include <QMouseEvent>
#include <QWidget>
#include "qtcad/defines.h"
#include "qtcad/QgViewFilter.h"

// Filters designed for specific editing modes
class QTCAD_EXPORT QgSelectFilter : public QgViewFilter {
	Q_OBJECT
	Q_DISABLE_COPY_MOVE(QgSelectFilter)


    public:
	QgSelectFilter() = default;
	// Primary mouse interaction.  This differs a bit for the
	// various selection types, hence the virtual definition
	// in the base class.
	bool eventFilter(QObject *, QEvent *) override
	{
		return false;
	}

	struct bu_ptbl selected_set = BU_PTBL_INIT_ZERO;

	// Whenever we're doing selections, we may want either all the objects
	// that match the selection criteria, or just the "closest" object.
	// Default behavior is to return everything, but this flag allows the
	// caller to request the more limited result as well.
	bool first_only = false;

};

class QTCAD_EXPORT QgSelectPntFilter: public QgSelectFilter {
	Q_OBJECT
	Q_DISABLE_COPY_MOVE(QgSelectPntFilter)


public:
	bool eventFilter(QObject *, QEvent *e) override;
};

class QTCAD_EXPORT QgSelectBoxFilter: public QgSelectFilter {
	Q_OBJECT
	Q_DISABLE_COPY_MOVE(QgSelectBoxFilter)


public:
	bool eventFilter(QObject *, QEvent *e) override;

private:
	fastf_t px = -FLT_MAX;
	fastf_t py = -FLT_MAX;
};

class QTCAD_EXPORT QgSelectRayFilter: public QgSelectFilter {
	Q_OBJECT
	Q_DISABLE_COPY_MOVE(QgSelectRayFilter)


public:
	bool eventFilter(QObject *, QEvent *e) override;

	struct db_i *dbip = nullptr;
};

#endif /* QGSELECTFILTER_H */

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
