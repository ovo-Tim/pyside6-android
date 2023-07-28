# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test cases for QObject methods'''

import gc
import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QObject, QTimer
from PySide6.QtWidgets import QApplication, QLabel, QVBoxLayout

is_alive = None


class InheritsCase(unittest.TestCase):
    '''Test case for QObject.inherits'''

    def testCppInheritance(self):
        # QObject.inherits() for c++ classes
        # A class inherits itself
        self.assertTrue(QObject().inherits('QObject'))

    def testPythonInheritance(self):
        # QObject.inherits() for python classes

        class Dummy(QObject):
            # Dummy class
            pass

        self.assertTrue(Dummy().inherits('QObject'))
        self.assertTrue(Dummy().inherits('Dummy'))
        self.assertTrue(not Dummy().inherits('FooBar'))

    def testPythonMultiInheritance(self):
        # QObject.inherits() for multiple inheritance
        # QObject.inherits(classname) should fail if classname isn't a
        # QObject subclass

        class Parent(object):
            # Dummy parent
            pass

        class Dummy(QObject, Parent):
            # Dummy class
            pass

        self.assertTrue(Dummy().inherits('QObject'))
        self.assertTrue(not Dummy().inherits('Parent'))

    def testSetAttributeBeforeCallingInitOnQObjectDerived(self):
        '''Test for bug #428.'''
        class DerivedObject(QObject):
            def __init__(self):
                self.member = 'member'
                super().__init__()
        obj0 = DerivedObject()
        # The second instantiation of DerivedObject will generate an exception
        # that will not come to surface immediately.
        obj1 = DerivedObject()
        # The mere calling of the object method causes
        # the exception to "reach the surface".
        obj1.objectName()

    def testMultipleInheritance(self):
        def declareClass():
            class Foo(object, QObject):
                pass

        self.assertRaises(TypeError, declareClass)

    # PYSIDE-11:
    #   The takeOwnership() method was relying that the SbkObject
    #   had a converter, which it's not the case when multiple
    #   inheritance is used.
    #   The deleteLater() method uses the takeOwnership() to give
    #   control of the object to C++, so it can be remove once
    #   the destructor is called.
    #   The solution was to add a default case when the object
    #   is null under the pythonTypeIsValueType() method in shiboken.
    def testDeleteMultipleInheritance(self):
        app = QApplication(sys.argv)

        class DerivedLabel(QLabel, QObject):
            def __del__(self):
                global is_alive
                is_alive = False

        global is_alive
        child = DerivedLabel('Hello')
        is_alive = True
        parent = QVBoxLayout()
        parent.addWidget(child)
        parent.removeWidget(child)
        child.deleteLater()
        self.assertTrue(is_alive)
        del child
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()
        self.assertTrue(is_alive)
        QTimer.singleShot(100, app.quit)
        app.exec()
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()
        self.assertFalse(is_alive)


if __name__ == '__main__':
    unittest.main()
