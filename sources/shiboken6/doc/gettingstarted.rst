Getting started
===============

Building from source
--------------------

This step is focused on building Shiboken from source, both the Generator and Python module.
Please notice that these are built when you are building PySide from source too, so there is no
need to continue if you already have a built PySide.

General Requirements
^^^^^^^^^^^^^^^^^^^^

* **Python**: 3.7+
* **Qt:** 6.0+
* **libclang:** The libclang library, recommended: version 10 for 6.0+.
  Prebuilt versions of it can be `downloaded here`_.
* **CMake:** 3.1+ is needed.

.. _downloaded here: https://download.qt.io/development_releases/prebuilt/libclang/

Simple build
^^^^^^^^^^^^

If you need only Shiboken Generator, a simple build run would look like this::

    # For the required libraries (this will also build the shiboken6 python module)
    python setup.py install --qtpaths=/path/to/qtpaths \
                            --build-tests \
                            --verbose-build \
                            --internal-build-type=shiboken6

    # For the executable
    python setup.py install --qtpaths=/path/to/qtpaths \
                            --build-tests \
                            --verbose-build \
                            --internal-build-type=shiboken6-generator

The same can be used for the module, changing the value of ``internal-build-type`` to
``shiboken6-module``.

.. warning:: If you are planning to use PySide too, for examples like
    'scriptableapplication' you need to have build it as well.  The main issue is
    that your PySide and Shiboken needs to be build using the same dependencies
    from Qt and libclang.

Using the wheels
----------------

Installing ``pyside6`` or ``shiboken6`` from pip **does not** install ``shiboken6_generator``,
because the wheels are not on PyPi.

You can get the ``shiboken6_generator`` wheels from Qt servers, and you can still install it
via ``pip``::

    pip install \
        --index-url=https://download.qt.io/official_releases/QtForPython/ \
        --trusted-host download.qt.io \
        shiboken6 pyside6 shiboken6_generator


The ``whl`` package cannot automatically discover in your system the location for:

* Clang installation,
* Qt location (indicated by the path of the ``qtpaths`` tool) with the same
  version/build as the one described in the wheel,
* Qt libraries with the same package version.

So using this process requires you to manually modify the variables:

* ``CLANG_INSTALL_DIR`` must be set to where the libraries are,
* ``PATH`` must include the location for the ``qtpaths`` tool with the same Qt
    version as the package,
* ``LD_LIBRARY_PATH`` including the Qt libraries and Clang libraries paths.
