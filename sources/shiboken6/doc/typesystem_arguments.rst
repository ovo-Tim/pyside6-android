.. _modifying-arguments:

Modifying Arguments
-------------------

.. _conversionrule-on-arguments:

conversion-rule
^^^^^^^^^^^^^^^

The ``conversion-rule`` node allows you to write customized code to convert
the given argument between the target language and C++.
It is then a child of the :ref:`modify-argument` node:

.. code-block:: xml

    <modify-argument index="2">
    <!-- for the second argument of the function -->
    <conversion-rule class="target | native">
        // the code
    </conversion-rule>
    </modify-argument>

The ``class`` attribute accepts one of the following values to define the
conversion direction to be either ``target-to-native`` or ``native-to-target``:

* ``native``: Defines the conversion direction to be ``target-to-native``.
              It is similar to the existing ``<target-to-native>`` element.
              See :ref:`Conversion Rule Tag <conversion-rule-tag>` for more information.

* ``target``: Defines the conversion direction to be ``native-to-target``.
              It is similar to the existing ``<native-to-target>`` element.
              See :ref:`Conversion Rule Tag <conversion-rule-tag>` for more information.

This node is typically used in combination with the :ref:`replace-type` and
:ref:`remove-argument` nodes. The given code is used instead of the generator's
conversion code.

Writing %N in the code (where N is a number), will insert the name of the
nth argument. Alternatively, %in and %out which will be replaced with the
name of the conversion's input and output variable, respectively. Note the
output variable must be declared explicitly, for example:

.. code-block:: xml

    <conversion-rule class="native">
    bool %out = (bool) %in;
    </conversion-rule>

.. note::

    You can also use the ``conversion-rule`` node to specify
    :ref:`a conversion code which will be used instead of the generator's conversion code everywhere for a given type <conversion-rule-tag>`.

.. _remove-argument:

remove-argument
^^^^^^^^^^^^^^^

The ``remove-argument`` node removes the given argument from the function's
signature, and it is a child of the :ref:`modify-argument` node.

.. code-block:: xml

 <modify-argument>
     <remove-argument />
 </modify-argument>

.. _rename-to:

rename to
^^^^^^^^^

The ``rename to`` node is used to rename a argument and use this new name in
the generated code, and it is a child of the :ref:`modify-argument` node.

.. code-block:: xml

 <modify-argument>
     <rename to='...' />
 </modify-argument>

.. warning:: This tag is deprecated, use the ``rename`` attribute from :ref:`modify-argument` tag instead.

.. _remove-default-expression:

remove-default-expression
^^^^^^^^^^^^^^^^^^^^^^^^^

The ``remove-default-expression`` node disables the use of the default expression
for the given argument, and it is a child of the :ref:`modify-argument` node.

.. code-block:: xml

     <modify-argument...>
         <remove-default-expression />
     </modify-argument>

.. _replace-default-expression:

replace-default-expression
^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``replace-default-expression`` node replaces the specified argument with the
expression specified by the ``with`` attribute, and it is a child of the
:ref:`modify-argument` node.

.. code-block:: xml

     <modify-argument>
         <replace-default-expression with="..." />
     </modify-argument>

.. _replace-type:

replace-type
^^^^^^^^^^^^

The ``replace-type`` node replaces the type of the given argument to the one
specified by the ``modified-type`` attribute, and it is a child of the
:ref:`modify-argument` node.

.. code-block:: xml

     <modify-argument>
         <replace-type modified-type="..." />
     </modify-argument>

If the new type is a class, the ``modified-type`` attribute must be set to
the fully qualified name (including name of the package as well as the class
name).

.. _define-ownership:

define-ownership
^^^^^^^^^^^^^^^^

The ``define-ownership`` tag indicates that the function changes the ownership
rules of the argument object, and it is a child of the
:ref:`modify-argument` node.
The ``class`` attribute specifies the class of
function where to inject the ownership altering code
(see :ref:`codegenerationterminology`). The ``owner`` attribute
specifies the new ownership of the object. It accepts the following values:

* target: the target language will assume full ownership of the object.
          The native resources will be deleted when the target language
          object is finalized.
* c++: The native code assumes full ownership of the object. The target
       language object will not be garbage collected.
* default: The object will get default ownership, depending on how it
           was created.

.. code-block:: xml

    <modify-argument>
          <define-ownership class="target | native"
                            owner="target | c++ | default" />
    </modify-argument>

.. _reference-count:

reference-count
^^^^^^^^^^^^^^^

The ``reference-count`` tag dictates how an argument should be handled by the
target language reference counting system (if there is any), it also indicates
the kind of relationship the class owning the function being modified has with
the argument. It is a child of the :ref:`modify-argument` node.
For instance, in a model/view relation a view receiving a model
as argument for a **setModel** method should increment the model's reference
counting, since the model should be kept alive as much as the view lives.
Remember that out hypothetical view could not become parent of the model,
since the said model could be used by other views as well.
The ``action`` attribute specifies what should be done to the argument
reference counting when the modified method is called. It accepts the
following values:

* add: increments the argument reference counter.
* add-all: increments the reference counter for each item in a collection.
* remove: decrements the argument reference counter.
* set: will assign the argument to the variable containing the reference.
* ignore: does nothing with the argument reference counter
          (sounds worthless, but could be used in situations
           where the reference counter increase is mandatory
           by default).

.. code-block:: xml

    <modify-argument>
          <reference-count action="add|add-all|remove|set|ignore" variable-name="..." />
    </modify-argument>


The variable-name attribute specifies the name used for the variable that
holds the reference(s).

.. _replace-value:

replace-value
^^^^^^^^^^^^^

The ``replace-value`` attribute lets you replace the return statement of a
function with a fixed string. This attribute can only be used for the
argument at ``index`` 0, which is always the function's return value.

.. code-block:: xml

     <modify-argument index="0" replace-value="this"/>

.. _parent:

parent
^^^^^^

The ``parent`` node lets you define the argument parent which will
take ownership of argument and will destroy the C++ child object when the
parent is destroyed (see :ref:`ownership-parent`).
It is a child of the :ref:`modify-argument` node.

.. code-block:: xml

    <modify-argument index="1">
          <parent index="this" action="add | remove" />
    </modify-argument>

In the ``index`` argument you must specify the parent argument. The action
*add* creates a parent link between objects, while *remove* will undo the
parentage relationship.
