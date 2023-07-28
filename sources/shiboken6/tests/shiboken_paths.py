# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0


import os
import sys
from pathlib import Path


def get_dir_env_var(var_name):
    """Return a directory set by an environment variable"""
    result = os.environ.get(var_name)
    if not result:
        raise ValueError(f'{var_name} is not set!')
    if not Path(result).is_dir():
        raise ValueError(f'{result} is not a directory!')
    return result


def get_build_dir():
    """
    Return the env var `BUILD_DIR`.
    If not set (interactive mode), take the last build history entry.
    """
    try:
        return get_dir_env_var('BUILD_DIR')
    except ValueError:
        look_for = Path("testing")
        here = Path(__file__).resolve().parent
        while here / look_for not in here.iterdir():
            import pprint
            parent = here.parent
            if parent == here:
                raise SystemError(look_for + " not found!")
            here = parent
        try:
            sys.path.insert(0, os.fspath(here))
            from testing.buildlog import builds
            if not builds.history:
                raise
            return builds.history[-1].build_dir
        finally:
            del sys.path[0]


def _prepend_path_var(var_name, paths):
    """Prepend additional paths to a path environment variable
       like PATH, LD_LIBRARY_PATH"""
    old_paths = os.environ.get(var_name)
    new_paths = os.pathsep.join(paths)
    if old_paths:
        new_paths += f'{os.pathsep}{old_paths}'
    os.environ[var_name] = new_paths


def add_python_dirs(python_dirs):
    """Add directories to the Python path unless present.
       Care is taken that the added directories come before
       site-packages."""
    new_paths = []
    for python_dir in python_dirs:
        new_paths.append(python_dir)
        if python_dir in sys.path:
            sys.path.remove(python_dir)
    sys.path[:0] = new_paths


def add_lib_dirs(lib_dirs):
    """Add directories to the platform's library path."""
    if sys.platform == 'win32':
        if sys.version_info >= (3, 8, 0):
            for lib_dir in lib_dirs:
                os.add_dll_directory(lib_dir)
        else:
            _prepend_path_var('PATH', lib_dirs)
    else:
        _prepend_path_var('LD_LIBRARY_PATH', lib_dirs)


def shiboken_paths(include_shiboken_tests=False):
    """Return a tuple of python directories/lib directories to be set for
       using the shiboken6 module from the build directory or running the
       shiboken tests depending on a single environment variable BUILD_DIR
       pointing to the build directory."""
    src_dir = Path(__file__).resolve().parent
    python_dirs = []
    if include_shiboken_tests:
        python_dirs.append(os.fspath(src_dir))  # For shiboken_test_helper
    python_dirs.append(get_build_dir())  # for toplevel shiboken6 import
    shiboken_dir = Path(get_build_dir()) / 'shiboken6'
    lib_dirs = [os.fspath(shiboken_dir / 'libshiboken')]
    if include_shiboken_tests:
        shiboken_test_dir = shiboken_dir /'tests'
        for module in ['minimal', 'sample', 'smart', 'other']:
            module_dir = shiboken_test_dir / f"{module}binding"
            python_dirs.append(os.fspath(module_dir))
            lib_dir = shiboken_test_dir / f"lib{module}"
            lib_dirs.append(os.fspath(lib_dir))
    return (python_dirs, lib_dirs)


def init_paths():
    """Sets the correct import paths (Python modules and C++ library
       paths) for testing shiboken depending on a single
       environment variable BUILD_DIR pointing to the build
       directory."""
    paths = shiboken_paths(True)
    add_python_dirs(paths[0])
    add_lib_dirs(paths[1])
