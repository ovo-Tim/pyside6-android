# Copyright (C) 2013 Riverbank Computing Limited.
# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

"""PySide6 port of the widgets/dialogs/standarddialogs example from Qt v5.x"""

import sys
from textwrap import dedent

from PySide6.QtCore import QDir, Qt, Slot
from PySide6.QtGui import QFont, QPalette
from PySide6.QtWidgets import (QApplication, QColorDialog, QCheckBox, QDialog,
                               QErrorMessage, QFontDialog, QFileDialog, QFrame,
                               QGridLayout, QGroupBox, QInputDialog, QLabel,
                               QLineEdit, QMessageBox, QPushButton,
                               QSizePolicy, QSpacerItem, QToolBox,
                               QVBoxLayout, QWidget)


class DialogOptionsWidget(QGroupBox):
    """Widget displaying a number of check boxes representing the dialog
       options."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self._layout = QVBoxLayout(self)
        self._mapping = {}

    def value(self):
        result = 0
        for checkbox, value in self._mapping.items():
            if checkbox.isChecked():
                result |= value
        return result

    def add_checkbox(self, text, value):
        checkbox = QCheckBox(text)
        self._layout.addWidget(checkbox)
        self._mapping[checkbox] = value


class Dialog(QDialog):

    def __init__(self, parent=None):
        super().__init__(parent)

        self._open_files_path = ''

        self._error_message_dialog = QErrorMessage(self)

        frame_style = QFrame.Sunken | QFrame.Panel

        self._integer_label = QLabel()
        self._integer_label.setFrameStyle(frame_style)
        self._integer_button = QPushButton("QInputDialog.get&Integer()")

        self._double_label = QLabel()
        self._double_label.setFrameStyle(frame_style)
        self._double_button = QPushButton("QInputDialog.get&Double()")

        self._item_label = QLabel()
        self._item_label.setFrameStyle(frame_style)
        self._item_button = QPushButton("QInputDialog.getIte&m()")

        self._text_label = QLabel()
        self._text_label.setFrameStyle(frame_style)
        self._text_button = QPushButton("QInputDialog.get&Text()")

        self._multiline_text_label = QLabel()
        self._multiline_text_label.setFrameStyle(frame_style)
        self._multiline_text_button = QPushButton("QInputDialog::get&MultiLineText()")

        self._color_label = QLabel()
        self._color_label.setFrameStyle(frame_style)
        self._color_button = QPushButton("QColorDialog.get&Color()")
        self._color_options = DialogOptionsWidget()
        self._color_options.add_checkbox("Show alpha channel",
                                         QColorDialog.ShowAlphaChannel)
        self._color_options.add_checkbox("No buttons",
                                         QColorDialog.NoButtons)

        self._font_label = QLabel()
        self._font_label.setFrameStyle(frame_style)
        self._font_button = QPushButton("QFontDialog.get&Font()")
        self._font_options = DialogOptionsWidget()
        self._font_options.add_checkbox("Do not use native dialog",
                                        QFontDialog.DontUseNativeDialog)
        self._font_options.add_checkbox("Show scalable fonts",
                                        QFontDialog.ScalableFonts)
        self._font_options.add_checkbox("Show non-scalable fonts",
                                        QFontDialog.NonScalableFonts)
        self._font_options.add_checkbox("Show monospaced fonts",
                                        QFontDialog.MonospacedFonts)
        self._font_options.add_checkbox("Show proportional fonts",
                                        QFontDialog.ProportionalFonts)
        self._font_options.add_checkbox("No buttons", QFontDialog.NoButtons)

        self._directory_label = QLabel()
        self._directory_label.setFrameStyle(frame_style)
        self._directory_button = QPushButton("QFileDialog.getE&xistingDirectory()")

        self._open_file_name_label = QLabel()
        self._open_file_name_label.setFrameStyle(frame_style)
        self._open_file_name_button = QPushButton("QFileDialog.get&OpenFileName()")

        self._open_file_names_label = QLabel()
        self._open_file_names_label.setFrameStyle(frame_style)
        self._open_file_names_button = QPushButton("QFileDialog.&getOpenFileNames()")

        self._save_file_name_label = QLabel()
        self._save_file_name_label.setFrameStyle(frame_style)
        self._save_file_name_button = QPushButton("QFileDialog.get&SaveFileName()")

        self._file_options = DialogOptionsWidget()
        self._file_options.add_checkbox("Do not use native dialog",
                                        QFileDialog.DontUseNativeDialog)
        self._file_options.add_checkbox("Show directories only",
                                        QFileDialog.ShowDirsOnly)
        self._file_options.add_checkbox("Do not resolve symlinks",
                                        QFileDialog.DontResolveSymlinks)
        self._file_options.add_checkbox("Do not confirm overwrite",
                                        QFileDialog.DontConfirmOverwrite)
        self._file_options.add_checkbox("Readonly", QFileDialog.ReadOnly)
        self._file_options.add_checkbox("Hide name filter details",
                                        QFileDialog.HideNameFilterDetails)
        self._file_options.add_checkbox("Do not use custom directory icons (Windows)",
                                        QFileDialog.DontUseCustomDirectoryIcons)

        self._critical_label = QLabel()
        self._critical_label.setFrameStyle(frame_style)
        self._critical_button = QPushButton("QMessageBox.critica&l()")

        self._information_label = QLabel()
        self._information_label.setFrameStyle(frame_style)
        self._information_button = QPushButton("QMessageBox.i&nformation()")

        self._question_label = QLabel()
        self._question_label.setFrameStyle(frame_style)
        self._question_button = QPushButton("QMessageBox.&question()")

        self._warning_label = QLabel()
        self._warning_label.setFrameStyle(frame_style)
        self._warning_button = QPushButton("QMessageBox.&warning()")

        self._error_button = QPushButton("QErrorMessage.showM&essage()")

        self._integer_button.clicked.connect(self.set_integer)
        self._double_button.clicked.connect(self.set_double)
        self._item_button.clicked.connect(self.set_item)
        self._text_button.clicked.connect(self.set_text)
        self._multiline_text_button.clicked.connect(self.set_multiline_text)

        self._color_button.clicked.connect(self.set_color)
        self._font_button.clicked.connect(self.set_font)
        self._directory_button.clicked.connect(self.set_existing_directory)
        self._open_file_name_button.clicked.connect(self.set_open_file_name)
        self._open_file_names_button.clicked.connect(self.set_open_file_names)
        self._save_file_name_button.clicked.connect(self.set_save_file_name)
        self._critical_button.clicked.connect(self.critical_message)
        self._information_button.clicked.connect(self.information_message)
        self._question_button.clicked.connect(self.question_message)
        self._warning_button.clicked.connect(self.warning_message)
        self._error_button.clicked.connect(self.error_message)

        vertical_layout = QVBoxLayout(self)
        toolbox = QToolBox()

        vertical_layout.addWidget(toolbox)
        page = QWidget()
        layout = QGridLayout(page)
        layout.addWidget(self._integer_button, 0, 0)
        layout.addWidget(self._integer_label, 0, 1)
        layout.addWidget(self._double_button, 1, 0)
        layout.addWidget(self._double_label, 1, 1)
        layout.addWidget(self._item_button, 2, 0)
        layout.addWidget(self._item_label, 2, 1)
        layout.addWidget(self._text_button, 3, 0)
        layout.addWidget(self._text_label, 3, 1)
        layout.addWidget(self._multiline_text_label, 4, 1)
        layout.addWidget(self._multiline_text_button, 4, 0)
        spacer = QSpacerItem(0, 0, QSizePolicy.Ignored, QSizePolicy.MinimumExpanding)
        layout.addItem(spacer, 5, 0)
        toolbox.addItem(page, "Input Dialogs")

        page = QWidget()
        layout = QGridLayout(page)
        layout.addWidget(self._color_button, 0, 0)
        layout.addWidget(self._color_label, 0, 1)
        spacer = QSpacerItem(0, 0, QSizePolicy.Ignored, QSizePolicy.MinimumExpanding)
        layout.addItem(spacer, 1, 0)
        layout.addWidget(self._color_options, 2, 0, 1, 2)
        toolbox.addItem(page, "Color Dialog")

        page = QWidget()
        layout = QGridLayout(page)
        layout.addWidget(self._font_button, 0, 0)
        layout.addWidget(self._font_label, 0, 1)
        spacer = QSpacerItem(0, 0, QSizePolicy.Ignored, QSizePolicy.MinimumExpanding)
        layout.addItem(spacer, 1, 0)
        layout.addWidget(self._font_options, 2, 0, 1, 2)
        toolbox.addItem(page, "Font Dialog")

        page = QWidget()
        layout = QGridLayout(page)
        layout.addWidget(self._directory_button, 0, 0)
        layout.addWidget(self._directory_label, 0, 1)
        layout.addWidget(self._open_file_name_button, 1, 0)
        layout.addWidget(self._open_file_name_label, 1, 1)
        layout.addWidget(self._open_file_names_button, 2, 0)
        layout.addWidget(self._open_file_names_label, 2, 1)
        layout.addWidget(self._save_file_name_button, 3, 0)
        layout.addWidget(self._save_file_name_label, 3, 1)
        spacer = QSpacerItem(0, 0, QSizePolicy.Ignored, QSizePolicy.MinimumExpanding)
        layout.addItem(spacer, 4, 0)
        layout.addWidget(self._file_options, 5, 0, 1, 2)

        toolbox.addItem(page, "File Dialogs")

        page = QWidget()
        layout = QGridLayout(page)
        layout.addWidget(self._critical_button, 0, 0)
        layout.addWidget(self._critical_label, 0, 1)
        layout.addWidget(self._information_button, 1, 0)
        layout.addWidget(self._information_label, 1, 1)
        layout.addWidget(self._question_button, 2, 0)
        layout.addWidget(self._question_label, 2, 1)
        layout.addWidget(self._warning_button, 3, 0)
        layout.addWidget(self._warning_label, 3, 1)
        layout.addWidget(self._error_button, 4, 0)
        spacer = QSpacerItem(0, 0, QSizePolicy.Ignored, QSizePolicy.MinimumExpanding)
        layout.addItem(spacer, 5, 0)
        toolbox.addItem(page, "Message Boxes")

        self.setWindowTitle("Standard Dialogs")

    @Slot()
    def set_integer(self):
        i, ok = QInputDialog.getInt(self,
                "QInputDialog.getInteger()", "Percentage:", 25, 0, 100, 1)
        if ok:
            self._integer_label.setText(f"{i}%")

    @Slot()
    def set_double(self):
        d, ok = QInputDialog.getDouble(self, "QInputDialog.getDouble()",
                "Amount:", 37.56, -10000, 10000, 2)
        if ok:
            self._double_label.setText(f"${d:g}")

    @Slot()
    def set_item(self):
        items = ("Spring", "Summer", "Fall", "Winter")

        item, ok = QInputDialog.getItem(self, "QInputDialog.getItem()",
                "Season:", items, 0, False)
        if ok and item:
            self._item_label.setText(item)

    @Slot()
    def set_text(self):
        text, ok = QInputDialog.getText(self, "QInputDialog.getText()",
                "User name:", QLineEdit.Normal,
                QDir.home().dirName())
        if ok and text != '':
            self._text_label.setText(text)

    @Slot()
    def set_multiline_text(self):
        text, ok = QInputDialog.getMultiLineText(self, "QInputDialog::getMultiLineText()",
                "Address:", "John Doe\nFreedom Street")
        if ok and text != '':
            self._multiline_text_label.setText(text)

    @Slot()
    def set_color(self):
        options_value = self._color_options.value()
        options = QColorDialog.ColorDialogOptions(options_value)
        color = QColorDialog.getColor(Qt.green, self, "Select Color", options)

        if color.isValid():
            self._color_label.setText(color.name())
            self._color_label.setPalette(QPalette(color))
            self._color_label.setAutoFillBackground(True)

    @Slot()
    def set_font(self):
        options_value = self._font_options.value()
        options = QFontDialog.FontDialogOptions(options_value)

        description = self._font_label.text()
        default_font = QFont()
        if description:
            default_font.fromString(description)

        ok, font = QFontDialog.getFont(default_font, self, "Select Font", options)
        if ok:
            self._font_label.setText(font.key())
            self._font_label.setFont(font)

    @Slot()
    def set_existing_directory(self):
        options_value = self._file_options.value()
        options = QFileDialog.Options(options_value) | QFileDialog.ShowDirsOnly

        directory = QFileDialog.getExistingDirectory(self,
                "QFileDialog.getExistingDirectory()",
                self._directory_label.text(), options)
        if directory:
            self._directory_label.setText(directory)

    @Slot()
    def set_open_file_name(self):
        options_value = self._file_options.value()
        options = QFileDialog.Options(options_value)

        fileName, filtr = QFileDialog.getOpenFileName(self,
                "QFileDialog.getOpenFileName()",
                self._open_file_name_label.text(),
                "All Files (*);;Text Files (*.txt)", "", options)
        if fileName:
            self._open_file_name_label.setText(fileName)

    @Slot()
    def set_open_file_names(self):
        options_value = self._file_options.value()
        options = QFileDialog.Options(options_value)

        files, filtr = QFileDialog.getOpenFileNames(self,
                "QFileDialog.getOpenFileNames()", self._open_files_path,
                "All Files (*);;Text Files (*.txt)", "", options)
        if files:
            self._open_files_path = files[0]
            file_list = ', '.join(files)
            self._open_file_names_label.setText(f"[{file_list}]")

    @Slot()
    def set_save_file_name(self):
        options_value = self._file_options.value()
        options = QFileDialog.Options(options_value)

        fileName, filtr = QFileDialog.getSaveFileName(self,
                "QFileDialog.getSaveFileName()",
                self._save_file_name_label.text(),
                "All Files (*);;Text Files (*.txt)", "", options)
        if fileName:
            self._save_file_name_label.setText(fileName)

    @Slot()
    def critical_message(self):
        m = dedent("""\
                   Activating the liquid oxygen stirring fans caused an
                   explosion in one of the tanks. Liquid oxygen levels
                   are getting low. This may jeopardize the moon landing mission.""")
        msg_box = QMessageBox(QMessageBox.Critical, "QMessageBox.critical()",
                              "Houston, we have a problem",
                              QMessageBox.Abort | QMessageBox.Retry | QMessageBox.Ignore,
                              self)
        msg_box.setInformativeText(m)
        reply = msg_box.exec()
        if reply == QMessageBox.Abort:
            self._critical_label.setText("Abort")
        elif reply == QMessageBox.Retry:
            self._critical_label.setText("Retry")
        else:
            self._critical_label.setText("Ignore")

    @Slot()
    def information_message(self):
        m = dedent("""\
                   This phrase was often used by public address announcers at
                   the conclusion of Elvis Presley concerts in order to
                   disperse audiences who lingered in hopes of an encore.
                   It has since become a catchphrase and punchline.""")
        msg_box = QMessageBox(QMessageBox.Information, "QMessageBox.information()",
                              "Elvis has left the building.",
                              QMessageBox.Ok, self)
        msg_box.setInformativeText(m)
        reply = msg_box.exec()
        if reply == QMessageBox.Ok:
            self._information_label.setText("OK")
        else:
            self._information_label.setText("Escape")

    @Slot()
    def question_message(self):
        m = dedent("""\
                   A cheeseburger is a hamburger topped with cheese.
                   Traditionally, the slice of cheese is placed on top of the
                   meat patty. The cheese is usually added to the cooking
                   hamburger patty shortly before serving, which allows the
                   cheese to melt.""")
        msg_box = QMessageBox(QMessageBox.Question, "QMessageBox.question()",
                              "Would you like cheese with that?",
                              QMessageBox.Yes | QMessageBox.No | QMessageBox.Cancel)
        msg_box.setInformativeText(m)
        reply = msg_box.exec()
        if reply == QMessageBox.Yes:
            self._question_label.setText("Yes")
        elif reply == QMessageBox.No:
            self._question_label.setText("No")
        else:
            self._question_label.setText("Cancel")

    @Slot()
    def warning_message(self):
        msg_box = QMessageBox(QMessageBox.Warning, "QMessageBox.warning()",
                              "Delete the only copy of your movie manuscript?",
                              QMessageBox.NoButton, self)
        m = "You've been working on this manuscript for 738 days now. Hang in there!"
        msg_box.setInformativeText(m)
        msg_box.setDetailedText('"A long time ago in a galaxy far, far away...."')
        msg_box.addButton("&Keep", QMessageBox.AcceptRole)
        msg_box.addButton("Delete", QMessageBox.RejectRole)
        if msg_box.exec() == QMessageBox.AcceptRole:
            self._warning_label.setText("Keep")
        else:
            self._warning_label.setText("Delete")

    @Slot()
    def error_message(self):
        m = dedent("""\
                   This dialog shows and remembers error messages. If the
                   user chooses to not  show the dialog again, the dialog
                   will not appear again if QErrorMessage.showMessage()
                   is called with the same message.""")
        self._error_message_dialog.showMessage(m)
        m = dedent("""\
                   You can queue up error messages, and they will be
                   shown one after each other. Each message maintains
                   its own state for whether it will be shown again
                   the next time QErrorMessage::showMessage() is called
                   with the same message.""")
        self._error_message_dialog.showMessage(m)


if __name__ == '__main__':
    app = QApplication(sys.argv)
    dialog = Dialog()
    availableGeometry = dialog.screen().availableGeometry()
    dialog.resize(availableGeometry.width() / 3, availableGeometry.height() * 2 / 3)
    dialog.move((availableGeometry.width() - dialog.width()) / 2,
                (availableGeometry.height() - dialog.height()) / 2)
    sys.exit(dialog.exec())
