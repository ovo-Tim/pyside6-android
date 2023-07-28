# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

"""
embedding_generator.py

This file takes the content of the two supported directories and inserts
it into a zip file. The zip file is then converted into a C++ source
file that can easily be unpacked again with Python (see signature.cpp,
constant 'PySide_PythonCode').

Note that this _is_ a zipfile, but since it is embedded into the shiboken
binary, we cannot use the zipimport module from Python.
But a similar solution is possible that allows for normal imports.

See signature_bootstrap.py for details.
"""

import sys
import os
import subprocess
import textwrap
import tempfile
import argparse
import marshal
import traceback
from pathlib import Path

# work_dir is set to the source for testing, only.
# It can be overridden in the command line.
work_dir = Path(__file__).parent.resolve()
embed_dir = work_dir
cur_dir = Path.cwd()
source_dir = work_dir.parents[2]
assert source_dir.name == "sources"
build_script_dir = work_dir.parents[3]
assert (build_script_dir / "build_scripts").exists()

sys.path.insert(0, os.fspath(build_script_dir))

from build_scripts import utils


def runpy(cmd, **kw):
    subprocess.call([sys.executable, '-E'] + cmd.split(), **kw)


def create_zipfile(use_pyc, quiet):
    """
    Collect all Python files, compile them, create a zip file
    and make a chunked base64 encoded file from it.
    """
    zip_name = "signature.zip"
    inc_name = "signature_inc.h"
    flag = '-b'
    os.chdir(work_dir)

    # Remove all left-over py[co] and other files first, in case we use '--reuse-build'.
    # Note that we could improve that with the PyZipfile function to use .pyc files
    # in different folders, but that makes only sense when COIN allows us to have
    # multiple Python versions in parallel.
    for root, dirs, files in os.walk(work_dir):
        for name in files:
            fpath = Path(root) / name
            ew = name.endswith
            if ew(".pyc") or ew(".pyo") or ew(".zip") or ew(".inc"):
                os.remove(fpath)
    # We copy every Python file into this dir, but only for the right version.
    # For testing in the source dir, we need to filter.
    ignore = []
    utils.copydir(source_dir / "shiboken6" / "shibokenmodule" / "files.dir" / "shibokensupport",
                  work_dir / "shibokensupport",
                  ignore=ignore, file_filter_function=lambda name, n2: name.endswith(".py"))
    if embed_dir != work_dir:
        utils.copyfile(embed_dir / "signature_bootstrap.py", work_dir)

    if not use_pyc:
        pass   # We cannot compile, unless we have folders per Python version
    else:
        files = ' '.join(fn for fn in os.listdir('.'))
        runpy(f'-m compileall -q {flag} {files}')
    files = ' '.join(fn for fn in os.listdir('.') if not fn == zip_name)
    runpy(f'-m zipfile -c {zip_name} {files}')
    tmp = tempfile.TemporaryFile(mode="w+")
    runpy(f'-m base64 {zip_name}', stdout=tmp)
    # now generate the include file
    tmp.seek(0)
    with open(inc_name, "w") as inc:
        _embed_file(tmp, inc)
    tmp.close()

    # also generate a simple embeddable .pyc file for signature_bootstrap.pyc
    boot_name = "signature_bootstrap.py" if not use_pyc else "signature_bootstrap.pyc"
    with open(boot_name, "rb") as ldr, open("signature_bootstrap_inc.h", "w") as inc:
        _embed_bytefile(ldr, inc, not use_pyc)
    os.chdir(cur_dir)
    if quiet:
        return

    # have a look at our populated folder unless quiet option
    def tree(directory):
        print(f'+ {directory}')
        for path in sorted(directory.rglob('*')):
            depth = len(path.relative_to(directory).parts)
            spacer = '    ' * depth
            print(f'{spacer}+ {path.name}')

    print("++++ Current contents of")
    tree(work_dir)
    print("++++")


def _embed_file(fin, fout):
    """
    Format a text file for embedding in a C++ source file.
    """
    # MSVC has a 64k string limitation. In C, it would be easy to create an
    # array of 64 byte strings and use them as one big array. In C++ this does
    # not work, since C++ insists in having the terminating nullbyte.
    # Therefore, we split the string after an arbitrary number of lines
    # (chunked file).
    limit = 50
    text = fin.readlines()
    print(textwrap.dedent("""
         // This is a ZIP archive of all Python files in the directory
         //         "shiboken6/shibokenmodule/files.dir/shibokensupport/signature"
         // There is also a toplevel file "signature_bootstrap.py[c]" that will be
         // directly executed from C++ as a bootstrap loader.
         """).strip(), file=fout)
    block, blocks = 0, len(text) // limit + 1
    for idx, line in enumerate(text):
        if idx % limit == 0:
            if block:
                fout.write(')"\n')
            comma = "," if block else ""
            block += 1
            fout.write(f'\n{comma} // Block {block} of {blocks}\nR"(')
        else:
            fout.write('\n')
        fout.write(line.strip())
    fout.write(')"\n\n/* Sentinel */, ""\n')


def _embed_bytefile(fin, fout, is_text):
    """
    Format a binary file for embedding in a C++ source file.
    This version works directly with a single .pyc file.
    """
    fname = fin.name
    remark = ("No .pyc file because '--LIMITED-API=yes'" if is_text else
              "The .pyc header is stripped away")
    print(textwrap.dedent(f"""
        /*
         * This is the file "{fname}" as a simple byte array.
         * It can be directly embedded without any further processing.
         * {remark}.
         */
         """), file=fout)
    headsize = ( 0 if is_text else
                16 if sys.version_info >= (3, 7) else 12 if sys.version_info >= (3, 3) else 8)
    binstr = fin.read()[headsize:]
    if is_text:
        try:
            compile(binstr, fin.name, "exec")
        except SyntaxError as e:
            print(e)
            traceback.print_exc(file=sys.stdout)
            print(textwrap.dedent(f"""
                *************************************************************************
                ***
                *** Could not compile the boot loader '{fname}'!
                ***
                *************************************************************************
                """))
            raise SystemError
    else:
        try:
            marshal.loads(binstr)
        except ValueError as e:
            print(e)
            traceback.print_exc(file=sys.stdout)
            version = sys.version_info[:3]
            print(textwrap.dedent(f"""
                *************************************************************************
                ***
                *** This Python version {version} seems to have a new .pyc header size.
                *** Please correct the 'headsize' constant ({headsize}).
                ***
                *************************************************************************
                """))
            raise SystemError

    print(file=fout)
    use_ord = sys.version_info[0] == 2
    for i in range(0, len(binstr), 16):
        for c in bytes(binstr[i : i + 16]):
            ord_c = ord(c) if use_ord else c
            print(f"{ord_c:#4},", file=fout, end="")
        print(file=fout)
    print("/* End Of File */", file=fout)


def str2bool(v):
    if v.lower() in ('yes', 'true', 't', 'y', '1'):
        return True
    elif v.lower() in ('no', 'false', 'f', 'n', '0'):
        return False
    else:
        raise argparse.ArgumentTypeError('Boolean value expected.')


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--cmake-dir', nargs="?")
    parser.add_argument('--use-pyc', type=str2bool)
    parser.add_argument('--quiet', action='store_true')
    args = parser.parse_args()
    if args.cmake_dir:
        work_dir = Path(args.cmake_dir).resolve()
    create_zipfile(args.use_pyc, args.quiet)
