Specifying Types
----------------

Including Snippets
^^^^^^^^^^^^^^^^^^

There might be repetitive XML code, for example function modifications that
need to be done on classes that are not related by type inheritance.
It is possible to split out such snippets and include them via an entity reference.

.. code-block:: xml

    <typesystem>
        <object-type name="A">
            &common_function_modifications;
        </object-type>
        <object-type name="B">
            &common_function_modifications;
        </object-type>
    </typesystem>

The entity name is interpreted as file name (with suffix **xml**) appended and resolved
in the type system paths passed as command line argument.

Note that this is not a standard externally parsed entity due to the limitations
of the underlying parser.

.. _typesystem:

typesystem
^^^^^^^^^^

This is the root node containing all the type system information.
It may contain :ref:`add-function`, :ref:`container-type`,
:ref:`custom-type`, :ref:`enum-type`, :ref:`extra-includes`, :ref:`function`,
:ref:`load-typesystem`, :ref:`namespace`, :ref:`object-type`,
:ref:`opaque-container`,
:ref:`primitive-type`, :ref:`rejection`, :ref:`smart-pointer-type`,
:ref:`suppress-warning`, :ref:`template`, :ref:`system_include`,
:ref:`typedef-type` or :ref:`value-type` child nodes.

It can have a number of attributes, described below.

.. code-block:: xml

    <typesystem package="..." default-superclass="..." allow-thread="..."
                exception-handling="..." snake-case="yes | no | both" >
    </typesystem>

The **package** attribute is a string describing the package to be used,
e.g. "QtCore".
The *optional* **default-superclass** attribute is the canonical C++ base class
name of all objects, e.g., "object".

The *optional* attributes **allow-thread** and **exception-handling**
specify the default handling for the corresponding function modification
(see :ref:`modify-function`).

The *optional* **snake-case** attribute specifies whether function
and field names will be automatically changed to the snake case
style that is common in Python (for example,  ``snakeCase`` will be
changed to ``snake_case``).

The value ``both`` means that the function or field will be exposed
under both its original name and the snake case version. There are
limitations to this though:

- When overriding a virtual function of a C++ class in Python,
  the snake case name must be used.

- When static and non-static overloads of a class member function
  exist (as is the case for example for ``QFileInfo::exists()``),
  the snake case name must be used.

.. _load-typesystem:

load-typesystem
^^^^^^^^^^^^^^^

The ``load-typesystem`` node specifies which type systems to load when mapping
multiple libraries to another language or basing one library on another, and
it is a child of the :ref:`typesystem` node.

.. code-block:: xml

    <typesystem>
        <load-typesystem name="..." generate="yes | no" />
    </typesystem>

The **name** attribute is the filename of the typesystem to load, the
**generate** attribute specifies whether code should be generated or not. The
later must be specified when basing one library on another, making the generator
able to understand inheritance hierarchies, primitive mapping, parameter types
in functions, etc.

Most libraries will be based on both the QtCore and QtGui modules, in which
case code generation for these libraries will be disabled.

.. _rejection:

rejection
^^^^^^^^^

The ``rejection`` node rejects the given class, or the specified function
or field, and it is a child of the :ref:`typesystem` node.

.. code-block:: xml

    <typesystem>
        <rejection class="..."
            function-name="..."
            argument-type="..."
            field-name="..." />
    </typesystem>

The **class** attribute is the C++ class name of the class to reject. Use
the *optional* **function-name**, **argument-type**, or **field-name**
attributes to reject a particular function, function with arguments of a
particular type, or a field. Note that the **field-name** and
**function-name**/**argument-type** cannot be specified at the same time.
To remove all occurrences of a given field or function, set the class
attribute to \*.

.. _primitive-type:

primitive-type
^^^^^^^^^^^^^^

The ``primitive-type`` node describes how a primitive type is mapped from C++ to
the target language, and is a child of the :ref:`typesystem` node. It may
contain :ref:`conversion-rule` child nodes. Note that most primitives are
already specified in the QtCore typesystem (see :ref:`primitive-cpp-types`).

.. code-block:: xml

    <typesystem>
        <primitive-type name="..."
            since="..."
            until="..."
            target-lang-api-name="..."
            default-constructor="..."
            preferred-conversion="yes | no" />
            view-on="..."
    </typesystem>

The **name** attribute is the name of the primitive in C++.

The optional **target-lang-api-name** attribute is the name of the
primitive type in the target language, defaulting to the **name** attribute.

The *optional*  **since** value is used to specify the API version in which
the type was introduced.

Similarly, the *optional*  **until** value can be used to specify the API
version in which the type will be obsoleted.

If the *optional* **preferred-conversion** attribute is set to *no*, it
indicates that this version of the primitive type is not the preferred C++
equivalent of the target language type. For example, in Python both "qint64"
and "long long" become "long" but we should prefer the "qint64" version. For
this reason we mark "long long" with preferred-conversion="no".

The *optional* **default-constructor** specifies the minimal constructor
call to build one value of the primitive-type. This is not needed when the
primitive-type may be built with a default constructor (the one without
arguments).

The *optional* **preferred-conversion** attribute tells how to build a default
instance of the primitive type. It should be a constructor call capable of
creating a instance of the primitive type. Example: a class "Foo" could have
a **preferred-conversion** value set to "Foo()". Usually this attribute is
used only for classes declared as primitive types and not for primitive C++
types, but that depends on the application using *ApiExtractor*.

The *optional* **view-on** attribute specifies that the type is a view
class like std::string_view or QStringView which has a constructor
accepting another type like std::string or QString. Since typically
no values can be assigned to view classes, no target-to-native conversion
can be generated for them. Instead, an instance of the viewed class should
be instantiated and passed to functions using the view class
for argument types.

See :ref:`predefined_templates` for built-in templates for standard type
conversion rules.

.. _namespace:

namespace-type
^^^^^^^^^^^^^^

The ``namespace-type`` node maps the given C++ namespace to the target
language, and it is a child of the :ref:`typesystem` node or other
``namespace-type`` nodes. It may contain :ref:`add-function`,
:ref:`declare-function`,  :ref:`enum-type`, :ref:`extra-includes`,
:ref:`include-element`,  :ref:`modify-function`, ``namespace-type``,
:ref:`object-type`, :ref:`smart-pointer-type`, :ref:`typedef-type` or :ref:`value-type`
child nodes.

.. code-block:: xml

    <typesystem>
        <namespace-type name="..."
            visible="true | auto | false"
            generate="yes | no"
            generate-using="yes | no"
            package="..."
            since="..."
            revision="..." />
    </typesystem>

The **name** attribute is the name of the namespace, e.g., "Qt".

The *optional* **visible** attribute is used specify whether the
namespace is visible in the target language name. Its default value is
**auto**. It means that normal namespaces are visible, but inline namespaces
(as introduced in C++ 11) will not be visible.

The detection of inline namespaces requires shiboken to be built
using LLVM 9.0.

The *optional* **generate** is a legacy attribute. Specifying
**no** is equivalent to **visible="false"**.

The *optional* **generate-using** attribute specifies whether
``using namespace`` is generated into the wrapper code for classes within
the namespace (default: **yes**). This ensures for example that not fully
qualified enumeration values of default argument values compile.
However, in rare cases, it might cause ambiguities and can then be turned
off.

The **package** attribute can be used to override the package of the type system.

The *optional*  **since** value is used to specify the API version of this type.

The **revision** attribute can be used to specify a revision for each type, easing the
production of ABI compatible bindings.

.. _enum-type:

enum-type
^^^^^^^^^

The ``enum-type`` node maps the given enum from C++ to the target language,
and it is a child of the :ref:`typesystem` node. Use
:ref:`reject-enum-value` child nodes to reject values.

.. code-block:: xml

    <typesystem>
        <enum-type name="..."
            identified-by-value="..."
            class="yes | no"
            since="..."
            flags="yes | no"
            flags-revision="..."
            cpp-type = "..."
            python-type = "IntEnum | IntFlag"
            lower-bound="..."
            upper-bound="..."
            force-integer="yes | no"
            extensible="yes | no"
            revision="..." />
    </typesystem>

The **name** attribute is the fully qualified C++ name of the enum
(e.g.,"Qt::FillRule"). If the *optional* **flags** attribute is set to *yes*
(the default is *no*), the generator will expect an existing QFlags<T> for the
given enum type. The **lower-bound** and **upper-bound** attributes are used
to specify runtime bounds checking for the enum value. The value must be a
compilable target language statement, such as "QGradient.Spread.PadSpread"
(taking again Python as an example). If the **force-integer** attribute is
set to *yes* (the default is *no*), the generated target language code will
use the target language integers instead of enums. And finally, the
**extensible** attribute specifies whether the given enum can be extended
with user values (the default is *no*).

The *optional*  **since** value is used to specify the API version of this type.

The attribute **identified-by-value** helps to specify anonymous enums using the
name of one of their values, which is unique for the anonymous enum scope.
Notice that the **enum-type** tag can either have **name** or **identified-by-value**
but not both.

The *optional* **python-type** attribute specifies the underlying
Python type.

The *optional* **cpp-type** attribute specifies a C++ to be used for
casting values. This can be useful for large values triggering MSVC
signedness issues.

The **revision** attribute can be used to specify a revision for each type, easing the
production of ABI compatible bindings.

The **flags-revision** attribute has the same purposes of **revision** attribute but
is used for the QFlag related to this enum.

.. _reject-enum-value:

reject-enum-value
^^^^^^^^^^^^^^^^^

The ``reject-enum-value`` node rejects the enum value specified by the
**name** attribute, and it is a child of the :ref:`enum-type` node.

.. code-block:: xml

     <enum-type>
         <reject-enum-value name="..."/>
     </enum-type>

This node is used when a C++ enum implementation has several identical numeric
values, some of which are typically obsolete.

.. _value-type:

value-type
^^^^^^^^^^

The ``value-type`` node indicates that the given C++ type is mapped onto the target
language as a value type. This means that it is an object passed by value on C++,
i.e. it is stored in the function call stack. It is a child of the :ref:`typesystem`
node or other type nodes and may contain :ref:`add-function`, :ref:`add-pymethoddef`,
:ref:`configuration-element`, :ref:`declare-function`, :ref:`conversion-rule`,
:ref:`enum-type`, :ref:`extra-includes`, :ref:`include-element`, :ref:`modify-function`,
:ref:`object-type`, :ref:`smart-pointer-type`, :ref:`typedef-type` or further
``value-type`` child nodes.

.. code-block:: xml

    <typesystem>
        <value-type  name="..." since="..."
         copyable="yes | no"
         allow-thread="..."
         disable-wrapper="yes | no"
         exception-handling="..."
         generate-functions="..."
         isNull ="yes | no"
         operator-bool="yes | no"
         hash-function="..."
         private="yes | no"
         qt-register-metatype = "yes | no | base"
         stream="yes | no"
         default-constructor="..."
         revision="..."
         snake-case="yes | no | both" />
    </typesystem>

The **name** attribute is the fully qualified C++ class name, such as
"QMatrix" or "QPainterPath::Element". The **copyable** attribute is used to
force or not specify if this type is copyable. The *optional* **hash-function**
attribute informs the function name of a hash function for the type.

The *optional* attribute **stream** specifies whether this type will be able to
use externally defined operators, like QDataStream << and >>. If equals to **yes**,
these operators will be called as normal methods within the current class.

The *optional*  **since** value is used to specify the API version of this type.

The *optional* **default-constructor** specifies the minimal constructor
call to build one instance of the value-type. This is not needed when the
value-type may be built with a default constructor (the one without arguments).
Usually a code generator may guess a minimal constructor for a value-type based
on its constructor signatures, thus **default-constructor** is used only in
very odd cases.

For the *optional* **disable-wrapper** and **generate-functions**
attributes, see :ref:`object-type`.

For the *optional* **private** attribute, see :ref:`private_types`.

The *optional* **qt-register-metatype** attribute determines whether
a Qt meta type registration is generated for ``name``. By
default, this is generated for non-abstract, default-constructible
types for usage in signals and slots.
The value ``base`` means that the registration will be generated for the
class in question but not for inheriting classes.  This allows for
restricting the registration to base classes of type hierarchies.

The **revision** attribute can be used to specify a revision for each type, easing the
production of ABI compatible bindings.

The *optional* attributes **allow-thread** and **exception-handling**
specify the default handling for the corresponding function modification
(see :ref:`modify-function`).

The *optional* **snake-case** attribute allows for overriding the value
specified on the **typesystem** element.

The *optional* **isNull** and **operator-bool** attributes can be used
to override the command line setting for generating bool casts
(see :ref:`bool-cast`).

.. _object-type:

object-type
^^^^^^^^^^^

The object-type node indicates that the given C++ type is mapped onto the target
language as an object type. This means that it is an object passed by pointer on
C++ and it is stored on the heap. It is a child of the :ref:`typesystem` node
or other type nodes and may contain :ref:`add-function`, :ref:`add-pymethoddef`,
:ref:`configuration-element`, :ref:`declare-function`, :ref:`enum-type`,
:ref:`extra-includes`, :ref:`include-element`, :ref:`modify-function`,
``object-type``, :ref:`smart-pointer-type`, :ref:`typedef-type` or
:ref:`value-type` child nodes.

.. code-block:: xml

    <typesystem>
        <object-type name="..."
         since="..."
         copyable="yes | no"
         allow-thread="..."
         disable-wrapper="yes | no"
         exception-handling="..."
         generate-functions="..."
         force-abstract="yes | no"
         hash-function="..."
         isNull ="yes | no"
         operator-bool="yes | no"
         parent-management="yes | no"
         polymorphic-id-expression="..."
         polymorphic-name-function="..."
         private="yes | no"
         qt-register-metatype = "yes | no | base"
         stream="yes | no"
         revision="..."
         snake-case="yes | no | both" />
    </typesystem>

The **name** attribute is the fully qualified C++ class name. If there is no
C++ base class, the default-superclass attribute can be used to specify a
superclass for the given type, in the generated target language API. The
**copyable** and **hash-function** attributes are the same as described for
:ref:`value-type`.

The *optional* **force-abstract** attribute forces the class to be
abstract, disabling its instantiation. The generator will normally detect
this automatically unless the class inherits from an abstract base class
that is not in the type system.

The *optional* **disable-wrapper** attribute disables the generation of a
**C++ Wrapper** (see :ref:`codegenerationterminology`). This will
effectively disable overriding virtuals methods in Python for the class.
It can be used when the class cannot be instantiated from Python and
its virtual methods pose some problem for the code generator (by returning
references, or using a default value that cannot be generated for a
parameter, or similar).

For the *optional* **private** attribute, see :ref:`private_types`.

The *optional* **qt-register-metatype** attribute determines whether
a Qt meta type registration is generated for ``name *``. By
default, this is only generated for non-QObject types for usage
in signals and slots.
The value ``base`` means that the registration will be generated for the
class in question but not for inheriting classes.  This allows for
restricting the registration to base classes of type hierarchies.

The *optional* attribute **stream** specifies whether this type will be able to
use externally defined operators, like QDataStream << and >>. If equals to **yes**,
these operators will be called as normal methods within the current class.

The *optional*  **since** value is used to specify the API version of this type.

The **revision** attribute can be used to specify a revision for each type, easing the
production of ABI compatible bindings.

The *optional* attributes **allow-thread** and **exception-handling**
specify the default handling for the corresponding function modification
(see :ref:`modify-function`).

The *optional* **generate-functions** specifies a semicolon-separated
list of function names or minimal signatures to be generated.
This allows for restricting the functions for which bindings are generated.
This also applies to virtual functions; so, all abstract functions
need to be listed to prevent non-compiling code to be generated.
If nothing is specified, bindings for all suitable functions are
generated. Note that special functions (constructors, etc),
cannot be specified.

The *optional* **snake-case** attribute allows for overriding the value
specified on the **typesystem** element.

The *optional* **isNull** and **operator-bool** attributes can be used
to override the command line setting for generating bool casts
(see :ref:`bool-cast`).

The *optional* **parent-management** attribute specifies that the class is
used for building object trees consisting of parents and children, for
example, user interfaces like the ``QWidget`` classes. For those classes,
the heuristics enabled by :ref:`ownership-parent-heuristics` and
:ref:`return-value-heuristics` are applied to automatically set parent
relationships. Compatibility note: In shiboken 6, when no class of the
type system has this attribute set, the heuristics will be applied
to all classes. In shiboken 7, it will be mandatory to set the
attribute.

The *optional* **polymorphic-id-expression** attribute specifies an
expression checking whether a base class pointer is of the matching
type. For example, in a ``virtual eventHandler(BaseEvent *e)``
function, this is used to construct a Python wrapper matching
the derived class (for example, a ``MouseEvent`` or similar).

The *optional* **polymorphic-name-function** specifies the name of a
function returning the type name of a derived class on the base class
type entry. Normally, ``typeid(ptr).name()`` is used for this.
However, this fails if the type hierarchy does not have virtual functions.
In this case, a function is required which typically decides depending
on some type enumeration.

interface-type
^^^^^^^^^^^^^^

This type is deprecated and no longer has any effect. Use object-type instead.

.. _container-type:

container-type
^^^^^^^^^^^^^^

The ``container-type`` node indicates that the given class is a container and
must be handled using one of the conversion helpers provided by attribute **type**.
It is a child of the :ref:`typesystem` node and may contain
:ref:`conversion-rule` child nodes.

.. code-block:: xml

    <typesystem>
        <container-type name="..."
            since="..."
            type ="..."
            opaque-containers ="..." />
    </typesystem>

The **name** attribute is the fully qualified C++ class name. The **type**
attribute is used to indicate what conversion rule will be applied to the
container. It can be one of: *list*, *set*, *map*, *multi-map* or *pair*.

Some types were deprecated in 6.2: *string-list*, *linked-list*, *vector*,
*stack* and *queue* are equivalent to *list*. *hash* and *multi-hash*
are equivalent to *map* and *multi-map*, respectively.

The *optional* **opaque-containers** attribute specifies a semi-colon separated
list of mappings from instantiations to a type name for
:ref:`opaque-containers`:

.. code-block:: xml

    <typesystem>
        <container-type name="std::array"
                        opaque-containers ="int,3:IntArray3;float,4:FloatArray4">


The *optional*  **since** value is used to specify the API version of this container.

Some common standard containers are :ref:`built-in <builtin-cpp-container-types>`,
and there are also a number of useful
:ref:`predefined conversion templates <predefined_templates>`.

.. _opaque-container:

opaque-container
^^^^^^^^^^^^^^^^

The ``opaque-container`` element can be used to  add further instantiations
of :ref:`opaque containers <opaque-containers>` to existing container types
(built-in or specified by :ref:`container-type` in included modules).
It is a child of the :ref:`typesystem` node.

.. code-block:: xml

    <typesystem>
        <oqaque-container name="..." opaque-containers ="..." />
    </typesystem>

For the **name** and **opaque-containers** attributes,
see :ref:`container-type`.

.. _typedef-type:

typedef-type
^^^^^^^^^^^^

The ``typedef-type`` node allows for specifying typedefs in the typesystem. They
are mostly equivalent to spelling out the typedef in the included header, which
is often complicated when trying to wrap libraries whose source code cannot be
easily extended.
It is a child of the :ref:`typesystem` node or other type nodes.

.. code-block:: xml

    <typesystem>
        <typedef-type name="..."
            source="..."
            since="..." />
    </typesystem>

The **source** attribute is the source. Example:

.. code-block:: xml

    <namespace-type name='std'>
        <value-type name='optional' generate='no'/>\n"
    </namespace-type>
    <typedef-type name="IntOptional" source="std::optional&lt;int&gt;"/>

is equivalent to

.. code-block:: c++

    typedef std::optional<int> IntOptional;

The *optional*  **since** value is used to specify the API version of this type.

.. _custom-type:

custom-type
^^^^^^^^^^^

The ``custom-type`` node simply makes the parser aware of the existence of a target
language type, thus avoiding errors when trying to find a type used in function
signatures and other places. The proper handling of the custom type is meant to
be done by a generator using the APIExractor.
It is a child of the :ref:`typesystem` node.

.. code-block:: xml

    <typesystem>
        <custom-type name="..."
            check-function="..." />
    </typesystem>

The **name** attribute is the name of the custom type, e.g., "PyObject".

The *optional*  **check-function** attribute can be used to specify a
boolean check function that verifies if the PyObject is of the given type
in the function overload decisor. While shiboken knows common check
functions like ``PyLong_Check()`` or ``PyType_Check()``, it might be useful
to provide one for function arguments modified to be custom types
handled by injected code (see :ref:`replace-type`).

See :ref:`cpython-types` for built-in types.

.. _smart-pointer-type:

smart-pointer-type
^^^^^^^^^^^^^^^^^^

The ``smart pointer`` type node indicates that the given class is a smart pointer
and requires inserting calls to **getter** to access the pointeee.
Currently, the usage is limited to function return values.
**ref-count-method** specifies the name of the method used to do reference counting.
It is a child of the :ref:`typesystem` node or other type nodes.

The *optional* attribute **instantiations** specifies for which instantiations
of the smart pointer wrappers will be generated (comma-separated list).
By default, this will happen for all instantiations found by code parsing.
This might be a problem when linking different modules, since wrappers for the
same instantiation might be generated into different modules, which then clash.
Providing an instantiations list makes it possible to specify which wrappers
will be generated into specific modules.

.. code-block:: xml

    <typesystem>
        <smart-pointer-type name="..."
            since="..."
            type="shared | handle | value-handle | unique"
            getter="..."
            ref-count-method="..."
            value-check-method="..."
            null-check-method="..."
            reset-method="..."
            instantiations="..."/>
        </typesystem>


The *optional* attribute **value-check-method** specifies a method
that can be used to check whether the pointer has a value.

The *optional* attribute **null-check-method** specifies a method
that can be used to check for ``nullptr``.

The *optional* attribute **reset-method** specifies a method
that can be used to clear the pointer.

The *optional* instantiations attribute specifies a comma-separated
list of instantiation types. When left empty, all instantiations
found in the code will be generated. The type name might optionally
be followed an equal sign and the Python type name, for example
``instantiations="int=IntPtr,double=DoublePtr"``.
It is also possible to specify a namespace delimited by ``::``.

The *optional* attribute **type** specifies the type:

*shared*
   A standard shared pointer.
*handle*
   A basic pointer handle which has a getter function and an
   ``operator->``.
*value-handle*
   A handle which has a getter function returning a value
   (``T`` instead of ``T *`` as for the other types).
   It can be used for ``std::optional``.
*unique*
   A standard, unique pointer (``std::unique_ptr``) or a similar
   movable pointer.
   Specifying the ``reset-method`` attribute is required for this work.

The example below shows an entry for a ``std::shared_ptr``:

.. code-block:: xml

    <system-include file-name="memory"/>

    <namespace-type name="std">
        <include file-name="memory" location="global"/>
        <modify-function signature="^.*$" remove="all"/>
        <enum-type name="pointer_safety"/>
        <smart-pointer-type name="shared_ptr" type="shared" getter="get"
                            ref-count-method="use_count"
                            instantiations="Integer">
            <include file-name="memory" location="global"/>
        </smart-pointer-type>
    </namespace-type>

If the smart pointer is the only relevant class from namespace ``std``,
it can also be hidden:

.. code-block:: xml

    <namespace-type name="std" visible="no">
        <smart-pointer-type name="shared_ptr" type="shared" getter="get"
                            ref-count-method="use_count"
                            instantiations="Integer">
            <include file-name="memory" location="global"/>
        </smart-pointer-type>
    </namespace-type>

First, shiboken is told to actually parse the system include files
containing the class definition using the :ref:`system_include`
element. For the ``namespace-type`` and ``smart-pointer-type``, the
standard include files are given to override the internal implementation
header ``shared_ptr.h``.
This creates some wrapper sources which need to be added to the
``CMakeLists.txt`` of the module.

.. _function:

function
^^^^^^^^

The ``function`` node indicates that the given C++ global function is mapped
onto the target language. It is a child of the :ref:`typesystem` node
and may contain a :ref:`modify-function` child node.

.. code-block:: xml

    <typesystem>
        <function signature="..." rename="..." since="..." snake-case="yes | no | both" />
    </typesystem>

There is a limitation; you cannot add a function overload using
the :ref:`add-function` tag to an existent function.

The *optional* **since** attribute is used to specify the API version in which
the function was introduced.

The *optional* **rename** attribute is used to modify the function name.

The *optional* **snake-case** attribute allows for overriding the value
specified on the **typesystem** element.

.. _system_include:

system-include
^^^^^^^^^^^^^^

The optional **system-include** specifies the name of a system include
file or a system include path (indicated by a trailing slash) to be
parsed. Normally, include files considered to be system include
files are skipped by the C++ code parser. Its primary use case
is exposing classes from the STL library.
It is a child of the :ref:`typesystem` node.

.. code-block:: xml

    <typesystem>
        <system-include file-name="memory"/>
        <system-include file-name="/usr/include/Qt/"/>
    </typesystem>

.. _conditional_processing:

Conditional Processing
^^^^^^^^^^^^^^^^^^^^^^

Simple processing instructions are provided for including or excluding
sections depending on the presence of keywords. The syntax is:

.. code-block:: xml

    <?if keyword !excluded_keyword ?>
       ...
    <?endif?>

There are predefined keywords indicating the operating system (``windows``,
``unix`` and ``darwin``).

The language level passed to the ``language-level`` command line option
is reflected as ``c++11``, ``c++14``, ``c++17`` or ``c++20``.

The class names passed to the
:ref:`--drop-type-entries <drop-type-entries>` command line option
are also predefined, prefixed by ``no_``. This allows for example
for enclosing added functions referring to those classes within
``<?if !no_ClassName?>``, ``<?endif?>``.

Other keywords can be specified using the
:ref:`--keywords <conditional_keywords>` command line option.

.. _private_types:

Defining Entities
^^^^^^^^^^^^^^^^^

It is possible to define entities using a simple processing instruction:

.. code-block:: xml

    <?entity name value?>
    <text>&name;</text>

This allows for defining function signatures depending on platform
in conjunction with :ref:`conditional_processing`.

Private Types
^^^^^^^^^^^^^

Marking :ref:`object-type` or :ref:`value-type` entries as private causes a
separate, private module header besides the public module header to be
generated for them.

This can be used for classes that are not referenced in dependent modules
and helps to prevent the propagation of for example private C++ headers
required for them.
