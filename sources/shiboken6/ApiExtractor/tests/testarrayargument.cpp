// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "testarrayargument.h"
#include "testutil.h"
#include <abstractmetaargument.h>
#include <abstractmetaenum.h>
#include <abstractmetafunction.h>
#include <abstractmetalang.h>
#include <abstractmetatype.h>
#include <primitivetypeentry.h>
#include <parser/enumvalue.h>

#include <qtcompat.h>

#include <QtTest/QTest>

using namespace Qt::StringLiterals;

void TestArrayArgument::testArrayArgumentWithSizeDefinedByInteger()
{
    const char cppCode[] = "\
    struct A {\n\
        enum SomeEnum { Value0, Value1, NValues };\n\
        void method(double[3]);\n\
    };\n";
    const char xmlCode[] = "\
    <typesystem package='Foo'>\n\
        <primitive-type name='double'/>\n\
        <object-type name='A'>\n\
            <enum-type name='SomeEnum'/>\n\
        </object-type>\n\
    </typesystem>\n";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode, false));
    QVERIFY(!builder.isNull());
    const auto classA = AbstractMetaClass::findClass(builder->classes(), u"A");
    QVERIFY(classA);

    const AbstractMetaArgument &arg = classA->functions().constLast()->arguments().constFirst();
    QVERIFY(arg.type().isArray());
    QCOMPARE(arg.type().arrayElementCount(), 3);
    QCOMPARE(arg.type().arrayElementType()->name(), u"double");
}

static QString functionMinimalSignature(const AbstractMetaClassCPtr &c, const QString &name)
{
    const auto f = c->findFunction(name);
    return f ? f->minimalSignature() : QString();
}

void TestArrayArgument::testArraySignature()
{
    const char cppCode[] ="\
    struct A {\n\
        void mi1(int arg[5]);\n\
        void mi1c(const int arg[5]);\n\
        void mi1cu(const int arg[]);\n\
        void mc1cu(const char arg[]);\n\
        void mc1cup(const char *arg[]);\n\
        void muc2(unsigned char *arg[2][3]);\n\
        void mc2c(const char *arg[5][6]);\n\
        void mc2cu(const char arg[][2]);\n\
    };\n";
    const char xmlCode[] = "\
    <typesystem package='Foo'>\n\
        <primitive-type name='char'/>\n\
        <primitive-type name='unsigned char'/>\n\
        <primitive-type name='int'/>\n\
        <object-type name='A'/>\n\
    </typesystem>\n";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode, false));
    QVERIFY(!builder.isNull());
    const auto classA = AbstractMetaClass::findClass(builder->classes(), u"A");
    QCOMPARE(functionMinimalSignature(classA, u"mi1"_s),
             u"mi1(int[5])");
    QCOMPARE(functionMinimalSignature(classA, u"mi1c"_s),
             u"mi1c(const int[5])");
    QCOMPARE(functionMinimalSignature(classA, u"mi1cu"_s),
             u"mi1cu(const int[])");
    QCOMPARE(functionMinimalSignature(classA, u"mc1cu"_s),
             u"mc1cu(const char*)");
    QCOMPARE(functionMinimalSignature(classA, u"mc1cup"_s),
             u"mc1cup(const char*[])");
    QCOMPARE(functionMinimalSignature(classA, u"muc2"_s),
             u"muc2(unsigned char*[2][3])");
    QCOMPARE(functionMinimalSignature(classA, u"mc2c"_s),
             u"mc2c(const char*[5][6])");
    QCOMPARE(functionMinimalSignature(classA, u"mc2cu"_s),
             u"mc2cu(const char[][2])");
}

void TestArrayArgument::testArrayArgumentWithSizeDefinedByEnumValue()
{
    const char cppCode[] = "\
    struct A {\n\
        enum SomeEnum { Value0, Value1, NValues };\n\
        void method(double[NValues]);\n\
    };\n";
    const char xmlCode[] = "\
    <typesystem package='Foo'>\n\
        <primitive-type name='double'/>\n\
        <object-type name='A'>\n\
            <enum-type name='SomeEnum'/>\n\
        </object-type>\n\
    </typesystem>\n";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode, false));
    QVERIFY(!builder.isNull());
    AbstractMetaClassPtr classA = AbstractMetaClass::findClass(builder->classes(), u"A");
    QVERIFY(classA);

    auto someEnum = classA->findEnum(u"SomeEnum"_s);
    QVERIFY(someEnum.has_value());
    auto nvalues = classA->findEnumValue(u"NValues"_s);
    QVERIFY(nvalues.has_value());

    const AbstractMetaArgument &arg = classA->functions().constLast()->arguments().constFirst();
    QVERIFY(arg.type().isArray());
    QCOMPARE(arg.type().arrayElementCount(), nvalues->value().value());
    QCOMPARE(arg.type().arrayElementType()->name(), u"double");
};

void TestArrayArgument::testArrayArgumentWithSizeDefinedByEnumValueFromGlobalEnum()
{
    const char cppCode[] = "\
    enum SomeEnum { Value0, Value1, NValues };\n\
    struct A {\n\
        void method(double[NValues]);\n\
    };\n";
    const char xmlCode[] = "\
    <typesystem package='Foo'>\n\
        <primitive-type name='double'/>\n\
        <enum-type name='SomeEnum'/>\n\
        <object-type name='A'>\n\
        </object-type>\n\
    </typesystem>\n";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode, false));
    QVERIFY(builder);
    const auto classA = AbstractMetaClass::findClass(builder->classes(), u"A");
    QVERIFY(classA);

    AbstractMetaEnum someEnum = builder->globalEnums().constFirst();
    auto nvalues = someEnum.findEnumValue(u"NValues");
    QVERIFY(nvalues.has_value());

    const AbstractMetaArgument &arg = classA->functions().constLast()->arguments().constFirst();
    QVERIFY(arg.type().isArray());
    QCOMPARE(arg.type().arrayElementCount(), nvalues->value().value());
    QCOMPARE(arg.type().arrayElementType()->name(), u"double");
};

QTEST_APPLESS_MAIN(TestArrayArgument)
