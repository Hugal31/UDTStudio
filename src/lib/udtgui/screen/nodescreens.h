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

#ifndef NODESCREENS_H
#define NODESCREENS_H

#include "udtgui_global.h"

#include <QWidget>

#include "nodescreen.h"

#include <QTabWidget>

class UDTGUI_EXPORT NodeScreens : public QWidget
{
    Q_OBJECT
public:
    NodeScreens(QWidget *parent = nullptr);

    Node *activeNode() const;

public slots:
    void setActiveNode(Node *node);

protected:
    Node *_activeNode;
    void addNode(Node *node);

    void createWidgets();
    QTabWidget *_tabWidget;

    struct NodeScreensStruct
    {
        Node *node;
        QList<NodeScreen *> screens;
    };
    QMap<Node *, NodeScreensStruct> _nodesMap;

    // to remove
    QList<NodeScreen *> _screens;
    void addScreen(NodeScreen *screen);
    bool screenExist(NodeScreen *screen);
};

#endif // NODESCREENS_H
