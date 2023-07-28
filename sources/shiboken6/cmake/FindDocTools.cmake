# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

find_program(SPHINX_BUILD sphinx-build DOC "Path to sphinx-build binary.")

# graphviz dot appears to be used by sphinx and not by CMake directly. This is just found to check
# if it exists.
find_program(DOT_EXEC dot)

# Lookup for qdoc and qhelpgenerator in multiple sources: ccache var, PATH or CMake package.
set(qhelpgenerator_binary "")
set(qdoc_binary "")
if(QHELPGENERATOR_EXECUTABLE)
    set(qhelpgenerator_binary "${QHELPGENERATOR_EXECUTABLE}")
else()
    find_package(Qt6 QUIET COMPONENTS Tools)
    if(TARGET Qt6::qhelpgenerator)
        get_target_property(qhelpgenerator_binary Qt6::qhelpgenerator IMPORTED_LOCATION)
    else()
        find_program(QHELPGENERATOR_EXECUTABLE qhelpgenerator DOC "Path to qhelpgenerator binary.")
        if(QHELPGENERATOR_EXECUTABLE)
            set(qhelpgenerator_binary "${QHELPGENERATOR_EXECUTABLE}")
        endif()
    endif()
endif()

if(QDOC_EXECUTABLE)
    set(qdoc_binary "${QDOC_EXECUTABLE}")
else()
    find_package(Qt6 QUIET COMPONENTS Tools)
    if(TARGET Qt6::qdoc)
        get_target_property(qdoc_binary Qt6::qdoc IMPORTED_LOCATION)
    else()
        find_program(QDOC_EXECUTABLE qdoc DOC "Path to qdoc binary.")
        if(QDOC_EXECUTABLE)
            set(qdoc_binary "${QDOC_EXECUTABLE}")
        endif()
    endif()
endif()
