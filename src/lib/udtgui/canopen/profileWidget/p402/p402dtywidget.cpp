/**
 ** This file is part of the UDTStudio project.
 ** Copyright 2019-2021 UniSwarm
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

#include "p402dtywidget.h"

#include "canopen/datalogger/dataloggerwidget.h"
#include "canopen/indexWidget/indexlabel.h"
#include "canopen/indexWidget/indexspinbox.h"

#include "profile/p402/modedty.h"
#include "profile/p402/nodeprofile402.h"
#include "services/rpdo.h"
#include "services/tpdo.h"

#include <QPushButton>

P402DtyWidget::P402DtyWidget(QWidget *parent)
    : P402ModeWidget(parent)
{
    createWidgets();
    _nodeProfile402 = nullptr;
}

void P402DtyWidget::readRealTimeObjects()
{
    _nodeProfile402->readRealTimeObjects();
}

void P402DtyWidget::readAllObjects()
{
    _nodeProfile402->readAllObjects();
}

void P402DtyWidget::reset()
{
    _modeDty->reset();
}

void P402DtyWidget::setNode(Node *node, uint8_t axis)
{
    if (node == nullptr)
    {
        return;
    }

    if (axis > 8)
    {
        return;
    }

    if (node != nullptr)
    {
        setNodeInterrest(node);

        if (!node->profiles().isEmpty())
        {
            _nodeProfile402 = dynamic_cast<NodeProfile402 *>(node->profiles()[axis]);
            _modeDty = dynamic_cast<ModeDty *>(_nodeProfile402->mode(NodeProfile402::OperationMode::DTY));
            connect(_enableRampCheckBox, &QCheckBox::clicked, _modeDty, &ModeDty::setEnableRamp);

            _targetObjectId = _modeDty->targetObjectId();
            registerObjId(_targetObjectId);

            _demandObjectId = _modeDty->demandObjectId();
            _demandLabel->setObjId(_demandObjectId);

            _slopeSpinBox->setObjId(_modeDty->slopeObjectId());
            _maxSpinBox->setObjId(_modeDty->maxObjectId());

            int max = _nodeProfile402->node()->nodeOd()->value(_maxSpinBox->objId()).toInt();
            _targetSlider->setRange(-max, max);
            _targetSlider->setTickInterval(max / 10);
            _sliderMinLabel->setNum(-max);
            _sliderMaxLabel->setNum(max);
        }

        connect(_maxSpinBox, &QSpinBox::editingFinished, this, &P402DtyWidget::updateMaxDty);
    }
}

void P402DtyWidget::updateFromSpinbox()
{
    qint16 value = static_cast<qint16>(_targetSpinBox->value());
    _modeDty->setTarget(value);
}

void P402DtyWidget::updateTargetFromSlider()
{
    qint16 value = static_cast<qint16>(_targetSlider->value());
    _modeDty->setTarget(value);
}

void P402DtyWidget::updateMaxDty()
{
    int max = _nodeProfile402->node()->nodeOd()->value(_maxSpinBox->objId()).toInt();
    _targetSlider->setRange(-max, max);
    _sliderMinLabel->setNum(-max);
    _sliderMaxLabel->setNum(max);
}

void P402DtyWidget::setZeroButton()
{
    _nodeProfile402->setTarget(0);
}

void P402DtyWidget::createDataLogger()
{
    DataLogger *dataLogger = new DataLogger();
    DataLoggerWidget *dataLoggerWidget = new DataLoggerWidget(dataLogger);
    dataLoggerWidget->setTitle(tr("Node %1 axis %2 DTY").arg(_nodeProfile402->nodeId()).arg(_nodeProfile402->axisId()));

    dataLogger->addData(_targetObjectId);
    dataLogger->addData(_demandObjectId);

    dataLoggerWidget->setAttribute(Qt::WA_DeleteOnClose);
    connect(dataLoggerWidget, &QObject::destroyed, dataLogger, &DataLogger::deleteLater);

    dataLoggerWidget->show();
    dataLoggerWidget->raise();
    dataLoggerWidget->activateWindow();
}

void P402DtyWidget::mapDefaultObjects()
{
    NodeObjectId controlWordObjectId = _nodeProfile402->controlWordObjectId();
    QList<NodeObjectId> dtyRpdoObjectList = {controlWordObjectId, _targetObjectId};
    _nodeProfile402->node()->rpdos().at(0)->writeMapping(dtyRpdoObjectList);

    NodeObjectId statusWordObjectId = _nodeProfile402->statusWordObjectId();
    QList<NodeObjectId> dtyTpdoObjectList = {statusWordObjectId, _demandObjectId};
    _nodeProfile402->node()->tpdos().at(0)->writeMapping(dtyTpdoObjectList);
}

void P402DtyWidget::createWidgets()
{
    QGroupBox *modeGroupBox = new QGroupBox(tr("Duty cycle mode"));
    _modeLayout = new QFormLayout();

    createTargetWidgets();
    createDemandWidgets();
    createLimitWidgets();

    QFrame *frame = new QFrame();
    frame->setFrameStyle(QFrame::HLine);
    frame->setFrameShadow(QFrame::Sunken);
    _modeLayout->addRow(frame);

    createSlopeWidgets();

    modeGroupBox->setLayout(_modeLayout);

    QWidget *widget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(modeGroupBox);
    layout->addWidget(createControlWordWidgets());

    QScrollArea *scrollArea = new QScrollArea;
    scrollArea->setWidget(widget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QVBoxLayout *vBoxLayout = new QVBoxLayout();
    vBoxLayout->addWidget(scrollArea);
    vBoxLayout->addLayout(createButtonWidgets());
    vBoxLayout->setContentsMargins(0, 0, 0, 0);
    setLayout(vBoxLayout);
}

void P402DtyWidget::createTargetWidgets()
{
    _targetSpinBox = new QSpinBox();
    _targetSpinBox->setRange(std::numeric_limits<qint16>::min(), std::numeric_limits<qint16>::max());
    _modeLayout->addRow(tr("&Target"), _targetSpinBox);

    QLayout *labelSliderLayout = new QHBoxLayout();

    _sliderMinLabel = new QLabel("min");
    labelSliderLayout->addWidget(_sliderMinLabel);
    labelSliderLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::MinimumExpanding, QSizePolicy::Minimum));
    labelSliderLayout->addWidget(new QLabel("0"));
    labelSliderLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::MinimumExpanding, QSizePolicy::Minimum));

    _sliderMaxLabel = new QLabel("max");
    labelSliderLayout->addWidget(_sliderMaxLabel);
    _modeLayout->addRow(labelSliderLayout);

    _targetSlider = new QSlider(Qt::Horizontal);
    _targetSlider->setTickPosition(QSlider::TicksBelow);
    _modeLayout->addRow(_targetSlider);

    connect(_targetSlider, &QSlider::valueChanged, this, &P402DtyWidget::updateTargetFromSlider);
    connect(_targetSpinBox, &QSpinBox::editingFinished, this, &P402DtyWidget::updateFromSpinbox);

    QPushButton *setZeroButton = new QPushButton();
    setZeroButton->setText("Set to 0");
    connect(setZeroButton, &QPushButton::clicked, this, &P402DtyWidget::setZeroButton);

    QLayout *setZeroLayout = new QHBoxLayout();
    setZeroLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::MinimumExpanding, QSizePolicy::Minimum));
    setZeroLayout->addWidget(setZeroButton);
    setZeroLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::MinimumExpanding, QSizePolicy::Minimum));
    _modeLayout->addRow(setZeroLayout);
}

void P402DtyWidget::createDemandWidgets()
{
    _demandLabel = new IndexLabel();
    _modeLayout->addRow(tr("Demand:"), _demandLabel);
}

void P402DtyWidget::createLimitWidgets()
{
    _maxSpinBox = new IndexSpinBox();
    _modeLayout->addRow(tr("Ma&x "), _maxSpinBox);
}

void P402DtyWidget::createSlopeWidgets()
{
    _slopeSpinBox = new IndexSpinBox();
    _modeLayout->addRow(tr("Target &slope "), _slopeSpinBox);
}

QGroupBox *P402DtyWidget::createControlWordWidgets()
{
    // Group Box CONTROL WORD
    QGroupBox *groupBox = new QGroupBox(tr("Control word:"));
    QFormLayout *layout = new QFormLayout();

    _enableRampCheckBox = new QCheckBox();
    _enableRampCheckBox->setChecked(true);
    layout->addRow(tr("Enable ramp (bit 4):"), _enableRampCheckBox);
    groupBox->setLayout(layout);

    return groupBox;
}

QHBoxLayout *P402DtyWidget::createButtonWidgets() const
{
    QPushButton *dataLoggerPushButton = new QPushButton(tr("Data logger"));
    connect(dataLoggerPushButton, &QPushButton::clicked, this, &P402DtyWidget::createDataLogger);

    QPushButton *mappingPdoPushButton = new QPushButton(tr("Map DTY to PDOs"));
    connect(mappingPdoPushButton, &QPushButton::clicked, this, &P402DtyWidget::mapDefaultObjects);

    QPixmap dtyModePixmap;
    QLabel *dtyModeLabel;
    dtyModeLabel = new QLabel();
    dtyModePixmap.load(":/diagram/img/diagrams/402TQDiagram.png");
    dtyModeLabel->setPixmap(dtyModePixmap);
    QPushButton *imgPushButton = new QPushButton(tr("Diagram DTY mode"));
    connect(imgPushButton, &QPushButton::clicked, dtyModeLabel, &QLabel::show);

    QHBoxLayout *layout = new QHBoxLayout();
    layout->setContentsMargins(2, 0, 2, 0);
    layout->setSpacing(5);
    layout->addWidget(dataLoggerPushButton);
    layout->addWidget(mappingPdoPushButton);
    layout->addWidget(imgPushButton);

    return layout;
}

void P402DtyWidget::odNotify(const NodeObjectId &objId, NodeOd::FlagsRequest flags)
{
    if ((flags & NodeOd::FlagsRequest::Error) != 0)
    {
        return;
    }

    if ((_nodeProfile402->node() == nullptr) || (_nodeProfile402->node()->status() != Node::STARTED))
    {
        return;
    }

    if (objId == _targetObjectId)
    {
        int value = _nodeProfile402->node()->nodeOd()->value(objId).toInt();
        if (!_targetSpinBox->hasFocus())
        {
            _targetSpinBox->blockSignals(true);
            _targetSpinBox->setValue(value);
            _targetSpinBox->blockSignals(false);
        }
        if (!_targetSlider->isSliderDown())
        {
            _targetSlider->blockSignals(true);
            _targetSlider->setValue(value);
            _targetSlider->blockSignals(false);
        }
    }
}
