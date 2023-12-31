# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

project(QtUiTools)

set(QtUiTools_SRC
${QtUiTools_GEN_DIR}/quiloader_wrapper.cpp
# module is always needed
${QtUiTools_GEN_DIR}/qtuitools_module_wrapper.cpp
)

set(QtUiTools_glue_sources
    "${QtUiTools_SOURCE_DIR}/glue/plugins.h"
)

set(QtUiTools_include_dirs  ${QtUiTools_SOURCE_DIR}
                            ${QtUiTools_BINARY_DIR}
                            ${Qt${QT_MAJOR_VERSION}Core_INCLUDE_DIRS}
                            ${Qt${QT_MAJOR_VERSION}Gui_INCLUDE_DIRS}
                            ${Qt${QT_MAJOR_VERSION}Widgets_INCLUDE_DIRS}
                            ${Qt${QT_MAJOR_VERSION}Xml_INCLUDE_DIRS}
                            ${Qt${QT_MAJOR_VERSION}Designer_INCLUDE_DIRS}
                            ${Qt${QT_MAJOR_VERSION}UiTools_INCLUDE_DIRS}
                            ${libpyside_SOURCE_DIR}
                            ${plugins_SOURCE_DIR}
                            ${QtCore_GEN_DIR}
                            ${QtGui_GEN_DIR}
                            ${QtWidgets_GEN_DIR}
                            )
set(QtUiTools_libraries     pyside6
                            uiplugin
                            ${Qt${QT_MAJOR_VERSION}UiTools_LIBRARIES}
                            ${Qt${QT_MAJOR_VERSION}Core_LIBRARIES}
                            ${Qt${QT_MAJOR_VERSION}Gui_LIBRARIES}
                            ${Qt${QT_MAJOR_VERSION}Widgets_LIBRARIES}
                            )
set(QtUiTools_deps QtWidgets)

configure_file("${QtUiTools_SOURCE_DIR}/QtUiTools_global.pre.h.in"
               "${QtUiTools_BINARY_DIR}/QtUiTools_global.pre.h" @ONLY)

create_pyside_module(NAME QtUiTools
                     INCLUDE_DIRS QtUiTools_include_dirs
                     LIBRARIES QtUiTools_libraries
                     DEPS QtUiTools_deps
                     TYPESYSTEM_PATH QtUiTools_SOURCE_DIR
                     SOURCES QtUiTools_SRC
                     GLUE_SOURCES QtUiTools_glue_sources)
