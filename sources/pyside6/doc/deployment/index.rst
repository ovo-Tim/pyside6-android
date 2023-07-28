.. _deployment-guides:

Deployment
==========

Deploying or freezing an application is an important part of a Python project,
this means to bundle all required resources so that the application finds everything it needs to
be able to run on a client's machine.
However, because most large projects aren't based on a single Python file, distributing these
applications can be a challenge.

Here are a few distribution options that you can use:
 1. Send a normal ZIP file with the application's content.
 2. Build a proper `Python package (wheel) <https://packaging.python.org/>`_.
 3. Freeze the application into a single binary file or directory.
 4. Provide native installer (msi, dmg)

If you are considering Option 3, then starting with 6.4, we ship a new tool called `pyside6-deploy`
that deploys your PySide6 application to all desktop platforms - Windows, Linux, and macOS. To know
more about how to use the tool see :ref:`pyside6-deploy`. Additionally, you can also use other
popular deployment tools shown below:

* `fbs`_
* `PyInstaller`_
* `cx_Freeze`_
* `py2exe`_
* `py2app`_
* `briefcase`_

.. _fbs: https://build-system.fman.io/
.. _PyInstaller: https://www.pyinstaller.org/
.. _cx_Freeze: https://marcelotduarte.github.io/cx_Freeze/
.. _py2exe: http://www.py2exe.org/
.. _py2app: https://py2app.readthedocs.io/en/latest/
.. _briefcase: https://briefcase.readthedocs.io

Although you can deploy PySide6 application using these tools, it is recommended to use
`pyside6-deploy` as it is easier to use and also to get the most optimized executable. Since
|project| is a cross-platform framework, we focus on solutions for the three major platforms that
Qt supports: Windows, Linux, and macOS.

The following table summarizes the platform support for those packaging tools:

.. raw:: html

    <table class="docutils align-default">
      <thead>
        <tr>
          <th class="head">Name</th>
          <th class="head">License</th>
          <th class="head">Qt 6</th>
          <th class="head">Qt 5</th>
          <th class="head">Linux</th>
          <th class="head">macOS</th>
          <th class="head">Windows</th>
        </tr>
      </thead>
      <tbody>
        <tr>
          <td><p>fbs</p></td>
          <td><p>GPL</p></td>
          <td></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
        </tr>
        <tr>
          <td><p>PyInstaller</p></td>
          <td><p>GPL</p></td>
          <td><p style="color: green;">partial</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
        </tr>
        <tr>
          <td><p>cx_Freeze</p></td>
          <td><p>MIT</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
        </tr>
        <tr>
          <td><p>py2exe</p></td>
          <td><p>MIT</p></td>
          <td><p style="color: green;">partial</p></td>
          <td><p style="color: green;">partial</p></td>
          <td><p style="color: red;">no</p></td>
          <td><p style="color: red;">no</p></td>
          <td><p style="color: green;">yes</p></td>
        </tr>
        <tr>
          <td><p>py2app</p></td>
          <td><p>MIT</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: red;">no</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: red;">no</p></td>
        </tr>
        <tr>
          <td><p>briefcase</p></td>
          <td><p>BSD3</p></td>
          <td><p style="color: green;">partial</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
        </tr>
        <tr>
          <td><p>Nuitka</p></td>
          <td><p>MIT</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
          <td><p style="color: green;">yes</p></td>
        </tr>
      </tbody>
    </table>

Notice that only *fbs*, *cx_Freeze*, *briefcase*, and *PyInstaller* meet our cross-platform requirement.

Since these are command-line tools, you'll need special hooks or scripts to handle resources
such as images, icons, and meta-information, before adding them to your package. Additionally,
these tools don't offer a mechanism to update your application packages.

To create update packages, use the `PyUpdater <https://www.pyupdater.org/>`_, which is a tool
built around PyInstaller.

The `fbs`_ tool offers a nice UI for the user to install the
application step-by-step.

.. note::

   Deployment is supported only from Qt for Python 5.12.2 and later.

Here's a set of tutorials on how to use these tools:

.. toctree::
    :name: mastertoc
    :maxdepth: 2

    deployment-pyside6-deploy.rst
    deployment-fbs.rst
    deployment-pyinstaller.rst
    deployment-cxfreeze.rst
    deployment-briefcase.rst
    deployment-py2exe.rst
    deployment-nuitka.rst
