# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

"""
The bug was caused by this commit:
"Support the qApp macro correctly, final version incl. debug"
e30e0c161b2b4d50484314bf006e9e5e8ff6b380
2017-10-27

The bug was first solved by this commit:
"Fix qApp macro refcount"
b811c874dedd14fd8b072bc73761d39255216073
2018-03-21

This test triggers the refcounting bug of qApp, issue PYSIDE-585.
Finally, the real patch included more changes, because another error
was in the ordering of shutdown calls. It was found using the following
Python configuration:

    In Python 3.6 create a directory 'debug' and cd into it.

    ../configure --with-pydebug --prefix=$HOME/pydebug/ --enable-shared

Then a lot more refcounting errors show up, which are due to a bug in
the code position of the shutdown procedure.
The reason for the initial refcount bug was that the shutdown code is once
more often called than the creation of the qApp wrapper.
Finally, it was easiest and more intuitive to simply make the refcount of
qApp_content equal to that of Py_None, which is also not supposed to be
garbage-collected.

For some reason, the test does not work as a unittest because it creates
no crash. We leave it this way.
"""

import os
import sys

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
sys.path.append(os.fspath(Path(__file__).resolve().parents[1] / "util"))
from init_paths import init_test_paths
init_test_paths()

from PySide6.QtCore import QTimer
from PySide6.QtWidgets import QApplication


app_instance = QApplication([])
# If the following line is commented, application doesn't crash on exit anymore.
app_instance2 = app_instance
QTimer.singleShot(0, qApp.quit)
app_instance.exec_()
