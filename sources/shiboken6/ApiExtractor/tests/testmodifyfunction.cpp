// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "testmodifyfunction.h"
#include "testutil.h"
#include <abstractmetaargument.h>
#include <abstractmetabuilder_p.h>
#include <abstractmetafunction.h>
#include <abstractmetalang.h>
#include <abstractmetatype.h>
#include <modifications.h>
#include <typesystem.h>

#include <qtcompat.h>

#include <QtTest/QTest>

using namespace Qt::StringLiterals;

void TestModifyFunction::testRenameArgument_data()
{
    QTest::addColumn<QByteArray>("pattern");
    QTest::newRow("fixed_string") << QByteArrayLiteral("method(int)");
    QTest::newRow("regular_expression") << QByteArrayLiteral("^method.*");
}

void TestModifyFunction::testRenameArgument()
{
    QFETCH(QByteArray, pattern);

    const char cppCode[] = "\
    struct A {\n\
        void method(int=0);\n\
    };\n";
    const char xmlCode1[] = "\
    <typesystem package='Foo'>\n\
        <primitive-type name='int'/>\n\
        <object-type name='A'>\n\
        <modify-function signature='";
   const char xmlCode2[] = R"('>
            <modify-argument index='1' rename='otherArg'/>
        </modify-function>
        </object-type>
    </typesystem>
)";

    const QByteArray xmlCode = QByteArray(xmlCode1) + pattern + QByteArray(xmlCode2);
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode.constData(), false));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    const auto classA = AbstractMetaClass::findClass(classes, u"A");
    const auto func = classA->findFunction(u"method");
    QVERIFY(func);

    QCOMPARE(func->argumentName(1), u"otherArg");
}

void TestModifyFunction::testOwnershipTransfer()
{
    const char cppCode[] = "\
    struct A {};\n\
    struct B {\n\
        virtual A* method();\n\
    };\n";
    const char xmlCode[] = "\
    <typesystem package=\"Foo\">\n\
        <object-type name='A' />\n\
        <object-type name='B'>\n\
        <modify-function signature='method()'>\n\
            <modify-argument index='return'>\n\
                <define-ownership owner='c++'/>\n\
            </modify-argument>\n\
        </modify-function>\n\
        </object-type>\n\
    </typesystem>\n";
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode, false));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    const auto classB = AbstractMetaClass::findClass(classes, u"B");
    const auto func = classB->findFunction(u"method");
    QVERIFY(func);

    QCOMPARE(func->argumentTargetOwnership(func->ownerClass(), 0),
             TypeSystem::CppOwnership);
}


void TestModifyFunction::invalidateAfterUse()
{
    const char cppCode[] = "\
    struct A {\n\
        virtual void call(int *a);\n\
    };\n\
    struct B : A {\n\
    };\n\
    struct C : B {\n\
        virtual void call2(int *a);\n\
    };\n\
    struct D : C {\n\
        virtual void call2(int *a);\n\
    };\n\
    struct E : D {\n\
    };\n";
    const char xmlCode[] = "\
    <typesystem package='Foo'>\n\
        <primitive-type name='int'/>\n\
        <object-type name='A'>\n\
        <modify-function signature='call(int*)'>\n\
          <modify-argument index='1' invalidate-after-use='true'/>\n\
        </modify-function>\n\
        </object-type>\n\
        <object-type name='B' />\n\
        <object-type name='C'>\n\
        <modify-function signature='call2(int*)'>\n\
          <modify-argument index='1' invalidate-after-use='true'/>\n\
        </modify-function>\n\
        </object-type>\n\
        <object-type name='D'>\n\
        <modify-function signature='call2(int*)'>\n\
          <modify-argument index='1' invalidate-after-use='true'/>\n\
        </modify-function>\n\
        </object-type>\n\
        <object-type name='E' />\n\
    </typesystem>\n";
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode,
                                                                false, u"0.1"_s));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    const auto classB = AbstractMetaClass::findClass(classes, u"B");
    auto func = classB->findFunction(u"call");
    QCOMPARE(func->modifications().size(), 1);
    QCOMPARE(func->modifications().at(0).argument_mods().size(), 1);
    QVERIFY(func->modifications().at(0).argument_mods().at(0).resetAfterUse());

    const auto classC = AbstractMetaClass::findClass(classes, u"C");
    QVERIFY(classC);
    func = classC->findFunction(u"call");
    QCOMPARE(func->modifications().size(), 1);
    QCOMPARE(func->modifications().at(0).argument_mods().size(), 1);
    QVERIFY(func->modifications().at(0).argument_mods().at(0).resetAfterUse());

    func = classC->findFunction(u"call2");
    QCOMPARE(func->modifications().size(), 1);
    QCOMPARE(func->modifications().at(0).argument_mods().size(), 1);
    QVERIFY(func->modifications().at(0).argument_mods().at(0).resetAfterUse());

    AbstractMetaClassCPtr classD =  AbstractMetaClass::findClass(classes, u"D");
    QVERIFY(classD);
    func = classD->findFunction(u"call");
    QCOMPARE(func->modifications().size(), 1);
    QCOMPARE(func->modifications().at(0).argument_mods().size(), 1);
    QVERIFY(func->modifications().at(0).argument_mods().at(0).resetAfterUse());

    func = classD->findFunction(u"call2");
    QCOMPARE(func->modifications().size(), 1);
    QCOMPARE(func->modifications().at(0).argument_mods().size(), 1);
    QVERIFY(func->modifications().at(0).argument_mods().at(0).resetAfterUse());

    const auto classE = AbstractMetaClass::findClass(classes, u"E");
    QVERIFY(classE);
    func = classE->findFunction(u"call");
    QVERIFY(func);
    QCOMPARE(func->modifications().size(), 1);
    QCOMPARE(func->modifications().at(0).argument_mods().size(), 1);
    QVERIFY(func->modifications().at(0).argument_mods().at(0).resetAfterUse());

    func = classE->findFunction(u"call2");
    QVERIFY(func);
    QCOMPARE(func->modifications().size(), 1);
    QCOMPARE(func->modifications().at(0).argument_mods().size(), 1);
    QVERIFY(func->modifications().at(0).argument_mods().at(0).resetAfterUse());
}

void TestModifyFunction::testWithApiVersion()
{
    const char cppCode[] = "\
    struct A {};\n\
    struct B {\n\
        virtual A* method();\n\
        virtual B* methodB();\n\
    };\n";
    const char xmlCode[] = "\
    <typesystem package='Foo'>\n\
        <object-type name='A' />\n\
        <object-type name='B'>\n\
        <modify-function signature='method()' since='0.1'>\n\
            <modify-argument index='return'>\n\
                <define-ownership owner='c++'/>\n\
            </modify-argument>\n\
        </modify-function>\n\
        <modify-function signature='methodB()' since='0.2'>\n\
            <modify-argument index='return'>\n\
                <define-ownership owner='c++'/>\n\
            </modify-argument>\n\
        </modify-function>\n\
        </object-type>\n\
    </typesystem>\n";
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode,
                                                                false, u"0.1"_s));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    const auto classB = AbstractMetaClass::findClass(classes, u"B");
    auto func = classB->findFunction(u"method");

    auto returnOwnership = func->argumentTargetOwnership(func->ownerClass(), 0);
    QCOMPARE(returnOwnership, TypeSystem::CppOwnership);

    func = classB->findFunction(u"methodB");
    returnOwnership = func->argumentTargetOwnership(func->ownerClass(), 0);
    QVERIFY(returnOwnership != TypeSystem::CppOwnership);
}

// Modifications on class/typesystem level are tested below
// in testScopedModifications().
void TestModifyFunction::testAllowThread()
{
    const char cppCode[] =R"CPP(\
struct A {
    void f1();
    void f2();
    void f3();
    int getter1() const;
    int getter2() const;
};
)CPP";

    const char xmlCode[] = R"XML(
<typesystem package='Foo'>
    <primitive-type name='int'/>
    <object-type name='A'>
        <modify-function signature='f2()' allow-thread='auto'/>
        <modify-function signature='f3()' allow-thread='no'/>
        <modify-function signature='getter2()const' allow-thread='yes'/>
    </object-type>
</typesystem>
)XML";
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode,
                                                                false, u"0.1"_s));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    const auto classA = AbstractMetaClass::findClass(classes, u"A");
    QVERIFY(classA);

    // Nothing specified, true
    const auto f1 = classA->findFunction(u"f1");
    QVERIFY(f1);
    QVERIFY(!f1->allowThread());

    // 'auto' specified, should be false for nontrivial function
    const auto f2 = classA->findFunction(u"f2");
    QVERIFY(f2);
    QVERIFY(f2->allowThread());

    // 'no' specified, should be false
    const auto f3 = classA->findFunction(u"f3");
    QVERIFY(f3);
    QVERIFY(!f3->allowThread());

    // Nothing specified, should be false for simple getter
    const auto getter1 = classA->findFunction(u"getter1");
    QVERIFY(getter1);
    QVERIFY(!getter1->allowThread());

    // Forced to true simple getter
    const auto getter2 = classA->findFunction(u"getter2");
    QVERIFY(getter2);
    QVERIFY(getter2->allowThread()); // Forced to true simple getter
}

void TestModifyFunction::testGlobalFunctionModification()
{
    const char cppCode[] = "\
    struct A {};\n\
    void function(A* a = 0);\n";
    const char xmlCode[] = "\
    <typesystem package='Foo'>\n\
        <primitive-type name='A'/>\n\
        <function signature='function(A*)'>\n\
            <modify-function signature='function(A*)'>\n\
                <modify-argument index='1'>\n\
                    <replace-type modified-type='A'/>\n\
                    <replace-default-expression with='A()'/>\n\
                </modify-argument>\n\
            </modify-function>\n\
        </function>\n\
    </typesystem>\n";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode, false));
    QVERIFY(builder);
    QCOMPARE(builder->globalFunctions().size(), 1);

    auto *td = TypeDatabase::instance();
    FunctionModificationList mods = td->globalFunctionModifications({u"function(A*)"_s});
    QCOMPARE(mods.size(), 1);
    const QList<ArgumentModification> &argMods = mods.constFirst().argument_mods();
    QCOMPARE(argMods.size(), 1);
    ArgumentModification argMod = argMods.constFirst();
    QCOMPARE(argMod.replacedDefaultExpression(), u"A()");

    QVERIFY(!builder->globalFunctions().isEmpty());
    const auto func = builder->globalFunctions().constFirst();
    QCOMPARE(func->arguments().size(), 1);
    const AbstractMetaArgument &arg = func->arguments().constFirst();
    QCOMPARE(arg.type().cppSignature(), u"A *");
    QCOMPARE(arg.originalDefaultValueExpression(), u"0");
    QCOMPARE(arg.defaultValueExpression(), u"A()");
}

// Tests modifications of exception handling and allow-thread
// on various levels.
void TestModifyFunction::testScopedModifications_data()
{
    QTest::addColumn<QByteArray>("cppCode");
    QTest::addColumn<QByteArray>("xmlCode");
    QTest::addColumn<bool>("expectedGenerateUnspecified");
    QTest::addColumn<bool>("expectedGenerateNonThrowing");
    QTest::addColumn<bool>("expectedGenerateThrowing");
    QTest::addColumn<bool>("expectedAllowThread");

    const QByteArray cppCode = R"CPP(
struct Base {
};

struct A : public Base {
    void unspecified();
    void nonThrowing() noexcept;
# if __cplusplus >= 201703L // C++ 17
    void throwing() noexcept(false);
#else
    void throwing() throw(int);
#endif
};
)CPP";

    // Default: Off
    QTest::newRow("none")
        << cppCode
        << QByteArray(R"XML(
<typesystem package= 'Foo'>
    <primitive-type name='int'/>
    <object-type name='Base'/>
    <object-type name='A'/>
</typesystem>)XML")
         << false << false << false // exception
         << false; // allowthread

    // Modify one function
    QTest::newRow("modify-function1")
        << cppCode
        << QByteArray(R"XML(
<typesystem package='Foo'>
    <primitive-type name='int'/>
    <object-type name='Base'/>
    <object-type name='A'>
        <modify-function signature='throwing()' exception-handling='auto-on'/>
    </object-type>
</typesystem>)XML")
         << false << false << true // exception
         << false; // allowthread

    // Flip defaults by modifying functions
    QTest::newRow("modify-function2")
        << cppCode
        << QByteArray(R"XML(
<typesystem package='Foo'>
    <primitive-type name='int'/>
    <object-type name='Base'/>
    <object-type name='A'>
        <modify-function signature='unspecified()' exception-handling='auto-on'/>
        <modify-function signature='throwing()' exception-handling='no'/>
    </object-type>
</typesystem>)XML")
         << true << false << false // exception
         << false; // allowthread

    // Activate on type system level
    QTest::newRow("typesystem-on")
        << cppCode
        << QByteArray(R"XML(
<typesystem package='Foo' exception-handling='auto-on' allow-thread='no'>
    <primitive-type name='int'/>
    <object-type name='Base'/>
    <object-type name='A'/>
</typesystem>)XML")
         << true << false << true // exception
         << false; // allowthread

    // Activate on class level
    QTest::newRow("class-on")
        << cppCode
        << QByteArray(R"XML(
<typesystem package='Foo'>
    <primitive-type name='int'/>
    <object-type name='Base'/>
    <object-type name='A' exception-handling='auto-on' allow-thread='no'/>
</typesystem>)XML")
         << true << false << true // exception
         << false; // allowthread

    // Activate on base class level
    QTest::newRow("baseclass-on")
        << cppCode
        << QByteArray(R"XML(
<typesystem package='Foo'>
    <primitive-type name='int'/>
    <object-type name='Base' exception-handling='auto-on' allow-thread='no'/>
    <object-type name='A'/>
</typesystem>)XML")
         << true << false << true // exception
         << false; // allowthread

    // Override value on class level
    QTest::newRow("override-class-on")
        << cppCode
        << QByteArray(R"XML(
<typesystem package='Foo'>
    <primitive-type name='int'/>
    <object-type name='Base'/>
    <object-type name='A' exception-handling='auto-on'>
        <modify-function signature='throwing()' exception-handling='no'/>
    </object-type>
</typesystem>)XML")
         << true << false << false // exception
         << false; // allowthread
}

void TestModifyFunction::testScopedModifications()
{
    QFETCH(QByteArray, cppCode);
    QFETCH(QByteArray, xmlCode);
    QFETCH(bool, expectedGenerateUnspecified);
    QFETCH(bool, expectedGenerateNonThrowing);
    QFETCH(bool, expectedGenerateThrowing);
    QFETCH(bool, expectedAllowThread);

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode.constData(), xmlCode.constData(), false));
    QVERIFY(builder);

    const auto classA = AbstractMetaClass::findClass(builder->classes(), u"A");
    QVERIFY(classA);

    auto f = classA->findFunction(QStringLiteral("unspecified"));
    QVERIFY(f);
    QCOMPARE(f->exceptionSpecification(), ExceptionSpecification::Unknown);
    QCOMPARE(f->generateExceptionHandling(), expectedGenerateUnspecified);
    QCOMPARE(f->allowThread(), expectedAllowThread);

    f = classA->findFunction(QStringLiteral("nonThrowing"));
    QVERIFY(f);
    QCOMPARE(f->exceptionSpecification(), ExceptionSpecification::NoExcept);
    QCOMPARE(f->generateExceptionHandling(), expectedGenerateNonThrowing);

    f = classA->findFunction(QStringLiteral("throwing"));
    QVERIFY(f);
    QCOMPARE(f->exceptionSpecification(), ExceptionSpecification::Throws);
    QCOMPARE(f->generateExceptionHandling(), expectedGenerateThrowing);
}

void TestModifyFunction::testSnakeCaseRenaming_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("expected");
    QTest::newRow("s1")
        << QStringLiteral("snakeCaseFunc") << QStringLiteral("snake_case_func");
    QTest::newRow("s2")
        << QStringLiteral("SnakeCaseFunc") << QStringLiteral("snake_case_func");
    QTest::newRow("consecutive-uppercase")
        << QStringLiteral("snakeCAseFunc") << QStringLiteral("snakeCAseFunc");
}

void TestModifyFunction::testSnakeCaseRenaming()
{
    QFETCH(QString, name);
    QFETCH(QString, expected);

    const QString actual = AbstractMetaBuilder::getSnakeCaseName(name);
    QCOMPARE(actual, expected);
}

QTEST_APPLESS_MAIN(TestModifyFunction)
