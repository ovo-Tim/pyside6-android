# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test cases for QObject.eventFilter'''

import gc
import os
import sys
import unittest
import weakref

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import QObject, QTimerEvent

from helper.usesqapplication import UsesQApplication


class FilterObject(QObject):
    '''Filter object for the basic test'''
    def __init__(self, obj=None, event_type=None, *args):
        # Creates a new filter object
        QObject.__init__(self, *args)
        self.obj = obj
        self.event_type = event_type
        self.events_handled = 0
        self.events_bypassed = 0

    def setTargetObject(self, obj):
        # Sets the object that will be filtered
        self.obj = obj

    def eventFilter(self, obj, event):
        '''Just checks if is the correct object and event type
        incrementing counter until reaching the limit. After that it
        stops filtering the events for the object.'''
        if (self.obj == obj):
            if isinstance(event, self.event_type) and self.events_handled < 5:
                self.events_handled += 1
                return True
            else:
                self.events_bypassed += 1
                return False
        else:
            return QObject.eventFilter(self, obj, event)


class FilteredObject(QObject):
    '''Class that will be filtered. Quits the app after 5 timer events'''
    def __init__(self, app, *args):
        QObject.__init__(self, *args)
        self.app = app
        self.times_called = 0

    def timerEvent(self, evt):
        # Overridden method
        self.times_called += 1

        if self.times_called == 5:
            self.app.quit()


class TestQObjectEventFilterPython(UsesQApplication):
    '''QObject.eventFilter - Reimplemented in python
    Filters 5 TimerEvents and then bypasses the other events to the
    timerEvent method. After 5 runs, the timerEvent method will ask
    the core application to exit'''
    def setUp(self):
        # Acquire resources
        UsesQApplication.setUp(self)
        self.obj_filter = FilterObject(event_type=QTimerEvent)

    def tearDown(self):
        # Release resources
        del self.obj_filter
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()
        UsesQApplication.tearDown(self)

    def testEventFilter(self):
        # QObject.eventFilter reimplemented in python
        filtered = FilteredObject(self.app)
        filtered.installEventFilter(self.obj_filter)
        self.obj_filter.setTargetObject(filtered)

        filtered.startTimer(0)

        self.app.exec()

        self.assertEqual(filtered.times_called, 5)
        self.assertEqual(self.obj_filter.events_handled, 5)

    @unittest.skipUnless(hasattr(sys, "getrefcount"), f"{sys.implementation.name} has no refcount")
    def testInstallEventFilterRefCountAfterDelete(self):
        '''Bug 910 - installEventFilter() increments reference count on target object
        http://bugs.pyside.org/show_bug.cgi?id=910'''
        obj = QObject()
        filt = QObject()

        self.assertEqual(sys.getrefcount(obj), 2)
        self.assertEqual(sys.getrefcount(filt), 2)
        obj.installEventFilter(filt)
        self.assertEqual(sys.getrefcount(obj), 2)
        self.assertEqual(sys.getrefcount(filt), 2)

        wref = weakref.ref(obj)
        del obj
        self.assertEqual(wref(), None)

    @unittest.skipUnless(hasattr(sys, "getrefcount"), f"{sys.implementation.name} has no refcount")
    def testInstallEventFilterRefCountAfterRemove(self):
        # Bug 910
        obj = QObject()
        filt = QObject()

        self.assertEqual(sys.getrefcount(obj), 2)
        self.assertEqual(sys.getrefcount(filt), 2)
        obj.installEventFilter(filt)
        self.assertEqual(sys.getrefcount(obj), 2)
        self.assertEqual(sys.getrefcount(filt), 2)
        obj.removeEventFilter(filt)
        self.assertEqual(sys.getrefcount(obj), 2)
        self.assertEqual(sys.getrefcount(filt), 2)

        wref = weakref.ref(obj)
        del obj
        self.assertEqual(wref(), None)


if __name__ == '__main__':
    unittest.main()
