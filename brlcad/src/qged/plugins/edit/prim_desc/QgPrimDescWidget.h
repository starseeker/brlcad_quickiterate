/*               Q G P R I M D E S C W I D G E T . H
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
/** @file QgPrimDescWidget.h
 *
 * Auto-generated primitive edit / create panel driven by ft_edit_desc().
 *
 * The widget supports two complementary workflows:
 *
 *  EDIT existing object
 *    When a solid is selected in the scene (QG_VIEW_SELECT arrives) the
 *    panel rebuilds itself for that primitive type, shows the current
 *    object name, and each "Apply" button pushes the widget values into
 *    s->e_para[] then calls rt_edit_set_edflag() + rt_edit_process().
 *
 *  CREATE new object
 *    A scrollable bar of push buttons at the top of the panel lists every
 *    primitive type that has an ft_edit_desc() descriptor.  Clicking one
 *    switches the panel to "create" mode for that type: the user fills in
 *    an object name and the parameter values, then presses "Create".
 *    Creation uses OBJ[type].ft_make() to initialise a default
 *    rt_db_internal, writes it to the database with wdb_put_internal(),
 *    then emits QG_VIEW_DB to refresh the scene.
 *    Starting an rt_edit session after creation is planned future work.
 *
 * Widget hierarchy (top to bottom):
 *   ┌ type_scroll   : scrollable bar of per-type QPushButtons (always visible)
 *   ├ mode_label    : "Creating new <type>" or "Editing <name>"
 *   ├ name_row      : "Name:" QLineEdit + "Create" QPushButton
 *   └ scroll_area   : QGroupBox per category > per-command rows
 *
 * Per-command row:
 *   bold label | QFormLayout of param widgets | "Apply" button
 *
 * Param widget types:
 *   SCALAR  -> QDoubleSpinBox (range, units suffix)
 *   INTEGER -> QSpinBox       (range)
 *   BOOLEAN -> QCheckBox
 *   POINT   -> 3x QDoubleSpinBox with X/Y/Z labels
 *   VECTOR  -> 3x QDoubleSpinBox with X/Y/Z labels
 *   STRING  -> QLineEdit (+ file-browser button when prim_field is a filename)
 *   ENUM    -> QComboBox populated from enum_labels/enum_ids
 *   COLOR   -> QPushButton that opens QColorDialog
 *   MATRIX  -> 4x4 QDoubleSpinBox grid (identity default)
 */

#ifndef QGPRIMDESCWIDGET_H
#define QGPRIMDESCWIDGET_H

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMap>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#ifndef Q_MOC_RUN
#  include "rt/edit.h"    /* rt_edit, rt_edit_prim_desc, RT_EDIT_PARAM_* */
#endif

/**
 * Auto-generated primitive parameter edit / create panel.
 *
 * See file-level comment for detailed design description.
 */
class QgPrimDescWidget : public QWidget
{
    Q_OBJECT

public:
    explicit QgPrimDescWidget(QWidget *parent = nullptr);
    ~QgPrimDescWidget();

    /**
     * Build (or rebuild) the parameter panel for a given primitive type.
     *
     * Looks up the descriptor from EDOBJ[prim_type_id].ft_edit_desc().
     * If prim_type_id is -1, or the type has no descriptor, a placeholder
     * message is shown instead.
     *
     * Does nothing if prim_type_id equals the currently displayed type.
     */
    void setPrimType(int prim_type_id);

    /**
     * Link this widget to a live rt_edit session.
     *
     * When s is non-NULL Apply buttons are fully functional; pass NULL to
     * disconnect (Apply buttons remain visible but greyed out).  The widget
     * does NOT take ownership of s.
     */
    void setEditState(struct rt_edit *s);

signals:
    /**
     * Emitted after rt_edit_process() or wdb_put_internal() modifies the
     * database or view.  flags follows the QgSignalFlags convention.
     */
    void view_updated(unsigned long long flags);

public slots:
    /**
     * Receives view-update events from the palette element.
     *
     * On QG_VIEW_SELECT: reads the current selection's leaf solid type and
     * calls setPrimType() if it differs from the current type, then updates
     * the name field and mode label for edit mode.
     */
    void do_view_update(unsigned long long flags);

private slots:
    /** Called by the "Create" button.  Creates a new database object using
     *  OBJ[type].ft_make() + wdb_put_internal(), then starts an rt_edit
     *  session on it so Apply buttons work immediately. */
    void createObject();

private:
    /* ----- Type-selector bar ----- */

    /** Build the scrollable type-button bar from the EDOBJ table. */
    void buildTypeBar();

    /** Highlight the button for type_id and clear all others. */
    void highlightTypeButton(int type_id);

    /* ----- Parameter panel ----- */

    /** Delete all dynamically generated group boxes and their contents. */
    void clearContent();

    /** Populate content_widget from desc. */
    void buildFromDesc(const struct rt_edit_prim_desc *desc);

    /**
     * Build the widget row for a single rt_edit_cmd_desc.
     * Registers the row in param_widgets[] and cmd_descs[].
     * Returns the row widget.
     */
    QWidget *buildCommandRow(const struct rt_edit_cmd_desc *cmd);

    /**
     * Create a type-appropriate input widget for one rt_edit_param_desc.
     * See file-level comment for the type->widget mapping.
     */
    QWidget *buildParamWidget(const struct rt_edit_param_desc *p);

    /**
     * Read widget values into s->e_para[], set e_inpara, then call
     * rt_edit_set_edflag(cmd_id) + rt_edit_process().  Does nothing when
     * es == nullptr.
     */
    void applyCommand(const struct rt_edit_cmd_desc *cmd);

    /** Refresh create_btn enable state and mode_label text. */
    void refreshModeUI();

    /* ----- Core state ----- */
    struct rt_edit *es = nullptr;      /**< active edit session (not owned) */
    int   current_prim_type    = -1;   /**< type id currently displayed     */
    QString current_prim_label;        /**< e.g. "TOR", "ELL"               */

    /* ----- Top-level layout widgets ----- */
    QVBoxLayout *outer_layout  = nullptr;

    /* Type-selector bar */
    QScrollArea *type_scroll   = nullptr;
    QWidget     *type_bar      = nullptr;  /**< container for type buttons */

    /* Mode indicator and name/create row */
    QLabel      *mode_label    = nullptr;
    QWidget     *name_row      = nullptr;
    QLineEdit   *name_input    = nullptr;
    QPushButton *create_btn    = nullptr;

    /* Descriptor-driven parameter panel */
    QLabel      *status_label  = nullptr;  /**< shown when no descriptor */
    QScrollArea *scroll_area   = nullptr;
    QWidget     *content       = nullptr;
    QVBoxLayout *content_layout = nullptr;

    /* ----- Type-button registry ----- */
    /** Maps prim_type_id -> the corresponding type-selector button */
    QMap<int, QPushButton *> type_buttons;

    /* ----- Per-command widget tracking ----- */
    /** param_widgets[cmd_id] -> list of input widgets (one per param) */
    QMap<int, QList<QWidget *>> param_widgets;

    /** cmd_descs[cmd_id] -> static cmd descriptor (pointer into EDOBJ) */
    QMap<int, const struct rt_edit_cmd_desc *> cmd_descs;

    /** For RT_EDIT_PARAM_COLOR buttons: map button -> chosen colour */
    QMap<QPushButton *, QColor> color_store;
};

#endif /* QGPRIMDESCWIDGET_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
