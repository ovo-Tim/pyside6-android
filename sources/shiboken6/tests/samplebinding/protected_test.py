#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test cases for protected methods.'''

import gc
import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

from sample import cacheSize
from sample import ProtectedNonPolymorphic, ProtectedVirtualDestructor
from sample import ProtectedPolymorphic, ProtectedPolymorphicDaughter, ProtectedPolymorphicGrandDaughter
from sample import createProtectedProperty, ProtectedProperty, ProtectedEnumClass
from sample import PrivateDtor
from sample import Event, ObjectType, Point

class ExtendedProtectedPolymorphic(ProtectedPolymorphic):
    def __init__(self, name):
        ProtectedPolymorphic.__init__(self, name)
        self.protectedName_called = False
    def protectedName(self):
        self.protectedName_called = True
        self._name = 'Extended' + ProtectedPolymorphic.protectedName(self)
        return self._name

class ExtendedProtectedPolymorphicDaughter(ProtectedPolymorphicDaughter):
    def __init__(self, name):
        self.protectedName_called = False
        ProtectedPolymorphicDaughter.__init__(self, name)
    def protectedName(self):
        self.protectedName_called = True
        self._name = 'ExtendedDaughter' + ProtectedPolymorphicDaughter.protectedName(self)
        return self._name

class ExtendedProtectedPolymorphicGrandDaughter(ProtectedPolymorphicGrandDaughter):
    def __init__(self, name):
        self.protectedName_called = False
        ProtectedPolymorphicGrandDaughter.__init__(self, name)
    def protectedName(self):
        self.protectedName_called = True
        self._name = 'ExtendedGrandDaughter' + ProtectedPolymorphicGrandDaughter.protectedName(self)
        return self._name

class ExtendedProtectedVirtualDestructor(ProtectedVirtualDestructor):
    def __init__(self):
        ProtectedVirtualDestructor.__init__(self)

class ProtectedNonPolymorphicTest(unittest.TestCase):
    '''Test cases for protected method in a class without virtual methods.'''

    def tearDown(self):
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()
        self.assertEqual(cacheSize(), 0)

    def testProtectedCall(self):
        '''Calls a non-virtual protected method.'''
        p = ProtectedNonPolymorphic('NonPoly')
        self.assertEqual(p.publicName(), p.protectedName())
        a0, a1 = 1, 2
        self.assertEqual(p.protectedSum(a0, a1), a0 + a1)

    def testProtectedCallWithInstanceCreatedOnCpp(self):
        '''Calls a non-virtual protected method on an instance created in C++.'''
        p = ProtectedNonPolymorphic.create()
        self.assertEqual(p.publicName(), p.protectedName())
        a0, a1 = 1, 2
        self.assertEqual(p.protectedSum(a0, a1), a0 + a1)

    def testModifiedProtectedCall(self):
        '''Calls a non-virtual protected method modified with code injection.'''
        p = ProtectedNonPolymorphic('NonPoly')
        self.assertEqual(p.dataTypeName(), 'integer')
        self.assertEqual(p.dataTypeName(1), 'integer')
        self.assertEqual(p.dataTypeName(Point(1, 2)), 'pointer')

class ProtectedPolymorphicTest(unittest.TestCase):
    '''Test cases for protected method in a class with virtual methods.'''

    def tearDown(self):
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()
        self.assertEqual(cacheSize(), 0)

    def testProtectedCall(self):
        '''Calls a virtual protected method.'''
        p = ProtectedNonPolymorphic('Poly')
        self.assertEqual(p.publicName(), p.protectedName())
        a0, a1 = 1, 2
        self.assertEqual(p.protectedSum(a0, a1), a0 + a1)

    def testProtectedCallWithInstanceCreatedOnCpp(self):
        '''Calls a virtual protected method on an instance created in C++.'''
        p = ProtectedPolymorphic.create()
        self.assertEqual(p.publicName(), p.protectedName())
        self.assertEqual(p.callProtectedName(), p.protectedName())

    def testReimplementedProtectedCall(self):
        '''Calls a reimplemented virtual protected method.'''
        original_name = 'Poly'
        p = ExtendedProtectedPolymorphic(original_name)
        name = p.callProtectedName()
        self.assertTrue(p.protectedName_called)
        self.assertEqual(p.protectedName(), name)
        self.assertEqual(ProtectedPolymorphic.protectedName(p), original_name)
class ProtectedPolymorphicDaugherTest(unittest.TestCase):
    '''Test cases for protected method in a class inheriting for a class with virtual methods.'''

    def testProtectedCallWithInstanceCreatedOnCpp(self):
        '''Calls a virtual protected method from parent class on an instance created in C++.'''
        p = ProtectedPolymorphicDaughter.create()
        self.assertEqual(p.publicName(), p.protectedName())
        self.assertEqual(p.callProtectedName(), p.protectedName())

    def testReimplementedProtectedCall(self):
        '''Calls a reimplemented virtual protected method from parent class.'''
        original_name = 'Poly'
        p = ExtendedProtectedPolymorphicDaughter(original_name)
        name = p.callProtectedName()
        self.assertTrue(p.protectedName_called)
        self.assertEqual(p.protectedName(), name)
        self.assertEqual(ProtectedPolymorphicDaughter.protectedName(p), original_name)


class ProtectedPolymorphicGrandDaugherTest(unittest.TestCase):
    '''Test cases for protected method in a class inheriting for a class that inherits from
    another with protected virtual methods.'''

    def tearDown(self):
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()
        self.assertEqual(cacheSize(), 0)

    def testProtectedCallWithInstanceCreatedOnCpp(self):
        '''Calls a virtual protected method from parent class on an instance created in C++.'''
        p = ProtectedPolymorphicGrandDaughter.create()
        self.assertEqual(p.publicName(), p.protectedName())
        self.assertEqual(p.callProtectedName(), p.protectedName())

    def testReimplementedProtectedCall(self):
        '''Calls a reimplemented virtual protected method from parent class.'''
        original_name = 'Poly'
        p = ExtendedProtectedPolymorphicGrandDaughter(original_name)
        name = p.callProtectedName()
        self.assertTrue(p.protectedName_called)
        self.assertEqual(p.protectedName(), name)
        self.assertEqual(ProtectedPolymorphicGrandDaughter.protectedName(p), original_name)

class ProtectedVirtualDtorTest(unittest.TestCase):
    '''Test cases for protected virtual destructor.'''

    def setUp(self):
        ProtectedVirtualDestructor.resetDtorCounter()

    def tearDown(self):
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()
        self.assertEqual(cacheSize(), 0)

    def testVirtualProtectedDtor(self):
        '''Original protected virtual destructor is being called.'''
        dtor_called = ProtectedVirtualDestructor.dtorCalled()
        for i in range(1, 10):
            pvd = ProtectedVirtualDestructor()
            # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
            gc.collect()
            del pvd
            # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
            gc.collect()
            self.assertEqual(ProtectedVirtualDestructor.dtorCalled(), dtor_called + i)

    def testVirtualProtectedDtorOnCppCreatedObject(self):
        '''Original protected virtual destructor is being called for a C++ created object.'''
        dtor_called = ProtectedVirtualDestructor.dtorCalled()
        for i in range(1, 10):
            pvd = ProtectedVirtualDestructor.create()
            del pvd
            # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
            gc.collect()
            self.assertEqual(ProtectedVirtualDestructor.dtorCalled(), dtor_called + i)

    def testProtectedDtorOnDerivedClass(self):
        '''Original protected virtual destructor is being called for a derived class.'''
        dtor_called = ExtendedProtectedVirtualDestructor.dtorCalled()
        for i in range(1, 10):
            pvd = ExtendedProtectedVirtualDestructor()
            del pvd
            # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
            gc.collect()
            self.assertEqual(ExtendedProtectedVirtualDestructor.dtorCalled(), dtor_called + i)


class ExtendedProtectedEnumClass(ProtectedEnumClass):
    def __init__(self):
        ProtectedEnumClass.__init__(self)
    def protectedEnumMethod(self, value):
        if value == ProtectedEnumClass.ProtectedItem0:
            return ProtectedEnumClass.ProtectedItem1
        return ProtectedEnumClass.ProtectedItem0
    def publicEnumMethod(self, value):
        if value == ProtectedEnumClass.PublicItem0:
            return ProtectedEnumClass.PublicItem1
        return ProtectedEnumClass.PublicItem0

class ProtectedEnumTest(unittest.TestCase):
    '''Test cases for protected enum.'''

    def tearDown(self):
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()
        self.assertEqual(cacheSize(), 0)

    def testProtectedMethodWithProtectedEnumArgument(self):
        '''Calls protected method with protected enum argument.'''
        obj = ProtectedEnumClass()

        self.assertEqual(type(ProtectedEnumClass.ProtectedItem0), ProtectedEnumClass.ProtectedEnum)

        self.assertEqual(obj.protectedEnumMethod(ProtectedEnumClass.ProtectedItem0), ProtectedEnumClass.ProtectedItem0)
        self.assertEqual(obj.protectedEnumMethod(ProtectedEnumClass.ProtectedItem1), ProtectedEnumClass.ProtectedItem1)

        self.assertEqual(obj.callProtectedEnumMethod(ProtectedEnumClass.ProtectedItem0), ProtectedEnumClass.ProtectedItem0)
        self.assertEqual(obj.callProtectedEnumMethod(ProtectedEnumClass.ProtectedItem1), ProtectedEnumClass.ProtectedItem1)

    def testProtectedMethodWithPublicEnumArgument(self):
        '''Calls protected method with public enum argument.'''
        obj = ProtectedEnumClass()

        self.assertEqual(obj.publicEnumMethod(ProtectedEnumClass.PublicItem0), ProtectedEnumClass.PublicItem0)
        self.assertEqual(obj.publicEnumMethod(ProtectedEnumClass.PublicItem1), ProtectedEnumClass.PublicItem1)

        self.assertEqual(obj.callPublicEnumMethod(ProtectedEnumClass.PublicItem0), ProtectedEnumClass.PublicItem0)
        self.assertEqual(obj.callPublicEnumMethod(ProtectedEnumClass.PublicItem1), ProtectedEnumClass.PublicItem1)

    def testOverriddenProtectedMethodWithProtectedEnumArgument(self):
        '''Calls overridden protected method with protected enum argument.'''
        obj = ExtendedProtectedEnumClass()

        self.assertEqual(obj.protectedEnumMethod(ProtectedEnumClass.ProtectedItem0), ProtectedEnumClass.ProtectedItem1)
        self.assertEqual(obj.protectedEnumMethod(ProtectedEnumClass.ProtectedItem1), ProtectedEnumClass.ProtectedItem0)

        self.assertEqual(ProtectedEnumClass.protectedEnumMethod(obj, ProtectedEnumClass.ProtectedItem0), ProtectedEnumClass.ProtectedItem0)
        self.assertEqual(ProtectedEnumClass.protectedEnumMethod(obj, ProtectedEnumClass.ProtectedItem1), ProtectedEnumClass.ProtectedItem1)

        self.assertEqual(obj.callProtectedEnumMethod(ProtectedEnumClass.ProtectedItem0), ProtectedEnumClass.ProtectedItem1)
        self.assertEqual(obj.callProtectedEnumMethod(ProtectedEnumClass.ProtectedItem1), ProtectedEnumClass.ProtectedItem0)

    def testOverriddenProtectedMethodWithPublicEnumArgument(self):
        '''Calls overridden protected method with public enum argument.'''
        obj = ExtendedProtectedEnumClass()

        self.assertEqual(obj.publicEnumMethod(ProtectedEnumClass.PublicItem0), ProtectedEnumClass.PublicItem1)
        self.assertEqual(obj.publicEnumMethod(ProtectedEnumClass.PublicItem1), ProtectedEnumClass.PublicItem0)

        self.assertEqual(ProtectedEnumClass.publicEnumMethod(obj, ProtectedEnumClass.PublicItem0), ProtectedEnumClass.PublicItem0)
        self.assertEqual(ProtectedEnumClass.publicEnumMethod(obj, ProtectedEnumClass.PublicItem1), ProtectedEnumClass.PublicItem1)

        self.assertEqual(obj.callPublicEnumMethod(ProtectedEnumClass.PublicItem0), ProtectedEnumClass.PublicItem1)
        self.assertEqual(obj.callPublicEnumMethod(ProtectedEnumClass.PublicItem1), ProtectedEnumClass.PublicItem0)


class ProtectedPropertyTest(unittest.TestCase):
    '''Test cases for a class with a protected property (or field in C++).'''

    def setUp(self):
        self.obj = ProtectedProperty()

    def tearDown(self):
        del self.obj
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()
        self.assertEqual(cacheSize(), 0)

    def testProtectedProperty(self):
        '''Writes and reads a protected integer property.'''
        self.obj.protectedProperty = 3
        self.assertEqual(self.obj.protectedProperty, 3)

    def testProtectedContainerProperty(self):
        '''Writes and reads a protected list of integers property.'''
        lst = [1, 2, 3, 4]
        self.obj.protectedContainerProperty = lst
        self.assertEqual(self.obj.protectedContainerProperty, lst)

    def testProtectedEnumProperty(self):
        '''Writes and reads a protected enum property.'''
        self.obj.protectedEnumProperty = Event.SOME_EVENT
        self.assertEqual(self.obj.protectedEnumProperty, Event.SOME_EVENT)

    def testProtectedValueTypeProperty(self):
        '''Writes and reads a protected value type property.'''
        point = Point(12, 34)
        self.obj.protectedValueTypeProperty = point
        self.assertEqual(self.obj.protectedValueTypeProperty, point)
        self.assertFalse(self.obj.protectedValueTypeProperty is point)
        pointProperty = self.obj.protectedValueTypeProperty
        self.assertTrue(self.obj.protectedValueTypeProperty is pointProperty)

    def testProtectedValueTypePropertyWrapperRegistration(self):
        '''Access colocated protected value type property.'''
        cache_size = cacheSize()
        point = Point(12, 34)
        obj = createProtectedProperty()
        obj.protectedValueTypeProperty
        self.assertEqual(obj.protectedValueTypeProperty.copy(),
                         obj.protectedValueTypeProperty)
        obj.protectedValueTypeProperty = point
        self.assertEqual(obj.protectedValueTypeProperty, point)
        self.assertFalse(obj.protectedValueTypeProperty is point)
        pointProperty = obj.protectedValueTypeProperty
        self.assertTrue(obj.protectedValueTypeProperty is pointProperty)
        del obj, point, pointProperty
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()
        self.assertEqual(cacheSize(), cache_size)

    def testProtectedValueTypePointerProperty(self):
        '''Writes and reads a protected value type pointer property.'''
        pt1 = Point(12, 34)
        pt2 = Point(12, 34)
        self.obj.protectedValueTypePointerProperty = pt1
        self.assertEqual(self.obj.protectedValueTypePointerProperty, pt1)
        self.assertEqual(self.obj.protectedValueTypePointerProperty, pt2)
        self.assertTrue(self.obj.protectedValueTypePointerProperty is pt1)
        self.assertFalse(self.obj.protectedValueTypePointerProperty is pt2)
        # PYSIDE-535: Need to assign None to break the cycle
        self.obj.protectedValueTypePointerProperty = None

    def testProtectedObjectTypeProperty(self):
        '''Writes and reads a protected object type property.'''
        obj = ObjectType()
        self.obj.protectedObjectTypeProperty = obj
        self.assertEqual(self.obj.protectedObjectTypeProperty, obj)
        # PYSIDE-535: Need to assign None to break the cycle
        self.obj.protectedObjectTypeProperty = None


class PrivateDtorProtectedMethodTest(unittest.TestCase):
    '''Test cases for classes with private destructors and protected methods.'''

    def tearDown(self):
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()
        self.assertEqual(cacheSize(), 0)

    def testProtectedMethod(self):
        '''Calls protected method of a class with a private destructor.'''
        obj = PrivateDtor.instance()

        self.assertEqual(type(obj), PrivateDtor)
        self.assertEqual(obj.instanceCalls(), 1)
        self.assertEqual(obj.instanceCalls(), obj.protectedInstanceCalls())
        obj = PrivateDtor.instance()
        self.assertEqual(obj.instanceCalls(), 2)
        self.assertEqual(obj.instanceCalls(), obj.protectedInstanceCalls())

if __name__ == '__main__':
    unittest.main()

