# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

from pythonforandroid.recipe import PythonRecipe
from pythonforandroid.logger import info
import zipfile
import shutil
from pathlib import Path


class PySideRecipe(PythonRecipe):
    version = '{{ version }}'
    wheel_path = '{{ wheel_path }}'
    depends = ["shiboken6"]
    call_hostpython_via_targetpython = False
    install_in_hostpython = False

    def build_arch(self, arch):
        """Unzip the wheel and copy into site-packages of target"""

        info("Installing {} into site-packages".format(self.name))
        with zipfile.ZipFile(self.wheel_path, "r") as zip_ref:
            info("Unzip wheels and copy into {}".format(self.ctx.get_python_install_dir(arch.arch)))
            zip_ref.extractall(self.ctx.get_python_install_dir(arch.arch))

        lib_dir = Path(f"{self.ctx.get_python_install_dir(arch.arch)}/PySide6/Qt/lib")
        info("Copying Qt libraries to be loaded on startup")
        shutil.copytree(lib_dir, self.ctx.get_libs_dir(arch.arch), dirs_exist_ok=True)

        info("Copying libc++_shared.so from SDK to be loaded on startup")
        libcpp_path = f"{self.ctx.ndk.sysroot_lib_dir}/{arch.command_prefix}/libc++_shared.so"
        shutil.copyfile(libcpp_path, Path(self.ctx.get_libs_dir(arch.arch)) / "libc++_shared.so")

        info("Copying Qt platform plugin to be loaded on startup from SDK to be loaded on startup")
        shutil.copyfile(
            Path(self.ctx.get_python_install_dir(arch.arch))
            / "PySide6" / "Qt" / "plugins" / "platforms"
            / f"libplugins_platforms_qtforandroid_{arch.arch}.so",
            Path(self.ctx.get_libs_dir(arch.arch)) / f"libplugins_platforms_qtforandroid_{arch.arch}.so",
        )


recipe = PySideRecipe()
