# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import (QCoreApplication, QUrl, QObject,
                            Property)
from PySide6.QtQml import (QQmlComponent, QQmlEngine, QmlExtended,
                           QmlElement)


"""Test for the QmlExtended decorator. Extends a class TestWidget
   by a property leftMargin through a TestExtension and verifies the setting."""


QML_IMPORT_NAME = "TestExtension"
QML_IMPORT_MAJOR_VERSION = 1


def component_error(component):
    result = ""
    for e in component.errors():
        if result:
            result += "\n"
        result += str(e)
    return result


class TestExtension(QObject):

    def __init__(self, parent=None):
        super().__init__(parent)
        self._leftMargin = 0

    @Property(int)
    def leftMargin(self):
        return self._leftMargin

    @leftMargin.setter
    def leftMargin(self, m):
        self._leftMargin = m


@QmlElement
@QmlExtended(TestExtension)
class TestWidget(QObject):

    def __init__(self, parent=None):
        super().__init__(parent)


class TestQmlExtended(unittest.TestCase):
    def testIt(self):
        app = QCoreApplication(sys.argv)
        file = Path(__file__).resolve().parent / 'registerextended.qml'
        url = QUrl.fromLocalFile(file)
        engine = QQmlEngine()
        component = QQmlComponent(engine, url)
        widget = component.create()
        self.assertTrue(widget, component_error(component))
        extension = widget.findChild(TestExtension)
        self.assertTrue(extension)
        self.assertEqual(extension.leftMargin, 10)


if __name__ == '__main__':
    unittest.main()
