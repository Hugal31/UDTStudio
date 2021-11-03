/**
 ** This file is part of the UDTStudio project.
 ** Copyright 2021 UniSwarm
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

#ifndef CANBUSSOCKETCAN_H
#define CANBUSSOCKETCAN_H

#include "canopen_global.h"

#include "canbusdriver.h"

#include <QMutex>
#include <QSocketNotifier>

class CANOPEN_EXPORT CanBusSocketCAN : public CanBusDriver
{
    Q_OBJECT
public:
    CanBusSocketCAN(const QString &adress);
    ~CanBusSocketCAN();

    // CanBusDriver interface
public:
    bool connectDevice() override;
    void disconnectDevice() override;

    QCanBusFrame readFrame() override;
    bool writeFrame(const QCanBusFrame &qtframe) override;

private:
    int _can_socket;
    QMutex _socketMutex;
    QSocketNotifier *_readNotifier;
    QSocketNotifier *_errorNotifier;

protected slots:
    void handleError();
};

#endif  // CANBUSSOCKETCAN_H
