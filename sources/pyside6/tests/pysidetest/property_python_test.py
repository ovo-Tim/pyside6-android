# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

"""
Test for PySide's Property
==========================

This test is copied from Python's `test_property.py` and adapted to
the PySide Property implementation.

This test is to ensure maximum compatibility.
"""

# Test case for property
# more tests are in test_descr

import gc
import os
import sys
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)

from PySide6.QtCore import Property, QObject

# This are the original imports.
import sys
import unittest
has_test = False
try:
    from test import support
    has_test = True
except ImportError:
    pass


class PropertyBase(Exception):
    pass


class PropertyGet(PropertyBase):
    pass


class PropertySet(PropertyBase):
    pass


class PropertyDel(PropertyBase):
    pass


class BaseClass(QObject):
    def __init__(self):
        super().__init__()

        self._spam = 5

    @Property(object)
    def spam(self):
        """BaseClass.getter"""
        return self._spam

    @spam.setter
    def spam(self, value):
        self._spam = value

    @spam.deleter
    def spam(self):
        del self._spam
        # PYSIDE-535: Need to collect garbage in PyPy to trigger deletion
        gc.collect()


class SubClass(BaseClass):

    @BaseClass.spam.getter
    def spam(self):
        """SubClass.getter"""
        raise PropertyGet(self._spam)

    @spam.setter
    def spam(self, value):
        raise PropertySet(self._spam)

    @spam.deleter
    def spam(self):
        raise PropertyDel(self._spam)


class PropertyDocBase(object):
    _spam = 1

    def _get_spam(self):
        return self._spam
    spam = Property(object, _get_spam, doc="spam spam spam")


class PropertyDocSub(PropertyDocBase):
    @PropertyDocBase.spam.getter
    def spam(self):
        """The decorator does not use this doc string"""
        return self._spam


class PropertySubNewGetter(BaseClass):
    @BaseClass.spam.getter
    def spam(self):
        """new docstring"""
        return 5


class PropertyNewGetter(QObject):
    def __init__(self):
        super().__init__()

    @Property(object)
    def spam(self):
        """original docstring"""
        return 1

    @spam.getter
    def spam(self):
        """new docstring"""
        return 8


class PropertyTests(unittest.TestCase):
    def test_property_decorator_baseclass(self):
        # see #1620
        base = BaseClass()
        self.assertEqual(base.spam, 5)
        self.assertEqual(base._spam, 5)
        base.spam = 10
        self.assertEqual(base.spam, 10)
        self.assertEqual(base._spam, 10)
        delattr(base, "spam")
        self.assertTrue(not hasattr(base, "spam"))
        self.assertTrue(not hasattr(base, "_spam"))
        base.spam = 20
        self.assertEqual(base.spam, 20)
        self.assertEqual(base._spam, 20)

    def test_property_decorator_subclass(self):
        # see #1620
        sub = SubClass()
        self.assertRaises(PropertyGet, getattr, sub, "spam")
        self.assertRaises(PropertySet, setattr, sub, "spam", None)
        self.assertRaises(PropertyDel, delattr, sub, "spam")

    @unittest.skipIf(sys.flags.optimize >= 2,
                     "Docstrings are omitted with -O2 and above")
    def test_property_decorator_subclass_doc(self):
        sub = SubClass()
        self.assertEqual(sub.__class__.spam.__doc__, "SubClass.getter")

    @unittest.skipIf(sys.flags.optimize >= 2,
                     "Docstrings are omitted with -O2 and above")
    def test_property_decorator_baseclass_doc(self):
        base = BaseClass()
        self.assertEqual(base.__class__.spam.__doc__, "BaseClass.getter")

    def test_property_decorator_doc(self):
        base = PropertyDocBase()
        sub = PropertyDocSub()
        self.assertEqual(base.__class__.spam.__doc__, "spam spam spam")
        self.assertEqual(sub.__class__.spam.__doc__, "spam spam spam")

    @unittest.skipIf(sys.flags.optimize >= 2,
                     "Docstrings are omitted with -O2 and above")
    def test_property_getter_doc_override(self):
        newgettersub = PropertySubNewGetter()
        self.assertEqual(newgettersub.spam, 5)
        self.assertEqual(newgettersub.__class__.spam.__doc__, "new docstring")
        newgetter = PropertyNewGetter()
        self.assertEqual(newgetter.spam, 8)
        self.assertEqual(newgetter.__class__.spam.__doc__, "new docstring")

    @unittest.skipIf(sys.flags.optimize >= 2,
                     "Docstrings are omitted with -O2 and above")
    def test_property_builtin_doc_writable(self):
        p = Property(object, doc='basic')
        self.assertEqual(p.__doc__, 'basic')
        p.__doc__ = 'extended'
        self.assertEqual(p.__doc__, 'extended')

    @unittest.skipIf(sys.flags.optimize >= 2,
                     "Docstrings are omitted with -O2 and above")
    def test_property_decorator_doc_writable(self):
        class PropertyWritableDoc(object):

            @Property(object)
            def spam(self):
                """Eggs"""
                return "eggs"

        sub = PropertyWritableDoc()
        self.assertEqual(sub.__class__.spam.__doc__, 'Eggs')
        sub.__class__.spam.__doc__ = 'Spam'
        self.assertEqual(sub.__class__.spam.__doc__, 'Spam')

    if has_test:  # This test has no support in Python 2
        @support.refcount_test
        def test_refleaks_in___init__(self):
            gettotalrefcount = support.get_attribute(sys, 'gettotalrefcount')
            fake_prop = Property(object, 'fget', 'fset', "freset", 'fdel', 'doc')
            refs_before = gettotalrefcount()
            for i in range(100):
                fake_prop.__init__(object, 'fget', 'fset', "freset", 'fdel', 'doc')
            self.assertAlmostEqual(gettotalrefcount() - refs_before, 0, delta=10)


# Note: We ignore the whole subclass tests concerning __doc__ strings.
# See the original Python test starting with:
# "Issue 5890: subclasses of property do not preserve method __doc__ strings"


if __name__ == '__main__':
    unittest.main()
