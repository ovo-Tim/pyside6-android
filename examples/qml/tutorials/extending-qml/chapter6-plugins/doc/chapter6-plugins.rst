.. _qml-chapter6-plugins-example:

Extending QML - Plugins Example
===============================

This is the last of a series of 6 examples forming a tutorial
about extending QML with Python.

This example refers to the Python version of using a QML plugin in Python. The
idea of plugins in Python is non-existent because Python modules are
dynamically loaded anyway. We use this idea and our QML type registration
decorators - ``QmlELement``/``QmlNamedElement`` - to register the QML modules as they
are imported. The ``pyside6-qml`` tool does this for you by simply pointing to the
``.qml`` file.

.. image:: plugins.png
   :width: 400
   :alt: Plugins Example


Running the Example
-------------------

.. code-block:: shell

    pyside6-qml examples/qml/tutorials/extending-qml/chapter6-plugins/app.qml -I examples/qml/tutorials/extending-qml/chapter6-plugins/Charts
