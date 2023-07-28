# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

"""
testrunner
==========

Provide an interface to the pyside tests.
-----------------------------------------

This program can only be run if PySide was built with tests enabled.
All tests are run in a single pass, and if not blacklisted, an error
is raised at the end of the run.

Recommended build process:
There is no need to install the project.
Building the project with something like

    python setup.py build --build-tests --qmake=<qmakepath> --ignore-git --debug

is sufficient. The tests are run by changing into the latest build dir and there
into pyside6, then 'make test'.


New testing policy:
-------------------

The tests are now run 5 times, and errors are reported
when they appear at least 3 times. With the variable COIN_RERUN_FAILED_ONLY it is
possible to configure if all tests should be rerun or the failed ones, only.

The full mode can be tested locally by setting

    export COIN_RERUN_FAILED_ONLY=0
"""

import argparse
import os
import sys
from collections import OrderedDict
from textwrap import dedent
from timeit import default_timer as timer

from .blacklist import BlackList
from .buildlog import builds
from .helper import decorate, script_dir
from .parser import TestParser
from .runner import TestRunner

# Should we repeat only failed tests?
COIN_RERUN_FAILED_ONLY = True
COIN_THRESHOLD = 3  # report error if >=
COIN_TESTING = 5  # number of runs
TIMEOUT = 20 * 60

if os.environ.get("COIN_RERUN_FAILED_ONLY", "1").lower() in "0 f false n no".split():
    COIN_RERUN_FAILED_ONLY = False


def test_project(project, args, blacklist, runs):
    ret = []

    if "pypy" in builds.classifiers:
        # As long as PyPy has so many bugs, we use 1 test only...
        global COIN_TESTING
        COIN_TESTING = runs = 1
        # ...and extend the timeout.
        global TIMEOUT
        TIMEOUT = 100 * 60

    # remove files from a former run
    for idx in range(runs):
        index = idx + 1
        runner = TestRunner(builds.selected, project, index)
        if os.path.exists(runner.logfile) and not args.skip:
            os.unlink(runner.logfile)
    # now start the real run
    rerun_list = None
    for idx in range(runs):
        index = idx + 1
        runner = TestRunner(builds.selected, project, index)
        print()
        print(f"********* Start testing of {project} *********")
        print("Config: Using", " ".join(builds.classifiers))
        print()
        if os.path.exists(runner.logfile) and args.skip:
            print("Parsing existing log file:", runner.logfile)
        else:
            if index > 1 and COIN_RERUN_FAILED_ONLY:
                rerun = rerun_list
                if not rerun:
                    print(f"--- no re-runs found, stopping before test {index} ---")
                    break
            else:
                rerun = None
            runner.run(f"RUN {idx + 1}:", rerun, TIMEOUT)
        results = TestParser(runner.logfile)
        r = 5 * [0]
        rerun_list = []
        print()
        fatal = False
        for item in results.iter_blacklist(blacklist):
            res = item.rich_result
            sharp = f"#{item.sharp}"
            mod_name = decorate(item.mod_name)
            print(f"RES {index}: Test {sharp:>4}: {res:<6} {mod_name}()")
            r[0] += 1 if res == "PASS" else 0
            r[1] += 1 if res == "FAIL!" else 0
            r[2] += 1 if res == "SKIPPED" else 0  # not yet supported
            r[3] += 1 if res == "BFAIL" else 0
            r[4] += 1 if res == "BPASS" else 0
            if res not in ("PASS", "BPASS"):
                rerun_list.append(item.mod_name)
            # PYSIDE-1229: When a fatal error happens, bail out immediately!
            if item.fatal:
                fatal = item
        print()
        print(
            f"Totals: {sum(r)} tests. "
            f"{r[0]} passed, {r[1]} failed, {r[2]} skipped, {r[3]} blacklisted, {r[4]} bpassed."
        )
        print()
        print(f"********* Finished testing of {project} *********")
        print()
        ret.append(r)
        if fatal:
            print("FATAL ERROR:", fatal)
            print("Repetitions cancelled!")
            break
    return ret, fatal, runs


def main():
    # create the top-level command parser
    start_time = timer()
    all_projects = "shiboken6 pyside6".split()
    tested_projects = "shiboken6 pyside6".split()
    tested_projects_quoted = " ".join("'i'" for i in tested_projects)
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=dedent(
            """\
        Run the tests for some projects, default = {tested_projects_quoted}.

        Testing is now repeated up to {COIN_TESTING} times, and errors are
        only reported if they occur {COIN_THRESHOLD} or more times.
        The environment variable COIN_RERUN_FAILED_ONLY controls if errors
        are only repeated if there are errors. The default is "1".
        """
        ),
    )
    subparsers = parser.add_subparsers(dest="subparser_name")

    # create the parser for the "test" command
    parser_test = subparsers.add_parser("test")
    group = parser_test.add_mutually_exclusive_group(required=False)
    blacklist_default = os.path.join(script_dir, "build_history", "blacklist.txt")
    group.add_argument(
        "--blacklist",
        "-b",
        type=argparse.FileType("r"),
        default=blacklist_default,
        help=f"a Qt blacklist file (default: {blacklist_default})",
    )
    parser_test.add_argument(
        "--skip", action="store_true", help="skip the tests if they were run before"
    )
    parser_test.add_argument(
        "--environ", nargs="+", help="use name=value ... to set environment variables"
    )
    parser_test.add_argument(
        "--buildno",
        default=-1,
        type=int,
        help="use build number n (0-based), latest = -1 (default)",
    )
    parser_test.add_argument(
        "--projects",
        nargs="+",
        type=str,
        default=tested_projects,
        choices=all_projects,
        help=f"use {tested_projects_quoted} (default) or other projects",
    )
    parser_getcwd = subparsers.add_parser("getcwd")
    parser_getcwd.add_argument(
        "filename", type=argparse.FileType("w"), help="write the build dir name into a file"
    )
    parser_getcwd.add_argument(
        "--buildno",
        default=-1,
        type=int,
        help="use build number n (0-based), latest = -1 (default)",
    )
    args = parser.parse_args()

    if hasattr(args, "buildno"):
        try:
            builds.set_buildno(args.buildno)
        except IndexError:
            print(f"history out of range. Try '{__file__} list'")
            sys.exit(1)

    if args.subparser_name == "getcwd":
        print(builds.selected.build_dir, file=args.filename)
        print(builds.selected.build_dir, "written to file", args.filename.name)
        sys.exit(0)
    elif args.subparser_name == "test":
        pass  # we do it afterwards
    elif args.subparser_name == "list":
        rp = os.path.relpath
        print()
        print("History")
        print("-------")
        for idx, build in enumerate(builds.history):
            print(idx, rp(build.log_dir), rp(build.build_dir))
        print()
        print("Note: only the last history entry of a folder is valid!")
        sys.exit(0)
    else:
        parser.print_help()
        sys.exit(1)

    if args.blacklist:
        args.blacklist.close()
        bl = BlackList(args.blacklist.name)
    else:
        bl = BlackList(None)
    if args.environ:
        for line in args.environ:
            things = line.split("=")
            if len(things) != 2:
                raise ValueError("you need to pass one or more name=value pairs.")
            key, value = things
            os.environ[key] = value

    version = sys.version.replace("\n", " ")
    print(
        dedent(
            f"""\
        System:
          Platform={sys.platform}
          Executable={sys.executable}
          Version={version}
          API version={sys.api_version}

        Environment:"""
        )
    )
    for key, value in sorted(os.environ.items()):
        print(f"  {key}={value}")
    print()

    q = 5 * [0]

    runs = COIN_TESTING
    fail_crit = COIN_THRESHOLD
    # now loop over the projects and accumulate
    fatal = False
    for project in args.projects:
        res, fatal, runs = test_project(project, args, bl, runs)
        if fatal:
            runs = 1
        for idx, r in enumerate(res):
            q = list(map(lambda x, y: x + y, r, q))

    if len(args.projects) > 1:
        print(
            f"All above projects: {sum(q)} tests. "
            f"{q[0]} passed, {q[1]} failed, {q[2]} skipped, {q[3]} blacklisted, {q[4]} bpassed."
        )
        print()

    tot_res = OrderedDict()
    for project in args.projects:
        for idx in range(runs):
            index = idx + 1
            runner = TestRunner(builds.selected, project, index)
            results = TestParser(runner.logfile)
            for item in results.iter_blacklist(bl):
                key = f"{project}:{item.mod_name}"
                tot_res.setdefault(key, [])
                tot_res[key].append(item.rich_result)
    tot_flaky = 0
    print("*" * 79)
    print("**")
    print("*   Summary Of All Tests")
    print("*")
    empty = True
    for test, res in tot_res.items():
        pass__c = res.count("PASS")
        bpass_c = res.count("BPASS")
        fail__c = res.count("FAIL!")
        bfail_c = res.count("BFAIL")
        fail2_c = fail__c + bfail_c
        fatal_c = res.count("FATAL")
        if pass__c == len(res):
            continue
        elif bpass_c >= runs and runs > 1:
            msg = "Remove blacklisting; test passes"
        elif fail__c >= runs:
            msg = "Newly detected Real test failure!"
        elif bfail_c >= runs:
            msg = "Keep blacklisting ;-("
        elif fail2_c > 0 and fail2_c < len(res):
            msg = "Flaky test"
            tot_flaky += 1
        elif fatal_c:
            msg = "FATAL format error, repetitions aborted!"
        else:
            continue
        empty = False
        padding = 6 * runs
        txt = " ".join((f"{piece:<5}" for piece in res))
        txt = (f"{txt}{padding * ' '}")[:padding]
        testpad = 36
        if len(test) < testpad:
            test += (testpad - len(test)) * " "
        print(txt, decorate(test), msg)
    if empty:
        print("*   (empty)")
    print("*")
    print("**")
    print("*" * 79)
    print()
    if runs > 1:
        print(f"Total flaky tests: errors but not always = {tot_flaky}")
        print()
    else:
        print("For info about flaky tests, we need to perform more than one run.")
        print("Please activate the COIN mode:    'export QTEST_ENVIRONMENT=ci'")
        print()
    # nag us about unsupported projects
    ap, tp = set(all_projects), set(tested_projects)
    if ap != tp:
        print("+++++ Note: please support", " ".join(ap - tp), "+++++")
        print()

    stop_time = timer()
    used_time = stop_time - start_time
    # Now create an error if the criterion is met:
    try:
        if fatal:
            raise ValueError(f"FATAL format error: {fatal}")
        err_crit = f"'FAIL! >= {fail_crit}'"
        fail_count = 0
        for res in tot_res.values():
            if res.count("FAIL!") >= fail_crit:
                fail_count += 1
        if fail_count == 1:
            raise ValueError(f"A test was not blacklisted and met the criterion {err_crit}")
        elif fail_count > 1:
            raise ValueError(
                f"{fail_count} failures were not blacklisted " f"and met the criterion {err_crit}"
            )
        print(f"No test met the error criterion {err_crit}")
    finally:
        print()
        print(f"Total time of whole Python script = {used_time:0.2f} sec")
        print()


# eof
