/*
* Copyright (C) 2019 ~ 2020 Deepin Technology Co., Ltd.
*
* Author:     linxun <linxun@uniontech.com>
* Maintainer: linxun <linxun@uniontech.com>
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* any later version.
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef DMTREEVIEW_H
#define DMTREEVIEW_H

#include <DTreeView>
#include <QStandardItemModel>
#include <QWidget>
#include "dmtreeviewdelegate.h"
#include <QModelIndex>
#include <QMouseEvent>
#include "dmdiskinfobox.h"
#include <QSortFilterProxyModel>

DWIDGET_USE_NAMESPACE

class DmTreeview : public DTreeView
{
    Q_OBJECT
public:
    explicit DmTreeview(QWidget *parent = nullptr);
    void initUI();
    void initmodel();
    void initdelegate();
    void sort();
    void additem(QStandardItem *t_item, DiskInfoData &data);
    QStandardItem *getModelByIndex(const QModelIndex &index);
    QStandardItem *addtopitem(DiskInfoData &data);
    QStandardItem *getcuritem();
    QStandardItem *getRootItem();
    QModelIndex getRootItemIndex();
    QModelIndex setDefaultdmItem();

    void addItem(DmDiskinfoBox *infobox, QStandardItem *puritem = nullptr);
    void addTopItem(DmDiskinfoBox *mailbox);
    void addSubItem(DmDiskinfoBox *mailbox, QStandardItem *pcurItem);

    void showEvent(QShowEvent *event)override;
signals:
    void sigselectitem(const QModelIndex &index);
public slots:
private:
    QStandardItemModel *m_model {nullptr};
    QAbstractItemDelegate *m_delegate {nullptr};
    QSortFilterProxyModel *m_pSortViewFilter {nullptr};
protected:
    void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;
    void mousePressEvent(QMouseEvent *event) override;
};

#endif // DMTREEVIEW_H