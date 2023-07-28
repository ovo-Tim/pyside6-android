Getting Started on macOS
========================

Requirements
------------

* `XCode`_ 8.2 (macOS 10.11), 8.3.3 (macOS 10.12), 9 (macOS 10.13), 10.1 (macOS 10.14)
* ``sphinx`` package for the documentation (optional).
* Depending on your OS, the following dependencies might also be required:

  * ``libgl-dev``, ``python-dev``, ``python-distutils``, and ``python-setuptools``.

* Check the platform dependencies of `Qt for macOS`_.

.. _XCode: https://developer.apple.com/xcode/
.. _`Qt for macOS`: https://doc.qt.io/qt-6/macos.html

Building from source
--------------------

Creating a virtual environment
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``venv`` module allows you to create a local, user-writeable copy of a python environment into
which arbitrary modules can be installed and which can be removed after use::

    python -m venv testenv  # your interpreter could be called 'python3'
    source testenv/bin/activate

will create and use a new virtual environment, which is indicated by the command prompt changing.

Setting up CLANG
~~~~~~~~~~~~~~~~

If you don't have libclang already in your system, you can download from the Qt servers::

    wget https://download.qt.io/development_releases/prebuilt/libclang/libclang-release_140-based-macos-universal.7z

Extract the files, and leave it on any desired path, and set the environment
variable required::

    7z x libclang-release_140-based-macos-universal.7z
    export LLVM_INSTALL_DIR=$PWD/libclang

Getting PySide
~~~~~~~~~~~~~~

Cloning the official repository can be done by::

    git clone https://code.qt.io/pyside/pyside-setup

Checking out the version that we want to build, for example, 6.5::

    cd pyside-setup && git checkout 6.5

Install the general dependencies::

    pip install -r requirements.txt

.. note:: Keep in mind you need to use the same version as your Qt installation

Building PySide
~~~~~~~~~~~~~~~

Check your Qt installation path, to specifically use that version of qtpaths to build PySide.
for example, ``/opt/Qt/6.5.0/gcc_64/bin/qtpaths``.

Build can take a few minutes, so it is recommended to use more than one CPU core::

    python setup.py build --qtpaths=/opt/Qt/6.5.0/gcc_64/bin/qtpaths --build-tests --ignore-git --parallel=8

Installing PySide
~~~~~~~~~~~~~~~~~

To install on the current directory, just run::

    python setup.py install --qtpaths=/opt/Qt/6.5.0/gcc_64/bin/qtpaths --build-tests --ignore-git --parallel=8

Test installation
~~~~~~~~~~~~~~~~~

You can execute one of the examples to verify the process is properly working.
Remember to properly set the environment variables for Qt and PySide::

    python examples/widgets/widgets/tetrix.py
