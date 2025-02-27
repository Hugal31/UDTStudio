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

#include "dataloggerchartswidget.h"

#include <QDateTimeAxis>
#include <QDebug>
#include <QGraphicsLayout>
#include <QMimeData>
#include <QOpenGLWidget>
#include <QScatterSeries>
#include <QToolTip>
#include <QValueAxis>
#include <qmath.h>

using namespace QtCharts;

DataLoggerChartsWidget::DataLoggerChartsWidget(DataLogger *dataLogger, QWidget *parent)
    : QChartView(parent)
{
    _dataLogger = nullptr;
    _rollingEnabled = false;
    _rollingTimeMs = 1000;

    setStyleSheet("QAbstractScrollArea {padding: 0px;}");

    _chart = new QtCharts::QChart();
    _chart->legend()->hide();
    //_chart->setTitle("Logger");
    _chart->legend()->setVisible(true);
    _chart->legend()->setAlignment(Qt::AlignBottom);
    _chart->setTheme(QtCharts::QChart::ChartThemeBlueCerulean);
    setRenderHint(QPainter::Antialiasing);
    _chart->layout()->setContentsMargins(0, 0, 0, 0);
    _chart->setBackgroundBrush(QColor(0x19232D));
    setChart(_chart);

    _axisX = new QtCharts::QDateTimeAxis();
    _axisX->setTickCount(11);
    _axisX->setFormat("hh:mm:ss");
    //_axisX->setTitleText("Time");

    _axisY = new QtCharts::QValueAxis();
    _axisY->setLabelFormat("%g");
    //_axisY->setTitleText("Value");

    setDataLogger(dataLogger);
    _idPending = -1;

    _useOpenGL = false;
    _viewCross = false;

    connect(&_updateTimer, &QTimer::timeout, this, &DataLoggerChartsWidget::updateSeries);
    _updateTimer.start(100);
}

DataLoggerChartsWidget::~DataLoggerChartsWidget()
{
    delete _chart;
}

DataLogger *DataLoggerChartsWidget::dataLogger() const
{
    return _dataLogger;
}

void DataLoggerChartsWidget::setDataLogger(DataLogger *dataLogger)
{
    if (dataLogger != _dataLogger)
    {
        if (dataLogger != nullptr)
        {
            connect(dataLogger, &DataLogger::dataAboutToBeAdded, this, &DataLoggerChartsWidget::addDataPrepare);
            connect(dataLogger, &DataLogger::dataAdded, this, &DataLoggerChartsWidget::addDataOk);
            connect(dataLogger, &DataLogger::dataAboutToBeRemoved, this, &DataLoggerChartsWidget::removeDataPrepare);
            connect(dataLogger, &DataLogger::dataRemoved, this, &DataLoggerChartsWidget::removeDataOk);
            // connect(dataLogger, &DataLogger::dataChanged, this, &DataLoggerChartsWidget::updateDlData);
        }
    }
    _dataLogger = dataLogger;
}

QtCharts::QChart *DataLoggerChartsWidget::chart() const
{
    return _chart;
}

QList<QtCharts::QXYSeries *> DataLoggerChartsWidget::series() const
{
    return _series;
}

bool DataLoggerChartsWidget::useOpenGL() const
{
    return _useOpenGL;
}

void DataLoggerChartsWidget::setUseOpenGL(bool useOpenGL)
{
    _useOpenGL = useOpenGL;
    for (QXYSeries *serie : qAsConst(_series))
    {
        serie->setUseOpenGL(useOpenGL);
    }
    QList<QOpenGLWidget *> glWidgets = findChildren<QOpenGLWidget *>();
    for (QOpenGLWidget *glWidget : qAsConst(glWidgets))
    {
        glWidget->update();
    }
    invalidateScene();
    update();

    emit useOpenGLChanged(useOpenGL);
}

bool DataLoggerChartsWidget::viewCross() const
{
    return _viewCross;
}

void DataLoggerChartsWidget::setViewCross(bool viewCross)
{
    _viewCross = viewCross;
    for (QXYSeries *serie : qAsConst(_series))
    {
        serie->setPointsVisible(viewCross);
    }
    emit viewCrossChanged(viewCross);
}

bool DataLoggerChartsWidget::isRollingEnabled() const
{
    return _rollingEnabled;
}

void DataLoggerChartsWidget::setRollingEnabled(bool rollingEnabled)
{
    _rollingEnabled = rollingEnabled;
    updateYaxis();
    emit rollingChanged(rollingEnabled);
}

int DataLoggerChartsWidget::rollingTimeMs() const
{
    return _rollingTimeMs;
}

void DataLoggerChartsWidget::setRollingTimeMs(int rollingTimeMs)
{
    _rollingTimeMs = rollingTimeMs;
    updateYaxis();
    emit rollingTimeMsChanged(rollingTimeMs);
}

void DataLoggerChartsWidget::updateDlData(int id)
{
    if (id >= _series.count())
    {
        return;
    }

    DLData *dlData = _dataLogger->data(id);
    QtCharts::QXYSeries *serie = _series[id];
    if (dlData->values().count() < serie->count())
    {
        serie->clear();
        return;
    }

    // serie->append(dlData->lastDateTime().toMSecsSinceEpoch(), dlData->lastValue());

    if (serie->color() != dlData->color())
    {
        serie->setPen(QPen(dlData->color(), 2));
    }

    updateYaxis();

    qreal min = _dataLogger->min();
    qreal max = _dataLogger->max();
    if (min == max)
    {
        min -= 0.5;
        max += 0.5;
    }
    if (min < _axisY->min() || min > _axisY->min() || max > _axisY->max() || max < _axisY->max())
    {
        _axisY->setRange(min, max);
        _axisY->applyNiceNumbers();
        if (_axisY->tickCount() < 4)
        {
            _axisY->setMinorTickCount(1);
        }
        else
        {
            _axisY->setMinorTickCount(0);
        }
    }
}

void DataLoggerChartsWidget::addDataPrepare(int id)
{
    _idPending = id;
}

void DataLoggerChartsWidget::addDataOk()
{
    if (_idPending >= 0 && _idPending < _dataLogger->dataList().count())
    {
        DLData *dlData = _dataLogger->data(_idPending);
        QtCharts::QLineSeries *serie = new QtCharts::QLineSeries();
        serie->setName(dlData->name());
        serie->setPen(QPen(dlData->color(), 2));
        serie->setBrush(QBrush(dlData->color()));
        serie->setPointsVisible(_viewCross);
        serie->setUseOpenGL(_useOpenGL);
        _chart->addSeries(serie);

        if (!_chart->axes(Qt::Horizontal).contains(_axisX))
        {
            _chart->addAxis(_axisX, Qt::AlignBottom);
            _chart->addAxis(_axisY, Qt::AlignLeft);
        }

        serie->attachAxis(_axisX);
        serie->attachAxis(_axisY);
        _series.append(serie);
        _serieLastDates.append(0);

        connect(serie, &QLineSeries::hovered, this, &DataLoggerChartsWidget::tooltip);
    }
    _idPending = -1;
}

void DataLoggerChartsWidget::removeDataPrepare(int id)
{
    if (id >= 0 && id < _dataLogger->dataList().count())
    {
        QtCharts::QXYSeries *serie = _series.at(id);
        _chart->removeSeries(serie);
        _series.removeAt(id);
        _serieLastDates.removeAt(id);
        serie->deleteLater();
    }
}

void DataLoggerChartsWidget::removeDataOk()
{
    _idPending = -1;
}

void DataLoggerChartsWidget::updateYaxis()
{
    QDateTime firstDateTime = _dataLogger->firstDateTime();
    QDateTime lastDateTime = _dataLogger->lastDateTime();

    if (_rollingEnabled)  // rolling mode
    {
        firstDateTime = _dataLogger->lastDateTime().addMSecs(-_rollingTimeMs);
        _axisX->setRange(firstDateTime, lastDateTime);
    }
    else
    {
        if (firstDateTime != _axisX->min() || lastDateTime > _axisX->max())
        {
            qint64 msDiff = firstDateTime.msecsTo(lastDateTime);
            _axisX->setRange(firstDateTime, lastDateTime.addMSecs(msDiff / 5));
        }
    }
}

void DataLoggerChartsWidget::tooltip(QPointF point, bool state)
{
    Q_UNUSED(state)
    QLineSeries *serie = dynamic_cast<QLineSeries *>(sender());

    QToolTip::showText(QCursor::pos(), QString("%1\n%2").arg(serie->name()).arg(point.y()), this, QRect());
}

void DataLoggerChartsWidget::updateSeries()
{
    if (!_dataLogger->isStarted())
    {
        return;
    }

    setUpdatesEnabled(false);

    for (int idSerie = 0; idSerie < _series.count(); idSerie++)
    {
        DLData *dlData = _dataLogger->data(idSerie);
        QXYSeries *serie = _series[idSerie];

        qint64 lastDateSerie = _serieLastDates[idSerie];
        qint64 lastDateDlData = dlData->lastDateTime().toMSecsSinceEpoch();

        if (lastDateSerie < lastDateDlData)
        {
            QList<QPointF> points;

            QList<qreal>::const_iterator valuesIt = dlData->values().cend();
            QList<QDateTime>::const_iterator timesIt = dlData->times().cend();
            valuesIt--;
            timesIt--;
            while (lastDateSerie < timesIt->toMSecsSinceEpoch())
            {
                points.prepend(QPointF(timesIt->toMSecsSinceEpoch(), *valuesIt));
                if (timesIt == dlData->times().cbegin())
                {
                    break;
                }
                valuesIt--;
                timesIt--;
            }
            serie->append(points);

            _serieLastDates[idSerie] = lastDateDlData;
            updateDlData(idSerie);
        }
    }

    setUpdatesEnabled(true);
}

void DataLoggerChartsWidget::dropEvent(QDropEvent *event)
{
    QChartView::dropEvent(event);
    if (event->mimeData()->hasFormat("index/subindex"))
    {
        const QStringList &stringListObjId = QString(event->mimeData()->data("index/subindex")).split(':', QString::SkipEmptyParts);
        for (const QString &stringObjId : stringListObjId)
        {
            NodeObjectId objId = NodeObjectId::fromMimeData(stringObjId);
            _dataLogger->addData(objId);
        }
        event->accept();
    }
}

void DataLoggerChartsWidget::dragEnterEvent(QDragEnterEvent *event)
{
    QChartView::dragEnterEvent(event);
    if (event->mimeData()->hasFormat("index/subindex"))
    {
        event->accept();
        event->acceptProposedAction();
    }
}

void DataLoggerChartsWidget::dragMoveEvent(QDragMoveEvent *event)
{
    QChartView::dragMoveEvent(event);
    event->accept();
    event->acceptProposedAction();
}
