Python-QML integration
======================

This tutorial provides a quick walk-through of a python application that loads, and interacts with
a QML file.  QML is a declarative language that lets you design UIs faster than a traditional
language, such as C++.  The QtQml and QtQuick modules provides the necessary infrastructure for
QML-based UIs.

In this tutorial, you will learn how to integrate Python with a QML application.
This mechanism will help us to understand how to use Python as a backend for certain
signals from the UI elements in the QML interface.  Additionally, you will learn how to provide
a modern look to your QML application using one of the features from Qt Quick Controls 2.

The tutorial is based on an application that allow you to set many text properties, like increasing
the font size, changing the color, changing the style, and so on.  Before you begin, install the
`PySide6 <https://pypi.org/project/PySide6/>`_ Python packages.

The following step-by-step process will guide you through the key elements of the QML based
application and PySide6 integration:

#. First, let's start with the following QML-based UI:

   .. image:: textproperties_default.png

   The design is based on a `GridLayout`, containing two `ColumnLayout`.
   Inside the UI you will find many `RadioButton`, `Button`, and a `Slider`.

#. With the QML file in place, you can load it from Python:

   .. literalinclude:: main.py
      :linenos:
      :lines: 63-76
      :emphasize-lines: 4,9

   Notice that we only need a :code:`QQmlApplicationEngine` to
   :code:`load` the QML file.

#. Define the ``Bridge`` class, containing all the logic for the element
   that will be register in QML:

   .. literalinclude:: main.py
      :linenos:
      :lines: 14-54
      :emphasize-lines: 3,4,7

   Notice that the registration happens thanks to the :code:`QmlElement`
   decorator, that underneath uses the reference to the :code:`Bridge`
   class and the variables :code:`QML_IMPORT_NAME` and
   :code:`QML_IMPORT_MAJOR_VERSION`.

#. Now, go back to the QML file and connect the signals to the slots defined in the ``Bridge`` class:

   .. code:: js

      Bridge {
         id: bridge
      }

   Inside the :code:`ApplicationWindow` we declare a component
   with the same name as the Python class, and provide an :code:`id:`.
   This :code:`id` will help you to get a reference to the element
   that was registered from Python.

   .. literalinclude:: view.qml
      :linenos:
      :lines: 45-55
      :emphasize-lines: 6-8

   The properties *Italic*, *Bold*, and *Underline* are mutually
   exclusive, this means only one can be active at any time.
   To achieve  this each time we select one of these options, we
   check the three properties via the QML element property as you can
   see in the above snippet.
   Only one of the three will return *True*, while the other two
   will return *False*, that is how we make sure only one is being
   applied to the text.

#. Each slot verifies if the selected option contains the text associated
   to the property:

   .. literalinclude:: main.py
      :linenos:
      :lines: 42-47
      :emphasize-lines: 4,6

   Returning *True* or *False* allows you to activate and deactivate
   the properties of the QML UI elements.

   It is also possible to return other values that are not *Boolean*,
   like the slot in charge of returning the font size:

   .. literalinclude:: main.py
      :linenos:
      :lines: 34-39

#. Now, for changing the look of our application, you have two options:

   1. Use the command line: execute the python file adding the option, ``--style``::

       python main.py --style material

   2. Use a ``qtquickcontrols2.conf`` file:

      .. literalinclude:: qtquickcontrols2.conf
         :linenos:

      Then add it to your ``.qrc`` file:

      .. literalinclude:: style.qrc
         :linenos:

      Generate the *rc* file running, ``pyside6-rcc style.qrc -o style_rc.py``
      And finally import it from your ``main.py`` script.

   .. literalinclude:: main.py
      :linenos:
      :lines: 4-12
      :emphasize-lines: 9

   You can read more about this configuration file
   `here <https://doc.qt.io/qt-5/qtquickcontrols2-configuration.html>`_.

   The final look of your application will be:

   .. image:: textproperties_material.png

You can :download:`view.qml <view.qml>` and
:download:`main.py <main.py>` to try this example.
