# Copyright (C) 2019 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

import datetime
import os
import re
import subprocess
import sys
import time
import warnings
from argparse import ArgumentParser, RawTextHelpFormatter
from enum import Enum
from pathlib import Path
from typing import List

DESC = """
Utility script for working with Qt for Python.

Feel free to extend!

Typical Usage:
Update and build a repository: python qp5_tool -p -b

qp5_tool.py uses a configuration file "%CONFIGFILE%"
in the format key=value.

It is possible to use repository-specific values by adding a key postfixed by
a dash and the repository folder base name, eg:
Modules-pyside-setup512=Core,Gui,Widgets,Network,Test

Configuration keys:
Acceleration     Incredibuild or unset
BuildArguments   Arguments to setup.py
Generator        Generator to be used for CMake. Currently, only Ninja is
                 supported.
Jobs             Number of jobs to be run simultaneously
Modules          Comma separated list of modules to be built
                 (for --module-subset=)
Python           Python executable (Use python_d for debug builds on Windows)

Arbitrary keys can be defined and referenced by $(name):

MinimalModules=Core,Gui,Widgets,Network,Test
Modules=$(MinimalModules),Multimedia
Modules-pyside-setup-minimal=$(MinimalModules)
"""


class Acceleration(Enum):
    NONE = 0
    INCREDIBUILD = 1


class BuildMode(Enum):
    NONE = 0
    BUILD = 1
    RECONFIGURE = 2
    MAKE = 3


DEFAULT_BUILD_ARGS = ['--build-tests', '--skip-docs', '--quiet']
IS_WINDOWS = sys.platform == 'win32'
INCREDIBUILD_CONSOLE = 'BuildConsole' if IS_WINDOWS else '/opt/incredibuild/bin/ib_console'
# Config file keys
ACCELERATION_KEY = 'Acceleration'
BUILDARGUMENTS_KEY = 'BuildArguments'
GENERATOR_KEY = 'Generator'
JOBS_KEY = 'Jobs'
MODULES_KEY = 'Modules'
PYTHON_KEY = 'Python'

DEFAULT_MODULES = "Core,Gui,Widgets,Network,Test,Qml,Quick,Multimedia,MultimediaWidgets"
DEFAULT_CONFIG_FILE = f"Modules={DEFAULT_MODULES}\n"

build_mode = BuildMode.NONE
opt_dry_run = False
opt_verbose = False


def which(needle: str):
    """Perform a path search"""
    needles = [needle]
    if IS_WINDOWS:
        for ext in ("exe", "bat", "cmd"):
            needles.append(f"{needle}.{ext}")

    for path in os.environ.get("PATH", "").split(os.pathsep):
        for n in needles:
            binary = Path(path) / n
            if binary.is_file():
                return binary
    return None


def command_log_string(args: List[str], directory: Path):
    result = f'[{directory.name}]'
    for arg in args:
        result += f' "{arg}"' if ' ' in arg else f' {arg}'
    return result


def execute(args: List[str]):
    """Execute a command and print to log"""
    log_string = command_log_string(args, Path.cwd())
    print(log_string)
    if opt_dry_run:
        return
    exit_code = subprocess.call(args)
    if exit_code != 0:
        raise RuntimeError(f'FAIL({exit_code}): {log_string}')


def run_process_output(args):
    """Run a process and return its output. Also run in dry_run mode"""
    std_out = subprocess.Popen(args, universal_newlines=1,
                               stdout=subprocess.PIPE).stdout
    result = [line.rstrip() for line in std_out.readlines()]
    std_out.close()
    return result


def run_git(args):
    """Run git in the current directory and its submodules"""
    args.insert(0, git)  # run in repo
    execute(args)  # run for submodules


def expand_reference(cache_dict, value):
    """Expand references to other keys in config files $(name) by value."""
    pattern = re.compile(r"\$\([^)]+\)")
    while True:
        match = pattern.match(value)
        if not match:
            break
        key = match.group(0)[2:-1]
        value = value[:match.start(0)] + cache_dict[key] + value[match.end(0):]
    return value


def editor():
    editor = os.getenv('EDITOR')
    if not editor:
        return 'notepad' if IS_WINDOWS else 'vi'
    editor = editor.strip()
    if IS_WINDOWS:
        # Windows: git requires quotes in the variable
        if editor.startswith('"') and editor.endswith('"'):
            editor = editor[1:-1]
        editor = editor.replace('/', '\\')
    return editor


def edit_config_file():
    exit_code = -1
    try:
        exit_code = subprocess.call([editor(), config_file])
    except Exception as e:
        reason = str(e)
        print(f'Unable to launch: {editor()}: {reason}')
    return exit_code


"""
Config file handling, cache and read function
"""
config_dict = {}


def read_config_file(file_name):
    """Read the config file into config_dict, expanding continuation lines"""
    global config_dict
    keyPattern = re.compile(r'^\s*([A-Za-z0-9\_\-]+)\s*=\s*(.*)$')
    with open(file_name) as f:
        while True:
            line = f.readline()
            if not line:
                break
            line = line.rstrip()
            match = keyPattern.match(line)
            if match:
                key = match.group(1)
                value = match.group(2)
                while value.endswith('\\'):
                    value = value.rstrip('\\')
                    value += f.readline().rstrip()
                config_dict[key] = expand_reference(config_dict, value)


def read_config(key):
    """
    Read a value from the '$HOME/.qp5_tool' configuration file. When given
    a key 'key' for the repository directory '/foo/qt-5', check for the
    repo-specific value 'key-qt5' and then for the general 'key'.
    """
    if not config_dict:
        read_config_file(config_file)
    repo_value = config_dict.get(f"{key}-{base_dir}")
    return repo_value if repo_value else config_dict.get(key)


def read_bool_config(key):
    value = read_config(key)
    return value and value in ['1', 'true', 'True']


def read_int_config(key, default=-1):
    value = read_config(key)
    return int(value) if value else default


def read_acceleration_config():
    value = read_config(ACCELERATION_KEY)
    if value:
        value = value.lower()
        if value == 'incredibuild':
            return Acceleration.INCREDIBUILD
    return Acceleration.NONE


def read_config_build_arguments():
    value = read_config(BUILDARGUMENTS_KEY)
    if value:
        return re.split(r'\s+', value)
    return DEFAULT_BUILD_ARGS


def read_config_modules_argument():
    value = read_config(MODULES_KEY)
    if value and value != '' and value != 'all':
        return f"--module-subset={value}"
    return None


def read_config_python_binary() -> str:
    binary = read_config(PYTHON_KEY)
    virtual_env = os.environ.get('VIRTUAL_ENV')
    if not binary:
        # Use 'python3' unless virtualenv is set
        use_py3 = not virtual_env and which('python3')
        binary = 'python3' if use_py3 else 'python'
    binary = Path(binary)
    if not binary.is_absolute():
        abs_path = which(str(binary))
        if abs_path:
            binary = abs_path
        else:
            warnings.warn(f'Unable to find "{binary}"', RuntimeWarning)
    if virtual_env:
        if not str(binary).startswith(virtual_env):
            w = f'Python "{binary}" is not under VIRTUAL_ENV "{virtual_env}"'
            warnings.warn(w, RuntimeWarning)
    return str(binary)


def get_config_file(base_name) -> Path:
    global user
    home = os.getenv('HOME')
    if IS_WINDOWS:
        # Set a HOME variable on Windows such that scp. etc.
        # feel at home (locating .ssh).
        if not home:
            home = os.getenv('HOMEDRIVE') + os.getenv('HOMEPATH')
            os.environ['HOME'] = home
        user = os.getenv('USERNAME')
        config_file = Path(os.getenv('APPDATA')) / base_name
    else:
        user = os.getenv('USER')
        config_dir = Path(home) / '.config'
        if config_dir.exists():
            config_file = config_dir / base_name
        else:
            config_file = Path(home) / f".{base_name}"
    return config_file


def build(target: str):
    """Run configure and build steps"""
    start_time = time.time()

    arguments = []
    acceleration = read_acceleration_config()
    if not IS_WINDOWS and acceleration == Acceleration.INCREDIBUILD:
        arguments.append(INCREDIBUILD_CONSOLE)
        arguments.appendh('--avoid')  # caching, v0.96.74
    arguments.extend([read_config_python_binary(), 'setup.py', target])
    build_arguments = read_config_build_arguments()
    if opt_verbose and '--quiet' in build_arguments:
        build_arguments.remove('--quiet')
    arguments.extend(build_arguments)
    generator = read_config(GENERATOR_KEY)
    if generator != 'Ninja':
        arguments.extend(['--make-spec', 'ninja'])
    jobs = read_int_config(JOBS_KEY)
    if jobs > 1:
        arguments.extend(['-j', str(jobs)])
    if build_mode != BuildMode.BUILD:
        arguments.extend(['--reuse-build', '--ignore-git'])
        if build_mode != BuildMode.RECONFIGURE:
            arguments.append('--skip-cmake')
    modules = read_config_modules_argument()
    if modules:
        arguments.append(modules)
    if IS_WINDOWS and acceleration == Acceleration.INCREDIBUILD:
        arg_string = ' '.join(arguments)
        arguments = [INCREDIBUILD_CONSOLE, f'/command={arg_string}']

    execute(arguments)

    elapsed_time = int(time.time() - start_time)
    print(f'--- Done({elapsed_time}s) ---')


def run_tests():
    """Run tests redirected into a log file with a time stamp"""
    logfile_name = datetime.datetime.today().strftime("test_%Y%m%d_%H%M.txt")
    binary = sys.executable
    command = f'"{binary}" testrunner.py test > {logfile_name}'
    print(command_log_string([command], Path.cwd()))
    start_time = time.time()
    result = 0 if opt_dry_run else os.system(command)
    elapsed_time = int(time.time() - start_time)
    print(f'--- Done({elapsed_time}s) ---')
    return result


def create_argument_parser(desc):
    parser = ArgumentParser(description=desc, formatter_class=RawTextHelpFormatter)
    parser.add_argument('--dry-run', '-d', action='store_true',
                        help='Dry run, print commands')
    parser.add_argument('--edit', '-e', action='store_true',
                        help='Edit config file')
    parser.add_argument('--reset', '-r', action='store_true',
                        help='Git reset hard to upstream state')
    parser.add_argument('--clean', '-c', action='store_true',
                        help='Git clean')
    parser.add_argument('--pull', '-p', action='store_true',
                        help='Git pull')
    parser.add_argument('--build', '-b', action='store_true',
                        help='Build (configure + build)')
    parser.add_argument('--make', '-m', action='store_true', help='Make')
    parser.add_argument('--no-install', '-n', action='store_true',
                        help='Run --build only, do not install')
    parser.add_argument('--Make', '-M', action='store_true',
                        help='cmake + Make (continue broken build)')
    parser.add_argument('--test', '-t', action='store_true',
                        help='Run tests')
    parser.add_argument('--version', '-v', action='version', version='%(prog)s 1.0')
    parser.add_argument('--verbose', '-V', action='store_true',
                        help='Turn off --quiet specified in build arguments')
    return parser


if __name__ == '__main__':
    git = None
    base_dir = None
    config_file = None
    user = None

    config_file = get_config_file('qp5_tool.conf')
    argument_parser = create_argument_parser(DESC.replace('%CONFIGFILE%', str(config_file)))
    options = argument_parser.parse_args()
    opt_dry_run = options.dry_run
    opt_verbose = options.verbose

    if options.edit:
        sys.exit(edit_config_file())

    if options.build:
        build_mode = BuildMode.BUILD
    elif options.make:
        build_mode = BuildMode.MAKE
    elif options.Make:
        build_mode = BuildMode.RECONFIGURE

    if build_mode == BuildMode.NONE and not (options.clean or options.reset
                                             or options.pull or options.test):
        argument_parser.print_help()
        sys.exit(0)

    git = 'git'
    if which(git) is None:
        warnings.warn('Unable to find git', RuntimeWarning)
        sys.exit(-1)

    if not config_file.exists():
        print('Create initial config file ', config_file, " ..")
        with open(config_file, 'w') as f:
            f.write(DEFAULT_CONFIG_FILE.format(' '.join(DEFAULT_BUILD_ARGS)))

    while not Path(".git").exists():
        cwd = Path.cwd()
        cwd_s = os.fspath(cwd)
        if cwd_s == '/' or (IS_WINDOWS and len(cwd_s) < 4):
            warnings.warn('Unable to find git root', RuntimeWarning)
            sys.exit(-1)
        os.chdir(cwd.parent)

    base_dir = Path.cwd().name

    if options.clean:
        run_git(['clean', '-dxf'])

    if options.reset:
        run_git(['reset', '--hard', '@{upstream}'])

    if options.pull:
        run_git(['pull', '--rebase'])

    if build_mode != BuildMode.NONE:
        target = 'build' if options.no_install else 'install'
        build(target)

    if options.test:
        sys.exit(run_tests())

    sys.exit(0)
