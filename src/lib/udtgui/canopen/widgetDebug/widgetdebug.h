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

#ifndef WIDGETDEBUG_H
#define WIDGETDEBUG_H

#include "udtgui_global.h"

#include "node.h"
#include "nodeodsubscriber.h"
#include "p402ipwidget.h"
#include "p402optionwidget.h"
#include "p402vlwidget.h"
#include "p402tqwidget.h"
#include "profile/p402/nodeprofile402.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolBar>
#include <QWidget>

class UDTGUI_EXPORT WidgetDebug : public QWidget, public NodeOdSubscriber
{
    Q_OBJECT
public:
    WidgetDebug(QWidget *parent = nullptr);
    WidgetDebug(Node *node, QWidget *parent = nullptr);
    ~WidgetDebug() override;

    Node *node() const;

public slots:
    void setNode(Node *value);
    void updateData();

    void start();
    void stop();
    void gotoStateOEClicked();

private:
    void createWidgets();
    void setTimer(int ms);
    void readData();

    QTimer _timer;
    QTimer _operationEnabledTimer;

    Node *_node;

    enum State
    {
        STATE_NotReadyToSwitchOn = 1,
        STATE_SwitchOnDisabled = 2,
        STATE_ReadyToSwitchOn = 3,
        STATE_SwitchedOn = 4,
        STATE_OperationEnabled = 5,
        STATE_QuickStopActive = 6,
        STATE_FaultReactionActive = 7,
        STATE_Fault = 8,
    };

    NodeObjectId _controlWordObjectId;
    NodeObjectId _statusWordObjectId;

    NodeProfile402 *_nodeProfile402;

    QStackedWidget *_stackedWidget;
    P402OptionWidget *_p402Option;
    P402VlWidget *_p402vl;
    P402IpWidget *_p402ip;
    P402TqWidget *_p402tq;

    QToolBar *_nmtToolBar;
    QSpinBox *_logTimerSpinBox;

    QGroupBox *_modeGroupBox;
    QGroupBox *_stateMachineGroupBox;
    QGroupBox *_controlWordGroupBox;
    QGroupBox *_statusWordGroupBox;

    QComboBox *_modeComboBox;
    QList<NodeProfile402::Mode> _listModeComboBox;
    void modeChanged();
    void stateChanged();
    void isHalted(bool state);
    void eventHappened(quint8 event);

    QLabel *_controlWordLabel;
    QPushButton *_haltPushButton;

    QLabel *_statusWordRawLabel;
    QLabel *_statusWordLabel;
    QLabel *_informationLabel;
    QLabel *_warningLabel;
    QButtonGroup *_stateMachineGroup;

    void displayOption402();
    void modeIndexChanged(int id);
    void stateMachineClicked(int id);
    void haltClicked();

    void setCheckableStateMachine(int id);

    // NodeOdSubscriber interface
protected:
    void odNotify(const NodeObjectId &objId, SDO::FlagsRequest flags) override;
    // QObject interface
public:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
};

#endif // WIDGETDEBUG_H
