/**
 ** This file is part of the UDTStudio project.
 ** Copyright 2019-2020 UniSwarm
 **
 ** This program is free software: you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, either version 3 of the License, or
 ** (at your option) any later version.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/

#ifndef BUSMANAGERWIDGET_H
#define BUSMANAGERWIDGET_H

#include "../udtgui_global.h"

#include <QWidget>

#include <QGroupBox>
#include <QLineEdit>
#include <QLabel>
#include <QToolBar>
#include <QSpinBox>

#include "canopenbus.h"

class UDTGUI_EXPORT BusManagerWidget : public QWidget
{
    Q_OBJECT
public:
    BusManagerWidget(QWidget *parent = nullptr);
    BusManagerWidget(CanOpenBus *bus, QWidget *parent = nullptr);

    CanOpenBus *bus() const;

signals:

public slots:
    void setBus(CanOpenBus *bus);
    void updateData();

    void exploreBus();
    void sendSyncOne();
    void toggleSync(bool start);

protected slots:
    void setSyncTimer(int i);

protected:
    void createWidgets();
    QGroupBox *_groupBox;
    QToolBar *_toolBar;
    QLineEdit *_busNameEdit;
    QLabel *_busTypeLabel;
    QLabel *_busStatusLabel;
    QSpinBox *_syncTimerSpinBox;

    CanOpenBus *_bus;
    QAction *_syncStartAction;
};

#endif // BUSMANAGERWIDGET_H
