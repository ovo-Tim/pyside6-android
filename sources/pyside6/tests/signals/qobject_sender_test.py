#!/usr/bin/python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test cases for QObject.sender()'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QCoreApplication, QObject, QTimer, SIGNAL
from helper.usesqapplication import UsesQApplication


class ExtQTimer(QTimer):
    def __init__(self):
        super().__init__()


class Receiver(QObject):
    def __init__(self):
        super().__init__()
        self.the_sender = None

    def callback(self):
        self.the_sender = self.sender()
        if QCoreApplication.instance():
            QCoreApplication.instance().exit()


class ObjectSenderTest(unittest.TestCase):
    '''Test case for QObject.sender() method.'''

    def testSenderPythonSignal(self):
        sender = QObject()
        recv = Receiver()
        QObject.connect(sender, SIGNAL('foo()'), recv.callback)
        sender.emit(SIGNAL('foo()'))
        self.assertEqual(sender, recv.the_sender)


class ObjectSenderCheckOnReceiverTest(unittest.TestCase):
    '''Test case for QObject.sender() method, this one tests the equality on the Receiver object.'''

    def testSenderPythonSignal(self):
        sender = QObject()
        recv = Receiver()
        QObject.connect(sender, SIGNAL('foo()'), recv.callback)
        sender.emit(SIGNAL('foo()'))
        self.assertEqual(sender, recv.the_sender)


class ObjectSenderWithQAppTest(UsesQApplication):
    '''Test case for QObject.sender() method with QApplication.'''

    def testSenderCppSignal(self):
        sender = QTimer()
        sender.setObjectName('foo')
        recv = Receiver()
        sender.timeout.connect(recv.callback)
        sender.start(10)
        self.app.exec()
        self.assertEqual(sender, recv.the_sender)

    def testSenderCppSignalSingleShotTimer(self):
        recv = Receiver()
        QTimer.singleShot(10, recv.callback)
        self.app.exec()
        self.assertTrue(isinstance(recv.the_sender, QObject))

    def testSenderCppSignalWithPythonExtendedClass(self):
        sender = ExtQTimer()
        recv = Receiver()
        sender.timeout.connect(recv.callback)
        sender.start(10)
        self.app.exec()
        self.assertEqual(sender, recv.the_sender)


class ObjectSenderWithQAppCheckOnReceiverTest(UsesQApplication):
    '''Test case for QObject.sender() method with QApplication.'''

    def testSenderCppSignal(self):
        sender = QTimer()
        sender.setObjectName('foo')
        recv = Receiver()
        sender.timeout.connect(recv.callback)
        sender.start(10)
        self.app.exec()
        self.assertEqual(sender, recv.the_sender)

    def testSenderCppSignalWithPythonExtendedClass(self):
        sender = ExtQTimer()
        recv = Receiver()
        sender.timeout.connect(recv.callback)
        sender.start(10)
        self.app.exec()
        self.assertEqual(sender, recv.the_sender)


if __name__ == '__main__':
    unittest.main()

