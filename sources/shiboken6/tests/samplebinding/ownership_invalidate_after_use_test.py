#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Ownership tests for cases of invalidation of Python wrapper after use.'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

from sample import ObjectType, ObjectTypeDerived, Event


class ExtObjectType(ObjectType):
    def __init__(self):
        ObjectType.__init__(self)
        self.type_of_last_event = None
        self.last_event = None
    def event(self, event):
        self.last_event = event
        self.type_of_last_event = event.eventType()
        return True

class MyObjectType (ObjectType):
    def __init__(self):
        super(MyObjectType, self).__init__()
        self.fail = False

    def event(self, ev):
        self.callInvalidateEvent(ev)
        try:
            ev.eventType()
        except:
            self.fail = True
            raise
        return True

    def invalidateEvent(self, ev):
        pass

class ExtObjectTypeDerived(ObjectTypeDerived):
    def __init__(self):
        ObjectTypeDerived.__init__(self)
        self.type_of_last_event = None
        self.last_event = None
    def event(self, event):
        self.last_event = event
        self.type_of_last_event = event.eventType()
        return True

class OwnershipInvalidateAfterUseTest(unittest.TestCase):
    '''Ownership tests for cases of invalidation of Python wrapper after use.'''

    def testInvalidateAfterUse(self):
        '''In ObjectType.event(Event*) the wrapper object created for Event must me marked as invalid after the method is called.'''
        eot = ExtObjectType()
        eot.causeEvent(Event.SOME_EVENT)
        self.assertEqual(eot.type_of_last_event, Event.SOME_EVENT)
        self.assertRaises(RuntimeError, eot.last_event.eventType)

    def testObjectInvalidatedAfterUseAsParameter(self):
        '''Tries to use wrapper invalidated after use as a parameter to another method.'''
        eot = ExtObjectType()
        ot = ObjectType()
        eot.causeEvent(Event.ANY_EVENT)
        self.assertEqual(eot.type_of_last_event, Event.ANY_EVENT)
        self.assertRaises(RuntimeError, ot.event, eot.last_event)

    def testit(self):
        obj = MyObjectType()
        obj.causeEvent(Event.BASIC_EVENT)
        self.assertFalse(obj.fail)

    def testInvalidateAfterUseInDerived(self):
        '''Invalidate was failing in a derived C++ class that also inherited
        other base classes'''
        eot = ExtObjectTypeDerived()
        eot.causeEvent(Event.SOME_EVENT)
        self.assertEqual(eot.type_of_last_event, Event.SOME_EVENT)
        self.assertRaises(RuntimeError, eot.last_event.eventType)

if __name__ == '__main__':
    unittest.main()

