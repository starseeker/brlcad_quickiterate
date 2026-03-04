/*              Q G P R I M D E S C W I D G E T . C P P
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
/** @file QgPrimDescWidget.cpp
 *
 * Auto-generated primitive edit / create panel.
 * See QgPrimDescWidget.h for design overview.
 */

#include "common.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <QApplication>
#include <QColor>
#include <QColorDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSizePolicy>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#ifndef Q_MOC_RUN
#  include "bu/log.h"
#  include "bu/malloc.h"
#  include "raytrace.h"           /* OBJ[], EDOBJ[], rt_db_internal, ... */
#  include "rt/functab.h"         /* OBJ, EDOBJ declarations              */
#  include "rt/edit.h"            /* RT_EDIT_PARAM_*, rt_edit_set_edflag, ... */
#  include "rt/wdb.h"             /* wdb_dbopen, wdb_put_internal */
#  include "ged.h"                /* struct ged */
#  include "../../../QgEdApp.h"   /* QgEdApp, QgModel */
#  include "../../../../libged/dbi.h" /* DbiState, BSelectState */
#endif

#include "qtcad/QgSignalFlags.h"
#include "QgPrimDescWidget.h"


/* ======================================================================
 * Construction / destruction
 * ====================================================================== */

QgPrimDescWidget::QgPrimDescWidget(QWidget *parent)
    : QWidget(parent)
{
    outer_layout = new QVBoxLayout(this);
    outer_layout->setContentsMargins(4, 4, 4, 4);
    outer_layout->setSpacing(4);

    /* --- Type-selector bar --- */
    type_scroll = new QScrollArea(this);
    type_scroll->setWidgetResizable(true);
    type_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    type_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    type_scroll->setFixedHeight(38);
    outer_layout->addWidget(type_scroll);

    type_bar = new QWidget;
    type_bar->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    type_scroll->setWidget(type_bar);
    buildTypeBar();  /* populate type_bar with one button per type */

    /* --- Mode label --- */
    mode_label = new QLabel(tr("Click a primitive type to begin."), this);
    mode_label->setAlignment(Qt::AlignCenter);
    outer_layout->addWidget(mode_label);

    /* --- Name input row --- */
    name_row = new QWidget(this);
    QHBoxLayout *nrl = new QHBoxLayout(name_row);
    nrl->setContentsMargins(0, 0, 0, 0);
    nrl->addWidget(new QLabel(tr("Name:"), name_row));
    name_input = new QLineEdit(name_row);
    name_input->setPlaceholderText(tr("object name"));
    nrl->addWidget(name_input, 1);
    create_btn = new QPushButton(tr("Create"), name_row);
    create_btn->setEnabled(false);
    create_btn->setToolTip(
tr("Write a new primitive with these parameters to the database"));
    QObject::connect(create_btn, &QPushButton::clicked,
     this, &QgPrimDescWidget::createObject);
    nrl->addWidget(create_btn);
    name_row->setLayout(nrl);
    outer_layout->addWidget(name_row);

    /* --- Descriptor-driven parameter panel --- */
    status_label = new QLabel(
tr("Click a primitive type above to see its edit parameters."), this);
    status_label->setAlignment(Qt::AlignCenter);
    status_label->setWordWrap(true);
    outer_layout->addWidget(status_label);

    scroll_area = new QScrollArea(this);
    scroll_area->setWidgetResizable(true);
    scroll_area->setVisible(false);
    outer_layout->addWidget(scroll_area, 1);

    content = new QWidget;
    content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(2, 2, 2, 2);
    content_layout->setSpacing(6);
    content_layout->addStretch(1);
    scroll_area->setWidget(content);

    setLayout(outer_layout);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
}


QgPrimDescWidget::~QgPrimDescWidget()
{
    param_widgets.clear();
    cmd_descs.clear();
    color_store.clear();
    type_buttons.clear();
}


/* ======================================================================
 * Public interface
 * ====================================================================== */

void
QgPrimDescWidget::setEditState(struct rt_edit *s)
{
    es = s;
    refreshModeUI();
}


void
QgPrimDescWidget::setPrimType(int prim_type_id)
{
    if (prim_type_id == current_prim_type)
return;

    current_prim_type = prim_type_id;
    clearContent();
    highlightTypeButton(prim_type_id);

    if (prim_type_id < 0) {
current_prim_label.clear();
status_label->setText(
    tr("Click a primitive type above to see its edit parameters."));
status_label->setVisible(true);
scroll_area->setVisible(false);
refreshModeUI();
return;
    }

    /* Walk EDOBJ to find the matching entry. */
    const struct rt_edit_prim_desc *desc = nullptr;
    for (int i = 0; EDOBJ[i].magic == RT_FUNCTAB_MAGIC; i++) {
if (i == prim_type_id) {
    if (EDOBJ[i].ft_edit_desc)
desc = (*EDOBJ[i].ft_edit_desc)();
    break;
}
    }

    if (desc && desc->prim_type) {
/* Make the label upper-case for display */
current_prim_label = QString::fromLatin1(desc->prim_type).toUpper();
    } else {
current_prim_label = QString::number(prim_type_id);
    }

    if (!desc) {
status_label->setText(
    tr("No parameter descriptor available for the selected primitive.\n"
       "Use the console to edit it via 'sed'."));
status_label->setVisible(true);
scroll_area->setVisible(false);
refreshModeUI();
return;
    }

    buildFromDesc(desc);
    status_label->setVisible(false);
    scroll_area->setVisible(true);
    refreshModeUI();
}


/* ======================================================================
 * View-update slot: detect selection changes and rebuild if needed
 * ====================================================================== */

void
QgPrimDescWidget::do_view_update(unsigned long long flags)
{
    if (!(flags & QG_VIEW_SELECT))
return;

    QgEdApp *app = qobject_cast<QgEdApp *>(qApp);
    if (!app || !app->mdl || !app->mdl->gedp || !app->mdl->gedp->dbi_state)
return;

    DbiState   *dbis = (DbiState *)app->mdl->gedp->dbi_state;
    BSelectState *ss = dbis->find_selected_state(nullptr);

    if (!ss || ss->list_selected_paths().empty()) {
/* Selection cleared - stay on current type (user may be creating) */
if (es) {
    setEditState(nullptr);
}
return;
    }

    std::vector<std::string> paths = ss->list_selected_paths();

    /* Take the first selected path and read its leaf solid type. */
    struct db_full_path dfp;
    db_full_path_init(&dfp);
    if (db_string_to_path(&dfp, app->mdl->gedp->dbip, paths[0].c_str()) != 0) {
db_free_full_path(&dfp);
return;
    }

    struct directory *leaf = DB_FULL_PATH_CUR_DIR(&dfp);
    int new_type = (leaf && (leaf->d_flags & RT_DIR_SOLID))
       ? (int)leaf->d_minor_type
       : -1;

    /* Populate name field with the selected object's name */
    if (leaf && new_type >= 0)
name_input->setText(QString::fromLatin1(leaf->d_namep));

    db_free_full_path(&dfp);

    /* Switch panel to the selected type (no-op if already showing it) */
    setPrimType(new_type);
}


/* ======================================================================
 * Slot: Create new object
 * ====================================================================== */

void
QgPrimDescWidget::createObject()
{
    if (current_prim_type < 0)
return;

    QString qname = name_input->text().trimmed();
    if (qname.isEmpty()) {
bu_log("QgPrimDescWidget: enter an object name before creating.\n");
return;
    }

    QgEdApp *app = qobject_cast<QgEdApp *>(qApp);
    if (!app || !app->mdl || !app->mdl->gedp)
return;
    struct ged *gedp = app->mdl->gedp;
    struct db_i *dbip = gedp->dbip;

    /* Check the type has an ft_make entry in OBJ[]. */
    if (!OBJ[current_prim_type].ft_make) {
bu_log("QgPrimDescWidget: primitive type %d has no ft_make; cannot create.\n",
       current_prim_type);
return;
    }

    const char *name = qname.toLocal8Bit().constData();

    /* Refuse to clobber an existing object. */
    if (db_lookup(dbip, name, LOOKUP_QUIET) != RT_DIR_NULL) {
bu_log("QgPrimDescWidget: object '%s' already exists; rename it first.\n",
       name);
return;
    }

    /* Build a default rt_db_internal via ft_make. */
    struct rt_db_internal intern;
    RT_DB_INTERNAL_INIT(&intern);
    OBJ[current_prim_type].ft_make(&OBJ[current_prim_type], &intern);

    /* Write to the database. */
    struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_DEFAULT);
    if (!wdbp) {
rt_db_free_internal(&intern);
bu_log("QgPrimDescWidget: could not open wdb handle.\n");
return;
    }

    if (wdb_put_internal(wdbp, name, &intern, 1.0) < 0) {
rt_db_free_internal(&intern);
bu_log("QgPrimDescWidget: wdb_put_internal failed for '%s'.\n", name);
return;
    }
    rt_db_free_internal(&intern);

    bu_log("QgPrimDescWidget: created '%s' (type %d).\n",
   name, current_prim_type);

    emit view_updated(QG_VIEW_DB);
}


/* ======================================================================
 * Internal: type-selector bar
 * ====================================================================== */

void
QgPrimDescWidget::buildTypeBar()
{
    QHBoxLayout *bl = new QHBoxLayout(type_bar);
    bl->setContentsMargins(2, 2, 2, 2);
    bl->setSpacing(2);

    for (int i = 0; EDOBJ[i].magic == RT_FUNCTAB_MAGIC; i++) {
if (!EDOBJ[i].ft_edit_desc)
    continue;

const struct rt_edit_prim_desc *desc = (*EDOBJ[i].ft_edit_desc)();
if (!desc || !desc->prim_type)
    continue;

QString label = QString::fromLatin1(desc->prim_type).toUpper();
QPushButton *btn = new QPushButton(label, type_bar);
btn->setCheckable(true);
btn->setFlat(true);
btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
btn->setToolTip(
    desc->prim_label
? QString::fromLatin1(desc->prim_label)
: label);

const int type_id = i;
QObject::connect(btn, &QPushButton::clicked, this,
 [this, type_id]() {
     setPrimType(type_id);
 });

bl->addWidget(btn);
type_buttons[i] = btn;
    }

    bl->addStretch(1);
    type_bar->setLayout(bl);
}


void
QgPrimDescWidget::highlightTypeButton(int type_id)
{
    for (auto it = type_buttons.begin(); it != type_buttons.end(); ++it) {
it.value()->setChecked(it.key() == type_id);
    }
}


/* ======================================================================
 * Internal: mode UI helpers
 * ====================================================================== */

void
QgPrimDescWidget::refreshModeUI()
{
    if (current_prim_type < 0) {
mode_label->setText(tr("Click a primitive type to begin."));
create_btn->setEnabled(false);
return;
    }

    if (es) {
/* Edit mode: an rt_edit session is active */
QString obj_name = name_input->text().trimmed();
if (obj_name.isEmpty())
    obj_name = tr("<unknown>");
mode_label->setText(
    tr("Editing <b>%1</b> (%2)")
.arg(obj_name)
.arg(current_prim_label));
create_btn->setEnabled(true);
create_btn->setText(tr("Create new"));
    } else {
/* Create mode: no rt_edit session */
mode_label->setText(
    tr("Creating new <b>%1</b> — fill in a name and press Create")
.arg(current_prim_label));
create_btn->setEnabled(true);
create_btn->setText(tr("Create"));
    }
}


/* ======================================================================
 * Internal: parameter panel construction
 * ====================================================================== */

void
QgPrimDescWidget::clearContent()
{
    param_widgets.clear();
    cmd_descs.clear();
    color_store.clear();

    /* Delete all child widgets from content, except the trailing stretch. */
    QLayoutItem *item;
    while ((item = content_layout->takeAt(0)) != nullptr) {
if (item->widget())
    item->widget()->deleteLater();
delete item;
    }
    content_layout->addStretch(1);
}


void
QgPrimDescWidget::buildFromDesc(const struct rt_edit_prim_desc *desc)
{
    if (!desc || desc->ncmd == 0)
return;

    /* Group commands by category, preserving first-seen order. */
    std::vector<std::string> cat_order;
    std::map<std::string, std::vector<const struct rt_edit_cmd_desc *>> by_cat;

    for (int i = 0; i < desc->ncmd; i++) {
const struct rt_edit_cmd_desc *cmd = &desc->cmds[i];
const char *cat = cmd->category ? cmd->category : "";
if (by_cat.find(cat) == by_cat.end())
    cat_order.push_back(cat);
by_cat[cat].push_back(cmd);
    }

    /* Sort each category by display_order (stable; secondary by array order). */
    for (auto &kv : by_cat) {
std::stable_sort(kv.second.begin(), kv.second.end(),
 [](const struct rt_edit_cmd_desc *a,
    const struct rt_edit_cmd_desc *b) {
     return a->display_order < b->display_order;
 });
    }

    /* Remove trailing stretch before inserting group boxes. */
    int last = content_layout->count() - 1;
    QLayoutItem *stretch = (last >= 0) ? content_layout->takeAt(last) : nullptr;

    for (const std::string &cat : cat_order) {
QString cat_label = cat.empty() ? tr("General")
: QString::fromStdString(cat);
/* Capitalize first letter. */
if (!cat_label.isEmpty())
    cat_label[0] = cat_label[0].toUpper();

QGroupBox *group = new QGroupBox(cat_label, content);
QVBoxLayout *gl  = new QVBoxLayout;
gl->setContentsMargins(6, 6, 6, 6);
gl->setSpacing(4);

for (const struct rt_edit_cmd_desc *cmd : by_cat[cat])
    gl->addWidget(buildCommandRow(cmd));

group->setLayout(gl);
content_layout->addWidget(group);
    }

    if (stretch)
content_layout->addItem(stretch);
}


QWidget *
QgPrimDescWidget::buildCommandRow(const struct rt_edit_cmd_desc *cmd)
{
    QWidget *row = new QWidget(content);
    QVBoxLayout *rl = new QVBoxLayout(row);
    rl->setContentsMargins(2, 2, 2, 2);
    rl->setSpacing(2);

    /* Command label (bold) */
    QLabel *lbl = new QLabel(
QString("<b>%1</b>").arg(QString::fromLatin1(cmd->label)), row);
    rl->addWidget(lbl);

    /* Parameter widgets */
    QFormLayout *fl = new QFormLayout;
    fl->setHorizontalSpacing(8);
    fl->setVerticalSpacing(2);
    fl->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    QList<QWidget *> pwidgets;
    for (int pi = 0; pi < cmd->nparam; pi++) {
const struct rt_edit_param_desc *p = &cmd->params[pi];
QWidget *pw = buildParamWidget(p);
const char *plabel = (p->label && p->label[0]) ? p->label : p->name;
fl->addRow(QString::fromLatin1(plabel) + ":", pw);
pwidgets.append(pw);
    }
    rl->addLayout(fl);

    /* Apply button */
    QPushButton *apply_btn = new QPushButton(tr("Apply"), row);
    apply_btn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    apply_btn->setToolTip(
tr("Apply these parameters to the current edit session"));

    const struct rt_edit_cmd_desc *cap = cmd;
    QObject::connect(apply_btn, &QPushButton::clicked, this,
     [this, cap]() { applyCommand(cap); });

    QHBoxLayout *ahl = new QHBoxLayout;
    ahl->addStretch(1);
    ahl->addWidget(apply_btn);
    rl->addLayout(ahl);

    /* Thin separator */
    QFrame *line = new QFrame(row);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    rl->addWidget(line);

    row->setLayout(rl);

    param_widgets[cmd->cmd_id] = pwidgets;
    cmd_descs[cmd->cmd_id]     = cmd;

    return row;
}


QWidget *
QgPrimDescWidget::buildParamWidget(const struct rt_edit_param_desc *p)
{
    const bool no_min = (p->range_min <= RT_EDIT_PARAM_NO_LIMIT * 0.5);
    const bool no_max = (p->range_max <= RT_EDIT_PARAM_NO_LIMIT * 0.5);

    switch (p->type) {

/* ---------------------------------------------------------------- */
case RT_EDIT_PARAM_SCALAR: {
    QDoubleSpinBox *sb = new QDoubleSpinBox;
    sb->setDecimals(4);
    sb->setSingleStep(0.1);
    sb->setMinimum(no_min ? -1e12 : p->range_min);
    sb->setMaximum(no_max ?  1e12 : p->range_max);
    sb->setValue(0.0);
    if (p->units && p->units[0])
sb->setSuffix(QString(" ") + QString::fromLatin1(p->units));
    if (p->name)
sb->setObjectName(QString::fromLatin1(p->name));
    return sb;
}

/* ---------------------------------------------------------------- */
case RT_EDIT_PARAM_INTEGER: {
    QSpinBox *sb = new QSpinBox;
    sb->setMinimum(no_min ? (int)-1e6 : (int)p->range_min);
    sb->setMaximum(no_max ? (int) 1e6 : (int)p->range_max);
    sb->setValue(0);
    if (p->name)
sb->setObjectName(QString::fromLatin1(p->name));
    return sb;
}

/* ---------------------------------------------------------------- */
case RT_EDIT_PARAM_BOOLEAN: {
    QCheckBox *cb = new QCheckBox;
    cb->setChecked(false);
    if (p->label && p->label[0])
cb->setText(QString::fromLatin1(p->label));
    if (p->name)
cb->setObjectName(QString::fromLatin1(p->name));
    return cb;
}

/* ---------------------------------------------------------------- */
case RT_EDIT_PARAM_POINT:
case RT_EDIT_PARAM_VECTOR: {
    QWidget *w = new QWidget;
    QHBoxLayout *hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(4);
    const char *axis_labels[] = {"X:", "Y:", "Z:"};
    for (int k = 0; k < 3; k++) {
hl->addWidget(new QLabel(QString::fromLatin1(axis_labels[k])));
QDoubleSpinBox *sb = new QDoubleSpinBox;
sb->setDecimals(4);
sb->setSingleStep(0.1);
sb->setMinimum(-1e12);
sb->setMaximum( 1e12);
sb->setValue(0.0);
if (p->units && p->units[0])
    sb->setSuffix(QString(" ") + QString::fromLatin1(p->units));
hl->addWidget(sb);
    }
    w->setLayout(hl);
    if (p->name)
w->setObjectName(QString::fromLatin1(p->name));
    return w;
}

/* ---------------------------------------------------------------- */
case RT_EDIT_PARAM_STRING: {
    QWidget *w = new QWidget;
    QHBoxLayout *hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(4);

    QLineEdit *le = new QLineEdit;
    le->setObjectName("string_input");
    if (p->prim_field && p->prim_field[0])
le->setPlaceholderText(
    tr("(field: %1)").arg(QString::fromLatin1(p->prim_field)));
    hl->addWidget(le, 1);

    /* File-browser button when the field looks like a filename. */
    if (p->prim_field) {
const QString field(p->prim_field);
const bool file_like =
    field.contains("name", Qt::CaseInsensitive) ||
    field.contains("file", Qt::CaseInsensitive) ||
    field.contains("fname", Qt::CaseInsensitive);
if (file_like) {
    QToolButton *btn = new QToolButton;
    btn->setText(QStringLiteral("..."));
    btn->setToolTip(tr("Browse for file"));
    QObject::connect(btn, &QToolButton::clicked, [le]() {
QString fn = QFileDialog::getOpenFileName(
    nullptr, QObject::tr("Select file"));
if (!fn.isEmpty())
    le->setText(fn);
    });
    hl->addWidget(btn);
}
    }

    w->setLayout(hl);
    if (p->name)
w->setObjectName(QString::fromLatin1(p->name));
    return w;
}

/* ---------------------------------------------------------------- */
case RT_EDIT_PARAM_ENUM: {
    QComboBox *cb = new QComboBox;
    for (int ei = 0; ei < p->nenum; ei++) {
const char *elbl = (p->enum_labels && p->enum_labels[ei])
       ? p->enum_labels[ei] : "(?)";
cb->addItem(QString::fromLatin1(elbl));
    }
    cb->setCurrentIndex(0);
    if (p->name)
cb->setObjectName(QString::fromLatin1(p->name));
    return cb;
}

/* ---------------------------------------------------------------- */
case RT_EDIT_PARAM_COLOR: {
    QPushButton *btn = new QPushButton;
    btn->setObjectName("color_button");
    QColor initial(255, 255, 255);
    color_store[btn] = initial;

    /* Helper lambda to update the button's swatch appearance. */
    auto update_swatch = [this, btn]() {
const QColor &c = color_store[btn];
btn->setStyleSheet(
    QString("background-color: rgb(%1,%2,%3); border: 1px solid #888;")
.arg(c.red()).arg(c.green()).arg(c.blue()));
btn->setText(
    QString("(%1, %2, %3)").arg(c.red()).arg(c.green()).arg(c.blue()));
    };
    update_swatch();

    QObject::connect(btn, &QPushButton::clicked, this,
     [this, btn, update_swatch]() {
 QColor chosen = QColorDialog::getColor(
     color_store[btn], nullptr, tr("Choose Colour"));
 if (chosen.isValid()) {
     color_store[btn] = chosen;
     update_swatch();
 }
     });

    if (p->name)
btn->setObjectName(QString::fromLatin1(p->name));
    return btn;
}

/* ---------------------------------------------------------------- */
case RT_EDIT_PARAM_MATRIX: {
    /* 4x4 grid of QDoubleSpinBox (row-major, identity default). */
    QWidget *w = new QWidget;
    QGridLayout *grid = new QGridLayout(w);
    grid->setSpacing(2);
    grid->setContentsMargins(0, 0, 0, 0);
    for (int row = 0; row < 4; row++) {
for (int col = 0; col < 4; col++) {
    QDoubleSpinBox *sb = new QDoubleSpinBox;
    sb->setDecimals(6);
    sb->setSingleStep(0.01);
    sb->setMinimum(-1e12);
    sb->setMaximum( 1e12);
    sb->setValue((row == col) ? 1.0 : 0.0);
    sb->setFixedWidth(80);
    grid->addWidget(sb, row, col);
}
    }
    w->setLayout(grid);
    if (p->name)
w->setObjectName(QString::fromLatin1(p->name));
    return w;
}

/* ---------------------------------------------------------------- */
default: {
    return new QLabel(tr("<i>(unsupported type %1)</i>").arg(p->type));
}
    }
}


/* ======================================================================
 * Internal: Apply command
 * ====================================================================== */

void
QgPrimDescWidget::applyCommand(const struct rt_edit_cmd_desc *cmd)
{
    if (!es) {
bu_log("QgPrimDescWidget: no active edit state; Apply ignored.\n"
       "  (Create the object first, or select an existing one.)\n");
return;
    }

    const QList<QWidget *> &pwidgets = param_widgets.value(cmd->cmd_id);

    for (int i = 0; i < RT_EDIT_MAXPARA; i++)
es->e_para[i] = 0.0;
    es->e_inpara = 0;

    int max_idx = 0;

    for (int pi = 0; pi < cmd->nparam; pi++) {
const struct rt_edit_param_desc *p = &cmd->params[pi];
QWidget *w = (pi < pwidgets.size()) ? pwidgets[pi] : nullptr;
if (!w)
    continue;

switch (p->type) {

    case RT_EDIT_PARAM_SCALAR: {
QDoubleSpinBox *sb = qobject_cast<QDoubleSpinBox *>(w);
if (!sb) break;
es->e_para[p->index] = sb->value();
max_idx = std::max(max_idx, p->index + 1);
break;
    }

    case RT_EDIT_PARAM_INTEGER: {
QSpinBox *sb = qobject_cast<QSpinBox *>(w);
if (!sb) break;
es->e_para[p->index] = (fastf_t)sb->value();
max_idx = std::max(max_idx, p->index + 1);
break;
    }

    case RT_EDIT_PARAM_BOOLEAN: {
QCheckBox *cb = qobject_cast<QCheckBox *>(w);
if (!cb) break;
es->e_para[p->index] = cb->isChecked() ? 1.0 : 0.0;
max_idx = std::max(max_idx, p->index + 1);
break;
    }

    case RT_EDIT_PARAM_POINT:
    case RT_EDIT_PARAM_VECTOR: {
QList<QDoubleSpinBox *> sbs = w->findChildren<QDoubleSpinBox *>();
if (sbs.size() < 3) break;
es->e_para[p->index]     = sbs[0]->value();
es->e_para[p->index + 1] = sbs[1]->value();
es->e_para[p->index + 2] = sbs[2]->value();
max_idx = std::max(max_idx, p->index + 3);
break;
    }

    case RT_EDIT_PARAM_STRING:
/* String parameters are not routed through e_para.
 * They require primitive-specific helpers (future work: e_str). */
break;

    case RT_EDIT_PARAM_ENUM: {
QComboBox *cb = qobject_cast<QComboBox *>(w);
if (!cb || !p->enum_ids) break;
int cidx = cb->currentIndex();
if (cidx >= 0 && cidx < p->nenum)
    es->e_para[p->index] = (fastf_t)p->enum_ids[cidx];
max_idx = std::max(max_idx, p->index + 1);
break;
    }

    case RT_EDIT_PARAM_COLOR: {
QPushButton *btn = qobject_cast<QPushButton *>(w);
if (!btn || !color_store.contains(btn)) break;
const QColor &c = color_store[btn];
es->e_para[p->index]     = (fastf_t)c.red();
es->e_para[p->index + 1] = (fastf_t)c.green();
es->e_para[p->index + 2] = (fastf_t)c.blue();
max_idx = std::max(max_idx, p->index + 3);
break;
    }

    case RT_EDIT_PARAM_MATRIX: {
/* 4x4 spin-box grid; values start at e_para[p->index]. */
QList<QDoubleSpinBox *> sbs = w->findChildren<QDoubleSpinBox *>();
for (int mi = 0; mi < 16 && mi < sbs.size(); mi++) {
    es->e_para[p->index + mi] = sbs[mi]->value();
    max_idx = std::max(max_idx, p->index + mi + 1);
}
break;
    }

    default:
break;
}
    }

    es->e_inpara = max_idx;

    rt_edit_set_edflag(es, cmd->cmd_id);
    rt_edit_process(es);

    emit view_updated(QG_VIEW_REFRESH);
}


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
