# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

"""Helper functions for formatting information on QMetaObject"""

from PySide6.QtCore import QMetaMethod


def _qbytearray_to_string(b):
    return bytes(b.data()).decode('utf-8')


def _format_metatype(meta_type):
    return meta_type.id() if meta_type.isValid() else '<invalid>'


def _dump_metaobject_helper(meta_obj, indent):
    meta_id = 0
    # FIXME: Otherwise crashes in Qt
    if meta_obj.propertyOffset() < meta_obj.propertyCount():
        meta_id = _format_metatype(meta_obj.metaType())
    print(f'{indent}class {meta_obj.className()}/{meta_id}:')
    indent += '    '

    info_offset = meta_obj.classInfoOffset()
    info_count = meta_obj.classInfoCount()
    if info_offset < info_count:
        print(f'{indent}Info:')
        for i in range(info_offset, info_count):
            name = meta_obj.classInfo(i).name()
            value = meta_obj.classInfo(i).value()
            print(f'{indent}{i:4d} {name}+{value}')

    enumerator_offset = meta_obj.enumeratorOffset()
    enumerator_count = meta_obj.enumeratorCount()
    if enumerator_offset < enumerator_count:
        print(f'{indent}Enumerators:')
        for e in range(enumerator_offset, enumerator_count):
            meta_enum = meta_obj.enumerator(e)
            name = meta_enum.name()
            value_str = ''
            descr = ''
            if meta_enum.isFlag():
                descr += ' flag'
            if meta_enum.isScoped():
                descr += ' scoped'
            for k in range(meta_enum.keyCount()):
                if k > 0:
                    value_str += ', '
                key = meta_enum.key(k)
                value = meta_enum.value(k)
                value_str += f'{key} = {value}'
            print(f'{indent}{e:4d} {name}{descr} ({value_str})')

    property_offset = meta_obj.propertyOffset()
    property_count = meta_obj.propertyCount()
    if property_offset < property_count:
        print(f'{indent}Properties:')
        for p in range(property_offset, property_count):
            meta_property = meta_obj.property(p)
            name = meta_property.name()
            desc = ''
            if meta_property.isConstant():
                desc += ', constant'
            if meta_property.isDesignable():
                desc += ', designable'
            if meta_property.isFlagType():
                desc += ', flag'
            if meta_property.isEnumType():
                desc += ', enum'
            if meta_property.isStored():
                desc += ', stored'
            if meta_property.isWritable():
                desc += ', writable'
            if meta_property.isResettable():
                desc += ', resettable'
            if meta_property.hasNotifySignal():
                notify_name_b = meta_property.notifySignal().name()
                notify_name = _qbytearray_to_string(notify_name_b)
                desc += f', notify="{notify_name}"'
            meta_id = _format_metatype(meta_property.metaType())
            type_name = meta_property.typeName()
            print(f'{indent}{p:4d} {type_name}/{meta_id} "{name}"{desc}')

    method_offset = meta_obj.methodOffset()
    method_count = meta_obj.methodCount()
    if method_offset < method_count:
        print('{}Methods:'.format(indent))
        for m in range(method_offset, method_count):
            method = meta_obj.method(m)
            signature = _qbytearray_to_string(method.methodSignature())
            access = ''
            if method.access() == QMetaMethod.Protected:
                access += 'protected '
            elif method.access() == QMetaMethod.Private:
                access += 'private '
            type = method.methodType()
            typeString = ''
            if type == QMetaMethod.Signal:
                typeString = ' (Signal)'
            elif type == QMetaMethod.Slot:
                typeString = ' (Slot)'
            elif type == QMetaMethod.Constructor:
                typeString = ' (Ct)'
            type_name = method.typeName()
            desc = f'{indent}{m:4d} {access}{type_name} "{signature}"{typeString}'
            parameter_names = method.parameterNames()
            if parameter_names:
                parameter_types = method.parameterTypes()
                desc += ' Parameters:'
                for p, bname in enumerate(parameter_names):
                    name = _qbytearray_to_string(bname)
                    type_name = _qbytearray_to_string(parameter_types[p])
                    if not name:
                        name = '<unnamed>'
                    desc += f' "{name}": {type_name}'
            print(desc)


def dump_metaobject(meta_obj):
    super_classes = [meta_obj]
    super_class = meta_obj.superClass()
    while super_class:
        super_classes.append(super_class)
        super_class = super_class.superClass()
    indent = ''
    for c in reversed(super_classes):
        _dump_metaobject_helper(c, indent)
        indent += '    '
