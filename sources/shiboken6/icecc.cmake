# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

include (CMakeForceCompiler)
option(ENABLE_ICECC "Enable icecc checking, for distributed compilation")
if (ENABLE_ICECC)
    find_program(ICECC icecc)
    if (ICECC)
        message(STATUS "icecc found! Distributed compilation for all!! huhuhu.")
        cmake_force_cxx_compiler(${ICECC} icecc)
    else(ICECC)
        message(FATAL_ERROR "icecc NOT found! re-run cmake without -DENABLE_ICECC")
    endif(ICECC)
endif(ENABLE_ICECC)
