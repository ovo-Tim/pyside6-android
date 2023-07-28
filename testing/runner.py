# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

import inspect
import os
import re
import subprocess
import sys
from subprocess import TimeoutExpired
from textwrap import dedent

# Get the dir path to the utils module
try:
    this_file = __file__
except NameError:
    this_file = sys.argv[0]
this_file = os.path.abspath(this_file)
this_dir = os.path.dirname(this_file)
build_scripts_dir = os.path.abspath(os.path.join(this_dir, ".."))

sys.path.append(build_scripts_dir)
from build_scripts.utils import detect_clang


class TestRunner(object):
    def __init__(self, log_entry, project, index):
        self.log_entry = log_entry
        built_path = log_entry.build_dir
        self.test_dir = os.path.join(built_path, project)
        log_dir = log_entry.log_dir
        if index is not None:
            self.logfile = os.path.join(log_dir, f"{project}.{index}.log")
        else:
            self.logfile = os.path.join(log_dir, f"{project}.log")
        os.environ["CTEST_OUTPUT_ON_FAILURE"] = "1"
        self._setup_clang()
        self._setup()

    def _setup_clang(self):
        if sys.platform != "win32":
            return
        clang_dir = detect_clang()
        if clang_dir[0]:
            clang_bin_dir = os.path.join(clang_dir[0], "bin")
            path = os.environ.get("PATH")
            if clang_bin_dir not in path:
                os.environ["PATH"] = clang_bin_dir + os.pathsep + path
                print(f"Adding {clang_bin_dir} as detected by {clang_dir[1]} to PATH")

    def _find_ctest_in_file(self, file_name):
        """
        Helper for _find_ctest() that finds the ctest binary in a build
        system file (ninja, Makefile).
        """
        look_for = "--force-new-ctest-process"
        line = None
        with open(file_name) as makefile:
            for line in makefile:
                if look_for in line:
                    break
            else:
                # We have probably forgotten to build the tests.
                # Give a nice error message with a shortened but exact path.
                rel_path = os.path.relpath(file_name)
                msg = dedent(
                    f"""\n
                    {'*' * 79}
                    **  ctest is not in '{rel_path}'.
                    *   Did you forget to build the tests with '--build-tests' in setup.py?
                    """
                )
                raise RuntimeError(msg)
        # the ctest program is on the left to look_for
        assert line, f"Did not find {look_for}"
        ctest = re.search(r'(\S+|"([^"]+)")\s+' + look_for, line).groups()
        return ctest[1] or ctest[0]

    def _find_ctest(self):
        """
        Find ctest in a build system file (ninja, Makefile)

        We no longer use make, but the ctest command directly.
        It is convenient to look for the ctest program using the Makefile.
        This serves us two purposes:

          - there is no dependency of the PATH variable,
          - each project is checked whether ctest was configured.
        """
        candidate_files = ["Makefile", "build.ninja"]
        for candidate in candidate_files:
            path = os.path.join(self.test_dir, candidate)
            if os.path.exists(path):
                return self._find_ctest_in_file(path)
        raise RuntimeError(
            "Cannot find any of the build system files " f"{', '.join(candidate_files)}."
        )

    def _setup(self):
        self.ctestCommand = self._find_ctest()

    def _run(self, cmd_tuple, label, timeout):
        """
        Perform a test run in a given build

        The build can be stopped by a keyboard interrupt for testing
        this script. Also, a timeout can be used.

        After the change to directly using ctest, we no longer use
        "--force-new-ctest-process". Until now this has no drawbacks
        but was a little faster.
        """

        self.cmd = cmd_tuple
        # We no longer use the shell option. It introduces wrong handling
        # of certain characters which are not yet correctly escaped:
        # Especially the "^" caret char is treated as an escape, and pipe symbols
        # without a caret are interpreted as such which leads to weirdness.
        # Since we have all commands with explicit paths and don't use shell
        # commands, this should work fine.
        print(
            dedent(
                f"""\
            running {self.cmd}
                 in {self.test_dir}
            """
            )
        )
        ctest_process = subprocess.Popen(
            self.cmd, cwd=self.test_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )

        def py_tee(input, output, label):
            """
            A simple (incomplete) tee command in Python

            This script simply logs everything from input to output
            while the output gets some decoration. The specific reason
            to have this script at all is:

            - it is necessary to have some decoration as prefix, since
              we run commands several times

            - collecting all output and then decorating is not nice if
              you have to wait for a long time

            The special escape is for the case of an embedded file in
            the output.
            """

            def xprint(*args, **kw):
                print(*args, file=output, **kw)

            # 'for line in input:' would read into too large chunks
            labelled = True
            # make sure that this text is not found in a traceback of the runner!
            text_a = "BEGIN" "_FILE"
            text_z = "END" "_FILE"
            while True:
                line = input.readline()
                if not line:
                    break
                if line.startswith(text_a):
                    labelled = False
                txt = line.rstrip()
                xprint(label, txt) if label and labelled else xprint(txt)
                if line.startswith(text_z):
                    labelled = True

        tee_src = dedent(inspect.getsource(py_tee))
        tee_src = f"import sys\n{tee_src}\npy_tee(sys.stdin, sys.stdout, '{label}')"
        tee_cmd = (sys.executable, "-E", "-u", "-c", tee_src)
        tee_process = subprocess.Popen(tee_cmd, cwd=self.test_dir, stdin=ctest_process.stdout)
        try:
            comm = tee_process.communicate
            _ = comm(timeout=timeout)[0]
        except (TimeoutExpired, KeyboardInterrupt):
            print()
            print("aborted, partial result")
            ctest_process.kill()
            _ = ctest_process.communicate()
            # ctest lists to a temp file. Move it to the log
            tmp_name = f"{self.logfile}.tmp"
            if os.path.exists(tmp_name):
                if os.path.exists(self.logfile):
                    os.unlink(self.logfile)
                os.rename(tmp_name, self.logfile)
            self.partial = True
        else:
            self.partial = False
        finally:
            print("End of the test run")
            print()
        tee_process.wait()

    def run(self, label, rerun, timeout):
        cmd = self.ctestCommand, "--output-log", self.logfile
        if rerun is not None:
            # cmd += ("--rerun-failed",)
            # For some reason, this worked never in the script file.
            # We pass instead the test names as a regex:
            words = "^(" + "|".join(rerun) + ")$"
            cmd += ("--tests-regex", words)
        self._run(cmd, label, timeout)
