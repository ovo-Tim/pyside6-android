# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from helper.helper import quickview_errorstring

from PySide6.QtCore import Property, QObject, QTimer, QUrl
from PySide6.QtGui import QGuiApplication, QPen, QColor, QPainter
from PySide6.QtQml import (qjsEngine, qmlContext, qmlEngine, qmlRegisterType,
                           ListProperty, QmlElement, QmlNamedElement)
from PySide6.QtQuick import QQuickView, QQuickItem, QQuickPaintedItem


QML_IMPORT_NAME = "Charts"
QML_IMPORT_MAJOR_VERSION = 1


@QmlElement
class PieSlice (QQuickPaintedItem):
    def __init__(self, parent=None):
        QQuickPaintedItem.__init__(self, parent)
        self._color = QColor()
        self._fromAngle = 0
        self._angleSpan = 0

    def getColor(self):
        return self._color

    def setColor(self, value):
        self._color = value

    def getFromAngle(self):
        return self._angle

    def setFromAngle(self, value):
        self._fromAngle = value

    def getAngleSpan(self):
        return self._angleSpan

    def setAngleSpan(self, value):
        self._angleSpan = value

    color = Property(QColor, getColor, setColor)
    fromAngle = Property(int, getFromAngle, setFromAngle)
    angleSpan = Property(int, getAngleSpan, setAngleSpan)

    def paint(self, painter):
        global paintCalled
        pen = QPen(self._color, 2)
        painter.setPen(pen)
        painter.setRenderHints(QPainter.Antialiasing, True)
        painter.drawPie(self.boundingRect(), self._fromAngle * 16, self._angleSpan * 16)
        paintCalled = True


@QmlNamedElement("PieChart")
class PieChartOriginalName(QQuickItem):
    def __init__(self, parent=None):
        QQuickItem.__init__(self, parent)
        self._name = ''
        self._slices = []

    def getName(self):
        return self._name

    def setName(self, value):
        self._name = value

    name = Property(str, getName, setName)

    def appendSlice(self, _slice):
        global appendCalled
        _slice.setParentItem(self)
        self._slices.append(_slice)
        appendCalled = True

    slices = ListProperty(PieSlice, append=appendSlice)


appendCalled = False
paintCalled = False


class TestQmlSupport(unittest.TestCase):

    def testIt(self):
        app = QGuiApplication([])

        view = QQuickView()
        file = Path(__file__).resolve().parent / 'registertype.qml'
        self.assertTrue(file.is_file())
        view.setSource(QUrl.fromLocalFile(file))
        root_object = view.rootObject()
        self.assertTrue(root_object, quickview_errorstring(view))
        self.assertTrue(qjsEngine(root_object))
        self.assertEqual(qmlEngine(root_object), view.engine())
        self.assertTrue(qmlContext(root_object))

        view.show()
        QTimer.singleShot(250, view.close)
        app.exec()
        self.assertTrue(appendCalled)
        self.assertTrue(paintCalled)


if __name__ == '__main__':
    unittest.main()
