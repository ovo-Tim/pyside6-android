# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

include(CMakeParseArguments)

# A version of cmake_parse_arguments that makes sure all arguments are processed and errors out
# with a message about ${type} having received unknown arguments.
macro(pyside_parse_all_arguments prefix type flags options multiopts)
    cmake_parse_arguments(${prefix} "${flags}" "${options}" "${multiopts}" ${ARGN})
    if(DEFINED ${prefix}_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unknown arguments were passed to ${type} (${${prefix}_UNPARSED_ARGUMENTS}).")
    endif()
endmacro()

macro(make_path varname)
   # accepts any number of path variables
   string(REPLACE ";" "${PATH_SEP}" ${varname} "${ARGN}")
endmacro()

macro(unmake_path varname)
   string(REPLACE "${PATH_SEP}" ";" ${varname} "${ARGN}")
endmacro()

# set size optimization flags for pyside6
macro(append_size_optimization_flags _module_name)
    if(NOT QFP_NO_OVERRIDE_OPTIMIZATION_FLAGS)
        if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
            target_compile_options(${_module_name} PRIVATE /Gy /Gw /EHsc)
            target_link_options(${_module_name} PRIVATE LINKER:/OPT:REF)
        elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU|CLANG")
            target_compile_options(${_module_name} PRIVATE -ffunction-sections -fdata-sections -fno-exceptions)
            target_link_options(${_module_name} PRIVATE LINKER:--gc-sections)
        endif()
    endif()
endmacro()

# Sample usage
# create_pyside_module(NAME QtGui
#                      INCLUDE_DIRS QtGui_include_dirs
#                      LIBRARIES QtGui_libraries
#                      DEPS QtGui_deps
#                      TYPESYSTEM_PATH QtGui_SOURCE_DIR
#                      SOURCES QtGui_SRC
#                      STATIC_SOURCES QtGui_static_sources
#                      TYPESYSTEM_NAME ${QtGui_BINARY_DIR}/typesystem_gui.xml
#                      DROPPED_ENTRIES QtGui_DROPPED_ENTRIES
#                      GLUE_SOURCES QtGui_glue_sources)
macro(create_pyside_module)
    pyside_parse_all_arguments(
        "module" # Prefix
        "create_pyside_module" # Macro name
        "" # Flags
        "NAME;TYPESYSTEM_PATH;TYPESYSTEM_NAME" # Single value
        "INCLUDE_DIRS;LIBRARIES;DEPS;SOURCES;STATIC_SOURCES;DROPPED_ENTRIES;GLUE_SOURCES" # Multival
        ${ARGN} # Number of arguments given when the macros is called
        )

    if ("${module_NAME}" STREQUAL "")
        message(FATAL_ERROR "create_pyside_module needs a NAME value.")
    endif()
    if ("${module_INCLUDE_DIRS}" STREQUAL "")
        message(FATAL_ERROR "create_pyside_module needs at least one INCLUDE_DIRS value.")
    endif()
    if ("${module_TYPESYSTEM_PATH}" STREQUAL "")
        message(FATAL_ERROR "create_pyside_module needs a TYPESYSTEM_PATH value.")
    endif()
    if ("${module_SOURCES}" STREQUAL "")
        message(FATAL_ERROR "create_pyside_module needs at least one SOURCES value.")
    endif()

    string(TOLOWER ${module_NAME} _module)
    string(REGEX REPLACE ^qt "" _module ${_module})

    if(${module_GLUE_SOURCES})
        set (module_GLUE_SOURCES "${${module_GLUE_SOURCES}}")
    else()
        set (module_GLUE_SOURCES "")
    endif()

    if (NOT EXISTS ${module_TYPESYSTEM_NAME})
        set(typesystem_path ${CMAKE_CURRENT_SOURCE_DIR}/typesystem_${_module}.xml)
    else()
        set(typesystem_path ${module_TYPESYSTEM_NAME})
    endif()

    # Create typesystem XML dependencies list, so that whenever they change, shiboken is invoked
    # automatically.
    # First add the main file.
    set(total_type_system_files ${typesystem_path})

    get_filename_component(typesystem_root "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)

    set(deps ${module_NAME} ${${module_DEPS}})
    foreach(dep ${deps})
        set(glob_expression "${typesystem_root}/${dep}/*.xml")
        file(GLOB type_system_files ${glob_expression})
        set(total_type_system_files ${total_type_system_files} ${type_system_files})
    endforeach(dep)

    # Remove any possible duplicates.
    list(REMOVE_DUPLICATES total_type_system_files)

    # Contains include directories to pass to shiboken's preprocessor (mkspec / global)
    get_target_property(qt_platform_includes Qt${QT_MAJOR_VERSION}::Platform
                        INTERFACE_INCLUDE_DIRECTORIES)
    # Add QtCore since include conventions are sometimes violated for its classes
    get_target_property(qt_core_includes Qt${QT_MAJOR_VERSION}::Core
                        INTERFACE_INCLUDE_DIRECTORIES)
    set(shiboken_include_dir_list ${pyside6_SOURCE_DIR} ${qt_platform_includes}
        ${qt_core_includes})

    # Transform the path separators into something shiboken understands.
    make_path(shiboken_include_dirs ${shiboken_include_dir_list})

    get_filename_component(pyside_binary_dir ${CMAKE_CURRENT_BINARY_DIR} DIRECTORY)

    # Install module glue files.
    string(TOLOWER ${module_NAME} lower_module_name)
    set(${module_NAME}_glue "${CMAKE_CURRENT_SOURCE_DIR}/../glue/${lower_module_name}.cpp")
    set(${module_name}_glue_dependency "")
    if(EXISTS ${${module_NAME}_glue})
        install(FILES ${${module_NAME}_glue} DESTINATION share/PySide6${pyside6_SUFFIX}/glue)
        set(${module_NAME}_glue_dependency ${${module_NAME}_glue})
    endif()

    # Install standalone glue files into typesystems subfolder, so that the resolved relative
    # paths remain correct.
    if (module_GLUE_SOURCES)
        install(FILES ${module_GLUE_SOURCES} DESTINATION share/PySide6${pyside6_SUFFIX}/typesystems/glue)
    endif()

    shiboken_get_tool_shell_wrapper(shiboken tool_wrapper)

    set(shiboken_command
        ${tool_wrapper}
        $<TARGET_FILE:Shiboken6::shiboken6>
        ${GENERATOR_EXTRA_FLAGS}
        "--include-paths=${shiboken_include_dirs}"
        "--typesystem-paths=${pyside_binary_dir}${PATH_SEP}${pyside6_SOURCE_DIR}${PATH_SEP}${${module_TYPESYSTEM_PATH}}"
        --output-directory=${CMAKE_CURRENT_BINARY_DIR}
        --license-file=${CMAKE_CURRENT_SOURCE_DIR}/../licensecomment.txt
        --lean-headers
        --api-version=${SUPPORTED_QT_VERSION})

    if(CMAKE_HOST_APPLE)
        set(shiboken_framework_include_dir_list ${QT_FRAMEWORK_INCLUDE_DIR})
        make_path(shiboken_framework_include_dirs ${shiboken_framework_include_dir_list})
        list(APPEND shiboken_command "--framework-include-paths=${shiboken_framework_include_dirs}")
    endif()

    if(${module_DROPPED_ENTRIES})
        list(JOIN ${module_DROPPED_ENTRIES} "\;" dropped_entries)
        list(APPEND shiboken_command "\"--drop-type-entries=${dropped_entries}\"")
    endif()

    list(APPEND shiboken_command "${pyside6_BINARY_DIR}/${module_NAME}_global.h"
         ${typesystem_path})

    add_custom_command( OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mjb_rejected_classes.log"
                        BYPRODUCTS ${${module_SOURCES}}
                        COMMAND ${shiboken_command}
                        DEPENDS ${total_type_system_files}
                                ${module_GLUE_SOURCES}
                                ${${module_NAME}_glue_dependency}
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        COMMENT "Running generator for ${module_NAME}...")

    include_directories(${module_NAME} ${${module_INCLUDE_DIRS}} ${pyside6_SOURCE_DIR})
    add_library(${module_NAME} MODULE ${${module_SOURCES}}
                                      ${${module_STATIC_SOURCES}})

    append_size_optimization_flags(${module_NAME})

    target_compile_definitions(${module_NAME} PRIVATE -DQT_LEAN_HEADERS=1)

    set_target_properties(${module_NAME} PROPERTIES
                          PREFIX ""
                          OUTPUT_NAME "${module_NAME}${SHIBOKEN_PYTHON_EXTENSION_SUFFIX}"
                          LIBRARY_OUTPUT_DIRECTORY ${pyside6_BINARY_DIR})
    if(WIN32)
        set_target_properties(${module_NAME} PROPERTIES SUFFIX ".pyd")
        # Sanitize windows.h as pulled by gl.h to prevent clashes with QAbstract3dAxis::min(), etc.
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNOMINMAX")
    endif()

    target_link_libraries(${module_NAME} ${${module_LIBRARIES}})
    target_link_libraries(${module_NAME} Shiboken6::libshiboken)
    if(${module_DEPS})
        add_dependencies(${module_NAME} ${${module_DEPS}})
    endif()
    create_generator_target(${module_NAME})

    # build type hinting stubs

    # Need to set the LD_ env vars before invoking the script, because it might use build-time
    # libraries instead of install time libraries.
    if (WIN32)
        set(ld_prefix_var_name "PATH")
    elseif(APPLE)
        set(ld_prefix_var_name "DYLD_LIBRARY_PATH")
    else()
        set(ld_prefix_var_name "LD_LIBRARY_PATH")
    endif()

    set(ld_prefix_list "")
    list(APPEND ld_prefix_list "${pysidebindings_BINARY_DIR}/libpyside")
    list(APPEND ld_prefix_list "${pysidebindings_BINARY_DIR}/libpysideqml")
    list(APPEND ld_prefix_list "${SHIBOKEN_SHARED_LIBRARY_DIR}")
    if(WIN32)
        list(APPEND ld_prefix_list "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
    endif()

    list(JOIN ld_prefix_list "${PATH_SEP}" ld_prefix_values_string)
    set(ld_prefix "${ld_prefix_var_name}=${ld_prefix_values_string}")

    # Append any existing ld_prefix values, so existing PATH, LD_LIBRARY_PATH, etc.
    # On Windows it is needed because pyside modules import Qt,
    # and the Qt modules are found from PATH.
    # On Linux and macOS, existing values might be set to find system libraries correctly.
    # For example on openSUSE when compiling with icc, libimf.so from Intel has to be found.
    if(WIN32)
        # Get the value of PATH with CMake separators.
        file(TO_CMAKE_PATH "$ENV{${ld_prefix_var_name}}" path_value)

        # Replace the CMake list separators with "\;"s, to avoid the PATH values being
        # interpreted as CMake list elements, we actually want to pass the whole string separated
        # by ";" to the command line.
        if(path_value)
            make_path(path_value "${path_value}")
            string(APPEND ld_prefix "${PATH_SEP}${path_value}")
        endif()
    else()
        # Handles both macOS and Linux.
        set(env_value "$ENV{${ld_prefix_var_name}}")
        if(env_value)
            string(APPEND ld_prefix ":${env_value}")
        endif()
    endif()

    qfp_strip_library("${module_NAME}")

    # Add target to generate pyi file, which depends on the module target.
    # Don't generate the files when cross-building because the target python can not be executed
    # on the host machine (usually, unless you use some userspace qemu based mechanism).
    # TODO: Can we do something better here to still get pyi files?
    if(NOT PYSIDE_IS_CROSS_BUILD)
        set(generate_pyi_options ${module_NAME} --sys-path
            "${pysidebindings_BINARY_DIR}"
            "${SHIBOKEN_PYTHON_MODULE_DIR}/..")     # use the layer above shiboken6
        if (QUIET_BUILD)
            list(APPEND generate_pyi_options "--quiet")
        endif()

        add_custom_target("${module_NAME}_pyi" ALL
                          COMMAND
                              ${CMAKE_COMMAND} -E env ${ld_prefix}
                              "${SHIBOKEN_PYTHON_INTERPRETER}"
                              "${CMAKE_CURRENT_SOURCE_DIR}/../support/generate_pyi.py"
                              ${generate_pyi_options})
        add_dependencies("${module_NAME}_pyi" ${module_NAME})

        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/../${module_NAME}.pyi"
                DESTINATION "${PYTHON_SITE_PACKAGES}/PySide6")
    endif()


    # install
    install(TARGETS ${module_NAME} LIBRARY DESTINATION "${PYTHON_SITE_PACKAGES}/PySide6")



    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PySide6/${module_NAME}/pyside6_${lower_module_name}_python.h
            DESTINATION include/PySide6${pyside6_SUFFIX}/${module_NAME}/)
    file(GLOB typesystem_files ${CMAKE_CURRENT_SOURCE_DIR}/typesystem_*.xml ${typesystem_path})

#   Copy typesystem files and remove module names from the <load-typesystem> element
#   so that it works in a flat directory:
#   <load-typesystem name="QtWidgets/typesystem_widgets.xml" ... ->
#   <load-typesystem name="typesystem_widgets.xml"
    foreach(typesystem_file ${typesystem_files})
        get_filename_component(typesystem_file_name "${typesystem_file}" NAME)
        file(READ "${typesystem_file}" typesystemXml)
        string(REGEX REPLACE "<load-typesystem name=\"[^/\"]+/" "<load-typesystem name=\"" typesystemXml "${typesystemXml}")
        set (typesystem_target_file "${CMAKE_CURRENT_BINARY_DIR}/PySide6/typesystems/${typesystem_file_name}")
        file(WRITE "${typesystem_target_file}" "${typesystemXml}")
        install(FILES "${typesystem_target_file}" DESTINATION share/PySide6${pyside6_SUFFIX}/typesystems)
    endforeach()
endmacro()

# Only add subdirectory if the associated Qt module is found.
# As a side effect, this macro now also defines the variable ${name}_GEN_DIR
# and must be called for every subproject.
macro(HAS_QT_MODULE var name)
    if (NOT DISABLE_${name} AND ${var})
        # we keep the PySide name here because this is compiled into shiboken
        set(${name}_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/${name}/PySide6/${name}
            CACHE INTERNAL "dir with generated source" FORCE)
        add_subdirectory(${name})
    else()
        # Used on documentation to skip modules
        set("if_${name}" "<!--" PARENT_SCOPE)
        set("end_${name}" "-->" PARENT_SCOPE)
    endif()
endmacro()

