// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "testmodifydocumentation.h"
#include "testutil.h"
#include <abstractmetalang.h>
#include <abstractmetafunction.h>
#include <documentation.h>
#include <modifications.h>
#include <complextypeentry.h>
#include <qtdocparser.h>

#include <qtcompat.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QTemporaryDir>
#include <QtTest/QTest>

using namespace Qt::StringLiterals;

void TestModifyDocumentation::testModifyDocumentation()
{
    const char cppCode[] = "struct B { void b(); }; class A {};\n";
    const char xmlCode[] =
R"(<typesystem package="Foo">
    <value-type name='B'>
        <modify-function signature='b()' remove='all'/>
    </value-type>
    <value-type name='A'>
    <modify-documentation xpath='description/brief'>&lt;brief>Modified Brief&lt;/brief></modify-documentation>
    <modify-documentation xpath='description/para[3]'>&lt;para>Some changed contents here&lt;/para></modify-documentation>
    </value-type>
</typesystem>
)";
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    const auto classA = AbstractMetaClass::findClass(builder->classes(), u"A");
    QVERIFY(classA);
    DocModificationList docMods = classA->typeEntry()->docModifications();
    QCOMPARE(docMods.size(), 2);
    QCOMPARE(docMods[0].code().trimmed(), u"<brief>Modified Brief</brief>");
    QCOMPARE(docMods[0].signature(), QString());
    QCOMPARE(docMods[1].code().trimmed(), u"<para>Some changed contents here</para>");
    QCOMPARE(docMods[1].signature(), QString());

    // Create a temporary directory for the documentation file since libxml2
    // cannot handle Qt resources.
    QTemporaryDir tempDir(QDir::tempPath() + u"/shiboken_testmodifydocXXXXXX"_s);
    QVERIFY2(tempDir.isValid(), qPrintable(tempDir.errorString()));
    const QString docFileName = u"a.xml"_s;
    QVERIFY(QFile::copy(u":/"_s + docFileName, tempDir.filePath(docFileName)));

    QtDocParser docParser;
    docParser.setDocumentationDataDirectory(tempDir.path());
    docParser.fillDocumentation(classA);

    const Documentation &doc = classA->documentation();
    const QString actualDocSimplified = doc.detailed().simplified();
    const QString actualBriefSimplified = doc.brief().simplified();
    QVERIFY(!actualDocSimplified.isEmpty());

const char expectedDoc[] =
R"(<?xml version="1.0"?>
<description>oi
<para>Paragraph number 1</para>
<para>Paragraph number 2</para>
<para>Some changed contents here</para>
</description>
)";
    const QString expectedDocSimplified = QString::fromLatin1(expectedDoc).simplified();
    // Check whether the first modification worked.
    QVERIFY(actualBriefSimplified.contains(u"Modified Brief"));

#ifndef HAVE_LIBXSLT
    // QtXmlPatterns is unable to handle para[3] in style sheets,
    // this only works in its XPath search.
    QEXPECT_FAIL("", "QtXmlPatterns cannot handle para[3] (QTBUG-66925)", Abort);
#endif
    QCOMPARE(actualDocSimplified, expectedDocSimplified);
}

void TestModifyDocumentation::testInjectAddedFunctionDocumentation()
{
    const char cppCode[] ="class A {};\n";
    const char xmlCode[] = R"XML(
<typesystem package="Foo">
    <value-type name='A'>
        <add-function signature="foo(int@parameter_name@)">
              <inject-documentation format="target" mode="append">
                  Injected documentation of added function foo.
              </inject-documentation>
        </add-function>
    </value-type>
</typesystem>
)XML";
    QScopedPointer<AbstractMetaBuilder> builder(TestUtil::parse(cppCode, xmlCode));
    QVERIFY(builder);
    const auto classA = AbstractMetaClass::findClass(builder->classes(), u"A");
    QVERIFY(classA);
    const auto f = classA->findFunction(u"foo");
    QVERIFY(f);
    QVERIFY(f->isUserAdded());
    auto docMods = f->addedFunctionDocModifications();
    QCOMPARE(docMods.size(), 1);
    const QString code = docMods.constFirst().code();
    QVERIFY(code.contains(u"Injected documentation of added function foo."));
}

// We expand QTEST_MAIN macro but using QCoreApplication instead of QApplication
// because this test needs an event loop but can't use QApplication to avoid a crash
// on our ARMEL/FRAMANTLE buildbot
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestModifyDocumentation tc;
    return QTest::qExec(&tc, argc, argv);
}
