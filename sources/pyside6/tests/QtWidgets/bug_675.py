# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import os
import sys

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
sys.path.append(os.fspath(Path(__file__).resolve().parents[1] / "util"))
from init_paths import init_test_paths
init_test_paths()

from PySide6.QtWidgets import (QApplication, QGraphicsLinearLayout,
                               QGraphicsScene, QGraphicsView, QTextEdit)


app = QApplication([])

scene = QGraphicsScene()

# don't segfault due to lack of keepReferenceCall
textEdit = scene.addWidget(QTextEdit())

layout = QGraphicsLinearLayout()
layout.addItem(textEdit)

view = QGraphicsView(scene)
view.show()
