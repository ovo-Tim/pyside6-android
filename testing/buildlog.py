# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

"""
testing/buildlog.py

A BuildLog holds the tests of a build.

BuildLog.classifiers finds the set of classifier strings.
"""

import os
import platform
import shutil
import sys
from collections import namedtuple
from textwrap import dedent

from .helper import script_dir

LogEntry = namedtuple("LogEntry", ["log_dir", "build_dir", "build_classifiers"])
is_ci = os.environ.get("QTEST_ENVIRONMENT", "") == "ci"


class BuildLog(object):
    """
    This class is a convenience wrapper around a list of log entries.

    The list of entries is sorted by date and checked for consistency.
    For simplicity and readability, the log entries are named tuples.

    """

    def __init__(self):
        history_dir = os.path.join(script_dir, "build_history")
        build_history = []
        for timestamp in os.listdir(history_dir):
            log_dir = os.path.join(history_dir, timestamp)
            if not os.path.isdir(log_dir):
                continue
            fpath = os.path.join(log_dir, "build_dir.txt")
            if not os.path.exists(fpath):
                continue
            with open(fpath) as f:
                f_contents = f.read().strip()
                f_contents_split = f_contents.splitlines()
                try:
                    if len(f_contents_split) == 2:
                        build_dir = f_contents_split[0]
                        build_classifiers = f_contents_split[1]
                    else:
                        build_dir = f_contents_split[0]
                        build_classifiers = ""
                except IndexError:
                    print(
                        dedent(
                            f"""
                        Error: There was an issue finding the build dir and its
                        characteristics, in the following considered file: '{fpath}'
                    """
                        )
                    )
                    sys.exit(1)

                if not os.path.exists(build_dir):
                    rel_dir, low_part = os.path.split(build_dir)
                    rel_dir, two_part = os.path.split(rel_dir)
                    if two_part.startswith("pyside") and two_part.endswith("build"):
                        build_dir = os.path.abspath(os.path.join(two_part, low_part))
                        if os.path.exists(build_dir):
                            print("Note: build_dir was probably moved.")
                        else:
                            print(f"Warning: missing build dir {build_dir}")
                            continue
            entry = LogEntry(log_dir, build_dir, build_classifiers)
            build_history.append(entry)
        # we take the latest build for now.
        build_history.sort()
        self.history = build_history
        self._buildno = None
        if not is_ci:
            # there seems to be a timing problem in RHel 7.6, so we better don't touch it
            self.prune_old_entries(history_dir)

    def prune_old_entries(self, history_dir):
        lst = []
        for timestamp in os.listdir(history_dir):
            log_dir = os.path.join(history_dir, timestamp)
            if not os.path.isdir(log_dir):
                continue
            lst.append(log_dir)
        if lst:

            def warn_problem(func, path, exc_info):
                cls, ins, _ = exc_info
                print(
                    f"rmtree({func.__name__}) warning: problem with "
                    f"{path}:\n   {cls.__name__}: {ins.args}"
                )

            lst.sort()
            log_dir = lst[-1]
            fname = os.path.basename(log_dir)
            ref_date_str = fname[:10]
            for log_dir in lst:
                fname = os.path.basename(log_dir)
                date_str = fname[:10]
                if date_str != ref_date_str:
                    shutil.rmtree(log_dir, onerror=warn_problem)

    def set_buildno(self, buildno):
        self.history[buildno]  # test
        self._buildno = buildno

    @property
    def selected(self):
        if self._buildno is None:
            return None
        if self.history is None:
            return None
        return self.history[self._buildno]

    @property
    def classifiers(self):
        if not self.selected:
            raise ValueError("+++ No build with the configuration found!")
        # Python2 legacy: Correct 'linux2' to 'linux', recommended way.
        plat_name = "linux" if sys.platform.startswith("linux") else sys.platform
        res = [plat_name, "qt6"]
        if is_ci:
            res.append("ci")
        if self.selected.build_classifiers:
            # Use classifier string encoded into build_dir.txt file.
            res.extend(self.selected.build_classifiers.split("-"))
        else:
            # the rest must be guessed from the given filename
            path = self.selected.build_dir
            base = os.path.basename(path)
            res.extend(base.split("-"))
        # add all the python and qt subkeys
        for entry in res:
            parts = entry.split(".")
            for idx in range(len(parts)):
                key = ".".join(parts[:idx])
                if key not in res:
                    res.append(key)
        # Allow to check the processor.
        # This gives "i386" or "arm" on macOS.
        res.append(platform.processor())
        return res


builds = BuildLog()
