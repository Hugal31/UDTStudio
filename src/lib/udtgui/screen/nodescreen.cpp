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

#include "nodescreen.h"

#include <QIcon>

NodeScreen::NodeScreen(QWidget *parent)
    : QWidget(parent)
{
    _node = nullptr;
}

void NodeScreen::setNode(Node *node, uint8_t axis)
{
    _node = node;
    setNodeInternal(node, axis);
}

NodeScreensWidget *NodeScreen::screenWidget() const
{
    return _screenWidget;
}

void NodeScreen::setScreenWidget(NodeScreensWidget *screenWidget)
{
    _screenWidget = screenWidget;
}

Node *NodeScreen::node() const
{
    return _node;
}

QIcon NodeScreen::icon() const
{
    return QIcon();
}
