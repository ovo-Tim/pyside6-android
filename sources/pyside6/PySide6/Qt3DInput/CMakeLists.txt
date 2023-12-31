# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

project(Qt3DInput)

set(Qt3DInput_SRC
${Qt3DInput_GEN_DIR}/qt3dinput_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qabstractactioninput_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qabstractaxisinput_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qabstractphysicaldevice_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qaction_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qactioninput_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qanalogaxisinput_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qaxis_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qaxisaccumulator_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qaxissetting_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qbuttonaxisinput_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qinputaspect_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qinputchord_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qinputsequence_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qinputsettings_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qkeyboardhandler_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qkeyevent_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qlogicaldevice_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qkeyboarddevice_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qmousedevice_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qmouseevent_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qwheelevent_wrapper.cpp
${Qt3DInput_GEN_DIR}/qt3dinput_qmousehandler_wrapper.cpp
# module is always needed
${Qt3DInput_GEN_DIR}/qt3dinput_module_wrapper.cpp)

set(Qt3DInput_include_dirs
                     ${Qt3DInput_SOURCE_DIR}
                     ${Qt3DInput_BINARY_DIR}
                     ${pyside6_SOURCE_DIR}
                     ${Qt${QT_MAJOR_VERSION}Core_INCLUDE_DIRS}
                     ${libpyside_SOURCE_DIR}
                     ${QtCore_GEN_DIR}
                     ${QtGui_GEN_DIR}
                     ${Qt3DCore_GEN_DIR})

set(Qt3DInput_libraries pyside6
                     ${Qt${QT_MAJOR_VERSION}3DInput_LIBRARIES})

set(Qt3DInput_deps Qt3DCore)

create_pyside_module(NAME Qt3DInput
                     INCLUDE_DIRS Qt3DInput_include_dirs
                     LIBRARIES Qt3DInput_libraries
                     DEPS Qt3DInput_deps
                     TYPESYSTEM_PATH Qt3DInput_SOURCE_DIR
                     SOURCES Qt3DInput_SRC
                     TYPESYSTEM_NAME ${Qt3DInput_BINARY_DIR}/typesystem_3dinput.xml)
