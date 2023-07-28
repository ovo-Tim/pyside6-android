// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "testabstractmetaclass.h"
#include "abstractmetabuilder.h"
#include "testutil.h"
#include <abstractmetaargument.h>
#include <abstractmetafunction.h>
#include <abstractmetalang.h>
#include <usingmember.h>
#include <typesystem.h>

#include <qtcompat.h>

#include <QtTest/QTest>

using namespace Qt::StringLiterals;

void TestAbstractMetaClass::testClassName()
{
    const char cppCode[] = "class ClassName {};";
    const char xmlCode[] = R"(<typesystem package="Foo">
    <value-type name="ClassName"/>
</typesystem>)";
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 1);
    QCOMPARE(classes[0]->name(), u"ClassName");
}

void TestAbstractMetaClass::testClassNameUnderNamespace()
{
    const char cppCode[] = "namespace Namespace { class ClassName {}; }\n";
    const char xmlCode[] = R"XML(
    <typesystem package="Foo">
        <namespace-type name="Namespace">
            <value-type name="ClassName"/>
        </namespace-type>
    </typesystem>)XML";
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 2); // 1 namespace + 1 class
    if (classes.constFirst()->name() != u"ClassName")
        qSwap(classes[0], classes[1]);

    QCOMPARE(classes[0]->name(), u"ClassName");
    QCOMPARE(classes[0]->qualifiedCppName(), u"Namespace::ClassName");
    QCOMPARE(classes[1]->name(), u"Namespace");
    QVERIFY(classes[1]->isNamespace());

    // Check ctors info
    QVERIFY(classes[0]->hasConstructors());
    QCOMPARE(classes[0]->functions().size(), 2); // default ctor + copy ctor

    auto ctors = classes[0]->queryFunctions(FunctionQueryOption::AnyConstructor);
    QCOMPARE(ctors.size(), 2);
    if (ctors.constFirst()->minimalSignature() != u"ClassName()")
        qSwap(ctors[0], ctors[1]);

    QCOMPARE(ctors[0]->arguments().size(), 0);
    QCOMPARE(ctors[0]->minimalSignature(), u"ClassName()");
    QCOMPARE(ctors[1]->arguments().size(), 1);
    QCOMPARE(ctors[1]->minimalSignature(), u"ClassName(Namespace::ClassName)");

    QVERIFY(!classes[0]->hasPrivateDestructor());
    QVERIFY(classes[0]->isCopyConstructible()); // implicit default copy ctor

    // This method is buggy and nobody wants to fix it or needs it fixed :-/
    // QVERIFY(classes[0]->hasNonPrivateConstructor());
}

static AbstractMetaFunctionCList virtualFunctions(const AbstractMetaClassCPtr &c)
{
    AbstractMetaFunctionCList result;
    const auto &functions = c->functions();
    for (const auto &f : functions) {
        if (f->isVirtual())
            result.append(f);
    }
    return result;
}

void TestAbstractMetaClass::testVirtualMethods()
{
    const char cppCode[] =R"CPP(
class A {
public:
    virtual int pureVirtual() const = 0;
};
class B : public A {};
class C : public B {
public:
    int pureVirtual() const override { return 0; }
};
class F final : public C {
public:
    int pureVirtual() const final { return 1; }
};
)CPP";

    const char xmlCode[] = R"XML(
<typesystem package="Foo">
    <primitive-type name='int'/>
    <object-type name='A'/>
    <object-type name='B'/>
    <object-type name='C'/>
    <object-type name='F'/>
</typesystem>
)XML";
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 4);
    const auto a = AbstractMetaClass::findClass(classes, u"A");
    const auto b = AbstractMetaClass::findClass(classes, u"B");
    const auto c = AbstractMetaClass::findClass(classes, u"C");
    const auto f = AbstractMetaClass::findClass(classes, u"F");
    QVERIFY(f);

    QCOMPARE(a->baseClass(), nullptr);
    QCOMPARE(b->baseClass(), a);
    QCOMPARE(c->baseClass(), b);
    QCOMPARE(f->baseClass(), c);

    QCOMPARE(a->functions().size(), 2); // default ctor + the pure virtual method
    QCOMPARE(b->functions().size(), 2);
    QCOMPARE(c->functions().size(), 2);
    QCOMPARE(f->functions().size(), 2);
    QVERIFY(f->attributes().testFlag(AbstractMetaClass::FinalCppClass));

    // implementing class, ownclass, declaringclass
    const auto ctorA = a->queryFunctions(FunctionQueryOption::Constructors).constFirst();
    const auto ctorB = b->queryFunctions(FunctionQueryOption::Constructors).constFirst();
    const auto ctorC = c->queryFunctions(FunctionQueryOption::Constructors).constFirst();
    QVERIFY(ctorA->isConstructor());
    QVERIFY(!ctorA->isVirtual());
    QVERIFY(ctorB->isConstructor());
    QVERIFY(!ctorB->isVirtual());
    QVERIFY(ctorC->isConstructor());
    QVERIFY(!ctorC->isVirtual());
    QCOMPARE(ctorA->implementingClass(), a);
    QCOMPARE(ctorA->ownerClass(), a);
    QCOMPARE(ctorA->declaringClass(), a);

    const auto virtualFunctionsA = virtualFunctions(a);
    const auto virtualFunctionsB = virtualFunctions(b);
    const auto virtualFunctionsC = virtualFunctions(c);
    const auto virtualFunctionsF = virtualFunctions(f);
    QCOMPARE(virtualFunctionsA.size(), 1); // Add a pureVirtualMethods method !?
    QCOMPARE(virtualFunctionsB.size(), 1);
    QCOMPARE(virtualFunctionsC.size(), 1);
    QCOMPARE(virtualFunctionsF.size(), 1);

    const auto funcA = virtualFunctionsA.constFirst();
    const auto funcB = virtualFunctionsB.constFirst();
    const auto funcC = virtualFunctionsC.constFirst();
    const auto funcF = virtualFunctionsF.constFirst();

    QCOMPARE(funcA->ownerClass(), a);
    QVERIFY(funcC->attributes().testFlag(AbstractMetaFunction::VirtualCppMethod));
    QCOMPARE(funcB->ownerClass(), b);
    QCOMPARE(funcC->ownerClass(), c);
    QVERIFY(funcC->attributes().testFlag(AbstractMetaFunction::OverriddenCppMethod));
    QVERIFY(funcF->attributes().testFlag(AbstractMetaFunction::FinalCppMethod));

    QCOMPARE(funcA->declaringClass(), a);
    QCOMPARE(funcB->declaringClass(), a);
    QCOMPARE(funcC->declaringClass(), a);

    // The next two tests could return null, because it makes more sense.
    // But we have too many code written relying on this behaviour where
    // implementingClass is equals to declaringClass on pure virtual functions
    QCOMPARE(funcA->implementingClass(), a);
    QCOMPARE(funcB->implementingClass(), a);
    QCOMPARE(funcC->implementingClass(), c);
}

void TestAbstractMetaClass::testVirtualBase()
{
    const char cppCode[] =R"CPP(
class Base {
public:
    virtual ~Base() = default;
};
class Derived : public Base {};
)CPP";

    const char xmlCode[] = R"XML(
<typesystem package="Foo">
    <object-type name='Base'/>
    <object-type name='Derived'/>
</typesystem>
)XML";
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    const auto base = AbstractMetaClass::findClass(classes, u"Base");
    QVERIFY(base);
    QVERIFY(base->isPolymorphic());
    const auto derived = AbstractMetaClass::findClass(classes, u"Derived");
    QVERIFY(derived);
    QVERIFY(derived->isPolymorphic());
}

void TestAbstractMetaClass::testDefaultValues()
{
    const char cppCode[] = "\
    struct A {\n\
        class B {};\n\
        void method(B b = B());\n\
    };\n";
    const char xmlCode[] = R"XML(
    <typesystem package="Foo">
        <value-type name='A'>
            <value-type name='B'/>
        </value-type>
    </typesystem>)XML";
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 2);
    const auto classA = AbstractMetaClass::findClass(classes, u"A");
    const auto candidates = classA->queryFunctionsByName(u"method"_s);
    QCOMPARE(candidates.size(), 1);
    const auto &method = candidates.constFirst();
    const AbstractMetaArgument &arg = method->arguments().constFirst();
    QCOMPARE(arg.defaultValueExpression(), arg.originalDefaultValueExpression());
}

void TestAbstractMetaClass::testModifiedDefaultValues()
{
    const char cppCode[] = "\
    struct A {\n\
        class B {};\n\
        void method(B b = B());\n\
    };\n";
    const char xmlCode[] = R"XML(
    <typesystem package="Foo">
        <value-type name='A'>
            <modify-function signature='method(A::B)'>
                <modify-argument index='1'>
                    <replace-default-expression with='Hello'/>
                </modify-argument>
            </modify-function>
            <value-type name='B'/>
        </value-type>
    </typesystem>)XML";
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 2);
    const auto classA = AbstractMetaClass::findClass(classes, u"A");
    const auto methodMatches = classA->queryFunctionsByName(u"method"_s);
    QCOMPARE(methodMatches.size(), 1);
    const auto method = methodMatches.constFirst();
    const AbstractMetaArgument &arg = method->arguments().constFirst();
    QCOMPARE(arg.defaultValueExpression(), u"Hello");
    QCOMPARE(arg.originalDefaultValueExpression(), u"A::B()");
}

void TestAbstractMetaClass::testInnerClassOfAPolymorphicOne()
{
    const char cppCode[] = "\
    struct A {\n\
        class B {};\n\
        virtual void method();\n\
    };\n";
    const char xmlCode[] = R"XML(
    <typesystem package="Foo">
        <object-type name='A'>
            <value-type name='B'/>
        </object-type>
    </typesystem>)XML";
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 2);
    const auto classA = AbstractMetaClass::findClass(classes, u"A");
    QVERIFY(classA);
    QVERIFY(classA->isPolymorphic());
    const auto classB = AbstractMetaClass::findClass(classes, u"A::B");
    QVERIFY(classB);
    QVERIFY(!classB->isPolymorphic());
}

void TestAbstractMetaClass::testForwardDeclaredInnerClass()
{
    const char cppCode[] ="\
    class A {\n\
        class B;\n\
    };\n\
    class A::B {\n\
    public:\n\
        void foo();\n\
    };\n";
    const char xmlCode[] = R"XML(
    <typesystem package="Foo">
        <value-type name='A'>
            <value-type name='B'/>
        </value-type>
    </typesystem>)XML";
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 2);
    const auto classA = AbstractMetaClass::findClass(classes, u"A");
    QVERIFY(classA);
    const auto classB = AbstractMetaClass::findClass(classes, u"A::B");
    QVERIFY(classB);
    const auto fooF = classB->findFunction(u"foo");
    QVERIFY(fooF);
}

void TestAbstractMetaClass::testSpecialFunctions()
{
    const char cppCode[] ="\
    struct A {\n\
        A();\n\
        A(const A&);\n\
        A &operator=(const A&);\n\
    };\n\
    struct B {\n\
        B();\n\
        B(const B &);\n\
        B &operator=(B);\n\
    };\n";
    const char xmlCode[] = "\
    <typesystem package=\"Foo\">\n\
        <object-type name='A'/>\n\
        <object-type name='B'/>\n\
    </typesystem>\n";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 2);

    const auto classA = AbstractMetaClass::findClass(classes, u"A");
    QVERIFY(classA);
    auto ctors = classA->queryFunctions(FunctionQueryOption::AnyConstructor);
    QCOMPARE(ctors.size(), 2);
    QCOMPARE(ctors.constFirst()->functionType(), AbstractMetaFunction::ConstructorFunction);
    QCOMPARE(ctors.at(1)->functionType(), AbstractMetaFunction::CopyConstructorFunction);
    auto assigmentOps = classA->queryFunctionsByName(u"operator="_s);
    QCOMPARE(assigmentOps.size(), 1);
    QCOMPARE(assigmentOps.constFirst()->functionType(),
             AbstractMetaFunction::AssignmentOperatorFunction);

    const auto classB = AbstractMetaClass::findClass(classes, u"B");
    QVERIFY(classB);
    ctors = classB->queryFunctions(FunctionQueryOption::AnyConstructor);
    QCOMPARE(ctors.size(), 2);
    QCOMPARE(ctors.constFirst()->functionType(), AbstractMetaFunction::ConstructorFunction);
    QCOMPARE(ctors.at(1)->functionType(), AbstractMetaFunction::CopyConstructorFunction);
    assigmentOps = classA->queryFunctionsByName(u"operator="_s);
    QCOMPARE(assigmentOps.size(), 1);
    QCOMPARE(assigmentOps.constFirst()->functionType(), AbstractMetaFunction::AssignmentOperatorFunction);
}

void TestAbstractMetaClass::testClassDefaultConstructors()
{
    const char cppCode[] = "\
    struct A {};\n\
    \n\
    struct B {\n\
        B();\n\
    private: \n\
        B(const B&);\n\
    };\n\
    \n\
    struct C {\n\
        C(const C&);\n\
    };\n\
    \n\
    struct D {\n\
    private: \n\
        D(const D&);\n\
    };\n\
    \n\
    struct E {\n\
    private: \n\
        ~E();\n\
    };\n\
    \n\
    struct F {\n\
        F(int, int);\n\
    };\n";
    const char xmlCode[] = "\
    <typesystem package='Foo'>\n\
        <primitive-type name='int'/>\n\
        <value-type name='A'/>\n\
        <object-type name='B'/>\n\
        <value-type name='C'/>\n\
        <object-type name='D'/>\n\
        <object-type name='E'/>\n\
        <value-type name='F'/>\n\
    </typesystem>\n";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 6);

    const auto classA = AbstractMetaClass::findClass(classes, u"A");
    QVERIFY(classA);
    QCOMPARE(classA->functions().size(), 2);

    auto ctors = classA->queryFunctions(FunctionQueryOption::AnyConstructor);
    QCOMPARE(ctors.size(), 2);
    if (ctors.constFirst()->minimalSignature() != u"A()")
        qSwap(ctors[0], ctors[1]);

    QCOMPARE(ctors[0]->arguments().size(), 0);
    QCOMPARE(ctors[0]->minimalSignature(), u"A()");
    QCOMPARE(ctors[1]->arguments().size(), 1);
    QCOMPARE(ctors[1]->minimalSignature(), u"A(A)");

    const auto classB = AbstractMetaClass::findClass(classes, u"B");
    QVERIFY(classB);
    QCOMPARE(classB->functions().size(), 2);
    QCOMPARE(classB->functions().constFirst()->minimalSignature(), u"B()");

    const auto classC = AbstractMetaClass::findClass(classes, u"C");
    QVERIFY(classC);
    QCOMPARE(classC->functions().size(), 1);
    QCOMPARE(classC->functions().constFirst()->minimalSignature(), u"C(C)");

    const auto classD = AbstractMetaClass::findClass(classes, u"D");
    QVERIFY(classD);
    QCOMPARE(classD->functions().size(), 1);
    QCOMPARE(classD->functions().constFirst()->minimalSignature(), u"D(D)");
    QVERIFY(classD->functions().constFirst()->isPrivate());

    const auto classE = AbstractMetaClass::findClass(classes, u"E");
    QVERIFY(classE);
    QVERIFY(classE->hasPrivateDestructor());
    QCOMPARE(classE->functions().size(), 0);

    const auto classF = AbstractMetaClass::findClass(classes, u"F");
    QVERIFY(classF);

    ctors = classF->queryFunctions(FunctionQueryOption::AnyConstructor);
    QCOMPARE(ctors.size(), 2);
    if (ctors.constFirst()->minimalSignature() != u"F(int,int)")
        qSwap(ctors[0], ctors[1]);

    QCOMPARE(ctors[0]->arguments().size(), 2);
    QCOMPARE(ctors[0]->minimalSignature(), u"F(int,int)");
    QCOMPARE(ctors[1]->arguments().size(), 1);
    QCOMPARE(ctors[1]->minimalSignature(), u"F(F)");
}

void TestAbstractMetaClass::testClassInheritedDefaultConstructors()
{
    const char cppCode[] = "\
    struct A {\n\
        A();\n\
    private: \n\
        A(const A&);\n\
    };\n\
    struct B : public A {};\n";
    const char xmlCode[] = "\
    <typesystem package='Foo'>\n\
        <object-type name='A'/>\n\
        <object-type name='B'/>\n\
    </typesystem>\n";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 2);
    const auto classA = AbstractMetaClass::findClass(classes, u"A");
    QVERIFY(classA);

    auto ctors = classA->queryFunctions(FunctionQueryOption::AnyConstructor);
    QCOMPARE(ctors.size(), 2);
    if (ctors.constFirst()->minimalSignature() != u"A()")
        qSwap(ctors[0], ctors[1]);

    QCOMPARE(ctors[0]->arguments().size(), 0);
    QCOMPARE(ctors[0]->minimalSignature(), u"A()");
    QCOMPARE(ctors[1]->arguments().size(), 1);
    QCOMPARE(ctors[1]->minimalSignature(), u"A(A)");
    QVERIFY(ctors[1]->isPrivate());

    const auto classB = AbstractMetaClass::findClass(classes, u"B");
    QVERIFY(classB);

    ctors = classB->queryFunctions(FunctionQueryOption::Constructors);
    QCOMPARE(ctors.size(), 1);
    QCOMPARE(ctors.constFirst()->arguments().size(), 0);
    QCOMPARE(ctors.constFirst()->minimalSignature(), u"B()");
}

void TestAbstractMetaClass::testAbstractClassDefaultConstructors()
{
    const char cppCode[] = "\
    struct A {\n\
        virtual void method() = 0;\n\
    };\n";
    const char xmlCode[] = "\
    <typesystem package='Foo'>\n\
        <object-type name='A'/>\n\
    </typesystem>\n";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 1);
    const auto classA = AbstractMetaClass::findClass(classes, u"A");
    QVERIFY(classA);

    const auto ctors = classA->queryFunctions(FunctionQueryOption::Constructors);
    QCOMPARE(ctors.size(), 1);
    QCOMPARE(ctors.constFirst()->arguments().size(), 0);
    QCOMPARE(ctors.constFirst()->minimalSignature(), u"A()");
}

void TestAbstractMetaClass::testObjectTypesMustNotHaveCopyConstructors()
{
    const char cppCode[] = "struct A {};\n";
    const char xmlCode[] = "\
    <typesystem package='Foo'>\n\
        <object-type name='A'/>\n\
    </typesystem>\n";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 1);
    const auto classA = AbstractMetaClass::findClass(classes, u"A");
    QVERIFY(classA);

    const auto ctors = classA->queryFunctions(FunctionQueryOption::Constructors);
    QCOMPARE(ctors.size(), 1);
    QCOMPARE(ctors.constFirst()->arguments().size(), 0);
    QCOMPARE(ctors.constFirst()->minimalSignature(), u"A()");
}

void TestAbstractMetaClass::testIsPolymorphic()
{
    const char cppCode[] = "\
    class A\n\
    {\n\
    public:\n\
        A();\n\
        inline bool abc() const { return false; }\n\
    };\n\
    \n\
    class B : public A\n\
    {\n\
    public:\n\
        B();\n\
        inline bool abc() const { return false; }\n\
    };\n";
    const char xmlCode[] = "\
    <typesystem package='Foo'>\n\
        <primitive-type name='bool'/>\n\
        <value-type name='A'/>\n\
        <value-type name='B'/>\n\
    </typesystem>\n";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 2);
    const auto b = AbstractMetaClass::findClass(classes, u"A");

    QVERIFY(!b->isPolymorphic());
    const auto a = AbstractMetaClass::findClass(classes, u"B");
    QVERIFY(!a->isPolymorphic());
}

void TestAbstractMetaClass::testClassTypedefedBaseClass()
{
    const char cppCode[] =R"CPP(
class Base {
};

using BaseAlias1 = Base;
using BaseAlias2 = BaseAlias1;

class Derived : public BaseAlias2 {
};
)CPP";
    const char xmlCode[] = R"XML(
<typesystem package='Foo'>
    <object-type name='Base'/>
    <object-type name='Derived'/>
</typesystem>
)XML";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 2);
    const auto base = AbstractMetaClass::findClass(classes, u"Base");
    QVERIFY(base);
    const auto derived = AbstractMetaClass::findClass(classes, u"Derived");
    QVERIFY(derived);
    QCOMPARE(derived->baseClasses().value(0), base);
}

void TestAbstractMetaClass::testFreeOperators_data()
{
    QTest::addColumn<QByteArray>("code");

    const QByteArray classHeader(R"CPP(
class Value
{
public:
    Value(int v) : m_value(v) {}
    int value() const { return m_value; }
)CPP");

    const QByteArray classFooter(R"CPP(
private:
    int m_value;
};
)CPP");

    const QByteArray multOperatorSignature("Value operator*(const Value &v1, const Value &v2)");
    const QByteArray multOperatorBody("{ return Value(v1.value() * v2.value()); }");
    const QByteArray multOperator =  multOperatorSignature + '\n' + multOperatorBody;

    QTest::newRow("free")
        << (classHeader + classFooter + "\ninline " + multOperator);
    QTest::newRow("free-friend-declared")
        << (classHeader + "\n    friend " + multOperatorSignature + ";\n" + classFooter
            + "\ninline " + multOperator);
    QTest::newRow("hidden friend")
        << (classHeader + "    friend inline " + multOperator + classFooter);
};

void TestAbstractMetaClass::testFreeOperators()
{
    QFETCH(QByteArray, code);
    const char  xmlCode[] = R"XML(
    <typesystem package="Foo">
        <primitive-type name="int"/>
        <value-type name="Value"/>
    </typesystem>)XML";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(code.constData(), xmlCode));
    QVERIFY(builder);
    const auto classes = builder->classes();
    QCOMPARE(classes.size(), 1);
    QVERIFY(classes.constFirst()->hasArithmeticOperatorOverload());
    FunctionQueryOptions opts(FunctionQueryOption::OperatorOverloads);
    QCOMPARE(classes.constFirst()->queryFunctions(opts).size(), 1);
}

void TestAbstractMetaClass::testUsingMembers()
{
    const char cppCode[] =R"CPP(
class Base {
public:
    explicit Base(int);

protected:
    void member();
};

class Derived : public Base {
public:
    using Base::Base;
    using Base::member;
};
)CPP";
    const char xmlCode[] = R"XML(
<typesystem package='Foo'>
    <primitive-type name='int'/>
    <object-type name='Base'/>
    <object-type name='Derived'/>
</typesystem>
)XML";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    QCOMPARE(classes.size(), 2);
    const auto base = AbstractMetaClass::findClass(classes, u"Base");
    QVERIFY(base);
    const auto derived = AbstractMetaClass::findClass(classes, u"Derived");
    QVERIFY(derived);
    const auto usingMembers = derived->usingMembers();
    QCOMPARE(usingMembers.size(), 2);
    for (const auto &um : usingMembers) {
        QCOMPARE(um.access, Access::Public);
        QCOMPARE(um.baseClass, base);
        QVERIFY(um.memberName == u"Base" || um.memberName == u"member");
    }
}

void TestAbstractMetaClass::testUsingTemplateMembers_data()
{
    const QByteArray cppCode(R"CPP(
struct Value {
   int value = 0;
};

template <class T> class List {
public:
    List();
    void append(const T &t);
};

class ValueList : public List<Value> {
public:
   void append(const Value &v1, const Value &v2);
)CPP");

    QTest::addColumn<QByteArray>("code");
    QTest::newRow("simple")
        << (cppCode + "using List::append;\n};\n");
    QTest::newRow("with-template-params")
        << (cppCode + "using List<Value>::append;\n};\n");
}

void TestAbstractMetaClass::testUsingTemplateMembers()
{
    QFETCH(QByteArray, code);

    const char xmlCode[] = R"XML(
<typesystem package='Foo'>
    <primitive-type name='int'/>
    <value-type name='Value'/>
    <container-type name='List' type='list'/>
    <value-type name='ValueList'/>
</typesystem>
)XML";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(code.constData(), xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    const auto valueList = AbstractMetaClass::findClass(classes, u"ValueList");
    QVERIFY(valueList);
    auto list = valueList->templateBaseClass();
    QVERIFY(valueList->isUsingMember(list, u"append"_s, Access::Public));
    QCOMPARE(valueList->queryFunctionsByName(u"append"_s).size(), 2);
}

void TestAbstractMetaClass::testGenerateFunctions()
{
    const char cppCode[] = R"CPP(
class TestClass {
public:
    TestClass();

    void alpha(int);
    void beta(int);
    void beta(double);
    void gamma(int);
};
)CPP";

    const char xmlCode[] = R"XML(
<typesystem package='Foo'>
    <object-type name='TestClass' generate-functions='beta(double);gamma'/>
</typesystem>
)XML";

    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    AbstractMetaClassList classes = builder->classes();
    const auto tc = AbstractMetaClass::findClass(classes, u"TestClass");
    // Verify that the constructor and 2 functions are generated.
    const auto &functions = tc->functions();
    QCOMPARE(functions.size(), 5);
    const auto generateCount =
        std::count_if(functions.cbegin(), functions.cend(),
                      [](const auto &af) { return af->generateBinding(); });
    QCOMPARE(generateCount, 3);
}

QTEST_APPLESS_MAIN(TestAbstractMetaClass)
