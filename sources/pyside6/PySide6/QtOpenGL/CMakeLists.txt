# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

project(QtOpenGL)

set(QtOpenGL_DROPPED_ENTRIES)

set(QtOpenGL_static_sources
    "${QtOpenGL_SOURCE_DIR}/glue/utils.cpp"
)

set(QtOpenGL_SRC
${QtOpenGL_GEN_DIR}/qabstractopenglfunctions_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopengldebuglogger_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopengldebugmessage_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopenglframebufferobjectformat_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopenglpaintdevice_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopenglpixeltransferoptions_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopenglshaderprogram_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopengltexture_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopengltextureblitter_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopenglversionprofile_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopenglvertexarrayobject_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopenglvertexarrayobject_binder_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopenglwindow_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopenglbuffer_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopenglshader_wrapper.cpp
${QtOpenGL_GEN_DIR}/qopenglframebufferobject_wrapper.cpp
# module is always needed
${QtOpenGL_GEN_DIR}/qtopengl_module_wrapper.cpp
)

get_property(QtGui_enabled_features  TARGET Qt${QT_MAJOR_VERSION}::Gui
             PROPERTY QT_ENABLED_PUBLIC_FEATURES)

if("opengles2" IN_LIST QtGui_enabled_features)
    list(APPEND QtOpenGL_DROPPED_ENTRIES QOpenGLTimeMonitor QOpenGLTimerQuery
                QOpenGLVersionFunctionsFactory)
    list(APPEND QtOpenGL_SRC ${QtOpenGL_GEN_DIR}/qopenglfunctions_es2_wrapper.cpp)
    message(STATUS "Qt${QT_MAJOR_VERSION}Gui: Dropping Desktop OpenGL classes (GLES2)")
else()
    list(APPEND QtOpenGL_DROPPED_ENTRIES QOpenGLFunctions_ES2)
    list(APPEND QtOpenGL_SRC
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_1_0_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_1_1_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_1_2_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_1_3_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_1_4_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_1_5_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_2_0_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_2_1_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_3_0_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_3_1_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_3_2_compatibility_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_3_2_core_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_3_3_compatibility_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_3_3_core_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_4_0_compatibility_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_4_0_core_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_4_1_compatibility_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_4_1_core_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_4_2_compatibility_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_4_2_core_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_4_3_compatibility_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_4_3_core_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_4_4_compatibility_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_4_4_core_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_4_5_compatibility_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglfunctions_4_5_core_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopenglversionfunctionsfactory_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopengltimemonitor_wrapper.cpp
        ${QtOpenGL_GEN_DIR}/qopengltimerquery_wrapper.cpp)
    message(STATUS "Qt${QT_MAJOR_VERSION}OpenGL: Adding Desktop OpenGL classes")
endif()


configure_file("${QtOpenGL_SOURCE_DIR}/QtOpenGL_global.post.h.in"
               "${QtOpenGL_BINARY_DIR}/QtOpenGL_global.post.h" @ONLY)

set(QtOpenGL_include_dirs   ${QtOpenGL_SOURCE_DIR}
                            ${Qt${QT_MAJOR_VERSION}Core_INCLUDE_DIRS}
                            ${Qt${QT_MAJOR_VERSION}Gui_INCLUDE_DIRS}
                            ${Qt${QT_MAJOR_VERSION}OpenGL_INCLUDE_DIRS}
                            ${libpyside_SOURCE_DIR}
                            ${QtCore_GEN_DIR}
                            ${QtGui_GEN_DIR}
                            ${QtOpenGL_GEN_DIR}
                            )
set(QtOpenGL_libraries      pyside6
                            ${Qt${QT_MAJOR_VERSION}Core_LIBRARIES}
                            ${Qt${QT_MAJOR_VERSION}Gui_LIBRARIES}
                            ${Qt${QT_MAJOR_VERSION}OpenGL_LIBRARIES})
set(QtOpenGL_deps QtGui)

create_pyside_module(NAME QtOpenGL
                     INCLUDE_DIRS QtOpenGL_include_dirs
                     LIBRARIES QtOpenGL_libraries
                     DEPS QtOpenGL_deps
                     TYPESYSTEM_PATH QtOpenGL_SOURCE_DIR
                     SOURCES QtOpenGL_SRC
                     STATIC_SOURCES QtOpenGL_static_sources
                     DROPPED_ENTRIES QtOpenGL_DROPPED_ENTRIES)
