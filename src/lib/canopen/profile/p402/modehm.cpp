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

#include "modehm.h"

#include "indexdb402.h"
#include "node.h"
#include "nodeprofile402.h"

ModeHm::ModeHm(NodeProfile402 *nodeProfile402)
    : Mode(nodeProfile402)
{
    _homeOffsetObjectId = IndexDb402::getObjectId(IndexDb402::OD_HM_HOME_OFFSET, _nodeProfile402->axisId());
    _homeOffsetObjectId.setBusIdNodeId(_nodeProfile402->node()->busId(), _nodeProfile402->node()->nodeId());
}

const NodeObjectId &ModeHm::homeOffsetObjectId() const
{
    return _homeOffsetObjectId;
}

void ModeHm::readAllObjects()
{
    _nodeProfile402->node()->readObject(_homeOffsetObjectId);
}
