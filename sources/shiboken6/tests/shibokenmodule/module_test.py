# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

from shiboken6 import Shiboken
from sample import *

class MultipleInherited (ObjectType, Point):
    def __init__(self):
        ObjectType.__init__(self)
        Point.__init__(self)

class TestShiboken(unittest.TestCase):
    def testIsValid(self):
        self.assertTrue(Shiboken.isValid(object()))
        self.assertTrue(Shiboken.isValid(None))

        bb = BlackBox()
        item = ObjectType()
        ticket = bb.keepObjectType(item)
        bb.disposeObjectType(ticket)
        self.assertFalse(Shiboken.isValid(item))

    def testWrapInstance(self):
        addr = ObjectType.createObjectType()
        obj = Shiboken.wrapInstance(addr, ObjectType)
        self.assertFalse(Shiboken.createdByPython(obj))
        obj.setObjectName("obj")
        self.assertEqual(obj.objectName(), "obj")
        self.assertEqual(addr, obj.identifier())
        self.assertFalse(Shiboken.createdByPython(obj))

        # avoid mem leak =]
        bb = BlackBox()
        self.assertTrue(Shiboken.createdByPython(bb))
        bb.disposeObjectType(bb.keepObjectType(obj))

    def testIsOwnedByPython(self):
        obj = ObjectType()
        self.assertTrue(Shiboken.ownedByPython(obj))
        p = ObjectType()
        obj.setParent(p)
        self.assertFalse(Shiboken.ownedByPython(obj))

    def testDump(self):
        """Just check if dump doesn't crash on certain use cases"""
        p = ObjectType()
        obj = ObjectType(p)
        obj2 = ObjectType(obj)
        obj3 = ObjectType(obj)
        self.assertEqual(Shiboken.dump(None), "Ordinary Python type.")
        Shiboken.dump(obj)

        model = ObjectModel(p)
        v = ObjectView(model, p)
        Shiboken.dump(v)

        m = MultipleInherited()
        Shiboken.dump(m)
        self.assertEqual(len(Shiboken.getCppPointer(m)), 2)

        # Don't crash even after deleting an object
        Shiboken.invalidate(obj)
        Shiboken.dump(obj)  # deleted
        Shiboken.dump(p)    # child deleted
        Shiboken.dump(obj2) # parent deleted

    def testDelete(self):
        obj = ObjectType()
        child = ObjectType(obj)
        self.assertTrue(Shiboken.isValid(obj))
        self.assertTrue(Shiboken.isValid(child))
        # Note: this test doesn't assure that the object dtor was really called
        Shiboken.delete(obj)
        self.assertFalse(Shiboken.isValid(obj))
        self.assertFalse(Shiboken.isValid(child))

    def testVersionAttr(self):
        self.assertEqual(type(Shiboken.__version__), str)
        self.assertTrue(len(Shiboken.__version__) >= 5)
        self.assertEqual(type(Shiboken.__version_info__), tuple)
        self.assertEqual(len(Shiboken.__version_info__), 5)

    def testAllWrappers(self):
        obj = ObjectType()
        self.assertTrue(obj in Shiboken.getAllValidWrappers())
        Shiboken.delete(obj)
        self.assertFalse(obj in Shiboken.getAllValidWrappers())


if __name__ == '__main__':
    unittest.main()
