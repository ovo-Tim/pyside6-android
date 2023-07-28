#!/usr/bin/env python
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

'''Test cases for std::list container conversions'''

import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from shiboken_paths import init_paths
init_paths()

from sample import ListUser, Point, PointF

class ExtendedListUser(ListUser):
    def __init__(self):
        ListUser.__init__(self)
        self.create_list_called = False

    def createList(self):
        self.create_list_called = True
        return [2, 3, 5, 7, 13]

class ListConversionTest(unittest.TestCase):
    '''Test case for std::list container conversions'''

    def testReimplementedVirtualMethodCall(self):
        '''Test if a Python override of a virtual method is correctly called from C++.'''
        lu = ExtendedListUser()
        lst = lu.callCreateList()
        self.assertTrue(lu.create_list_called)
        self.assertEqual(type(lst), list)
        for item in lst:
            self.assertEqual(type(item), int)

    def testPrimitiveConversionInsideContainer(self):
        '''Test primitive type conversion inside conversible std::list container.'''
        cpx0 = complex(1.2, 3.4)
        cpx1 = complex(5.6, 7.8)
        lst = ListUser.createComplexList(cpx0, cpx1)
        self.assertEqual(type(lst), list)
        for item in lst:
            self.assertEqual(type(item), complex)
        self.assertEqual(lst, [cpx0, cpx1])

    def testSumListIntegers(self):
        '''Test method that sums a list of integer values.'''
        lu = ListUser()
        lst = [3, 5, 7]
        result = lu.sumList(lst)
        self.assertEqual(result, sum(lst))

    def testSumListFloats(self):
        '''Test method that sums a list of float values.'''
        lu = ListUser()
        lst = [3.3, 4.4, 5.5]
        result = lu.sumList(lst)
        self.assertEqual(result, sum(lst))

    def testConversionInBothDirections(self):
        '''Test converting a list from Python to C++ and back again.'''
        lu = ListUser()
        lst = [3, 5, 7]
        lu.setList(lst)
        result = lu.getList()
        self.assertEqual(result, lst)

    def testConversionInBothDirectionsWithSimilarContainer(self):
        '''Test converting a tuple, instead of the expected list, from Python to C++ and back again.'''
        lu = ListUser()
        lst = (3, 5, 7)
        lu.setList(lst)
        result = lu.getList()
        self.assertNotEqual(result, lst)
        self.assertEqual(result, list(lst))

    def testConversionOfListOfObjectsPassedAsArgument(self):
        '''Calls method with a Python list of wrapped objects to be converted to a C++ list.'''
        mult = 3
        pts0 = (Point(1.0, 2.0), Point(3.3, 4.4), Point(5, 6))
        pts1 = (Point(1.0, 2.0), Point(3.3, 4.4), Point(5, 6))
        ListUser.multiplyPointList(pts1, mult)
        for pt0, pt1 in zip(pts0, pts1):
            self.assertEqual(pt0.x() * mult, pt1.x())
            self.assertEqual(pt0.y() * mult, pt1.y())

    def testConversionOfInvalidLists(self):
        mult = 3
        pts = (Point(1.0, 2.0), 3, Point(5, 6))
        self.assertRaises(TypeError, ListUser.multiplyPointList, pts, mult)

    def testOverloadMethodReceivingRelatedContainerTypes(self):
        self.assertEqual(ListUser.ListOfPointF, ListUser.listOfPoints([PointF()]))
        self.assertEqual(ListUser.ListOfPoint, ListUser.listOfPoints([Point()]))

if __name__ == '__main__':
    unittest.main()

