#!/usr/bin/python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test cases for QObject methods'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QObject, Signal, Slot, Qt


class Obj(QObject):
    signal = Signal()

    def empty(self):
        pass

    def emitSignal(self):
        self.signal.emit()


class Receiver(QObject):

    def __init__(self, parent=None):
        super().__init__(parent)
        self._count = 0

    def count(self):
        return self._count

    @Slot()
    def testSlot(self):
        self._count += 1


class ObjectNameCase(unittest.TestCase):
    '''Tests related to QObject object name'''

    def testSimple(self):
        # QObject.objectName(string)
        name = 'object1'
        obj = QObject()
        obj.setObjectName(name)

        self.assertEqual(name, obj.objectName())

    def testEmpty(self):
        # QObject.objectName('')
        name = ''
        obj = QObject()
        obj.setObjectName(name)

        self.assertEqual(name, obj.objectName())

    def testDefault(self):
        # QObject.objectName() default
        obj = QObject()
        self.assertEqual('', obj.objectName())

    def testUnicode(self):
        name = 'não'
        # FIXME Strange error on upstream when using equal(name, obj)
        obj = QObject()
        obj.setObjectName(name)
        self.assertEqual(obj.objectName(), name)

    def testUniqueConnection(self):
        obj = Obj()
        # On first connect, UniqueConnection returns True, and on the second
        # it must return False, and not a RuntimeError (PYSIDE-34)
        self.assertTrue(obj.signal.connect(obj.empty, Qt.UniqueConnection))
        self.assertFalse(obj.signal.connect(obj.empty, Qt.UniqueConnection))

    def testDisconnect(self):
        obj = Obj()
        receiver = Receiver()
        conn_id = obj.signal.connect(receiver.testSlot)
        self.assertTrue(conn_id)

        obj.emitSignal()
        self.assertEqual(receiver.count(), 1)

        obj.signal.disconnect(conn_id)
        obj.emitSignal()
        self.assertEqual(receiver.count(), 1)


if __name__ == '__main__':
    unittest.main()
