// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "xmlutils_libxslt.h"
#include "xmlutils.h"

#include "qtcompat.h"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QString>

#include <libxslt/xsltutils.h>
#include <libxslt/transform.h>

#include <libxml/xmlsave.h>
#include <libxml/xpath.h>

#include <cstdlib>
#include <memory>

using namespace Qt::StringLiterals;

static void cleanup()
{
    xsltCleanupGlobals();
    xmlCleanupParser();
}

static void ensureInitialized()
{
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        xmlInitParser();
        xsltInit();
        qAddPostRoutine(cleanup);
    }
}

// RAI Helpers for cleaning up libxml2/libxslt data

struct XmlDocDeleter // for std::unique_ptr<xmlDoc>
{
    void operator()(xmlDocPtr xmlDoc) { xmlFreeDoc(xmlDoc); }
};

struct XmlXPathObjectDeleter
{
    void operator()(xmlXPathObjectPtr xPathObject) { xmlXPathFreeObject(xPathObject); }
};

struct XmlStyleSheetDeleter // for std::unique_ptr<xsltStylesheet>
{
    void operator()(xsltStylesheetPtr xslt) { xsltFreeStylesheet(xslt); }
};

struct XmlXPathContextDeleter
{
    void operator()(xmlXPathContextPtr xPathContext) { xmlXPathFreeContext(xPathContext); }
};

using XmlDocUniquePtr = std::unique_ptr<xmlDoc, XmlDocDeleter>;
using XmlPathObjectUniquePtr = std::unique_ptr<xmlXPathObject, XmlXPathObjectDeleter>;
using XmlStyleSheetUniquePtr = std::unique_ptr<xsltStylesheet, XmlStyleSheetDeleter>;
using XmlXPathContextUniquePtr = std::unique_ptr<xmlXPathContext, XmlXPathContextDeleter>;

// Helpers for formatting nodes obtained from XPATH queries

static int qbXmlOutputWriteCallback(void *context, const char *buffer, int len)
{
    static_cast<QByteArray *>(context)->append(buffer, len);
    return len;
}

static int qbXmlOutputCloseCallback(void * /* context */) { return 0; }

static QByteArray formatNode(xmlNodePtr node, QString *errorMessage)
{
    QByteArray result;
    xmlSaveCtxtPtr saveContext =
        xmlSaveToIO(qbXmlOutputWriteCallback, qbXmlOutputCloseCallback,
                    &result, "UTF-8", 0);
    if (!saveContext) {
        *errorMessage = u"xmlSaveToIO() failed."_s;
        return result;
    }
    const long saveResult = xmlSaveTree(saveContext, node);
    xmlSaveClose(saveContext);
    if (saveResult < 0)
        *errorMessage = u"xmlSaveTree() failed."_s;
    return result;
}

// XPath expressions
class LibXmlXQuery : public XQuery
{
public:
    explicit LibXmlXQuery(XmlDocUniquePtr &doc, XmlXPathContextUniquePtr &xpathContext) :
       m_doc(std::move(doc)), m_xpathContext(std::move(xpathContext))
    {
        ensureInitialized();
    }

protected:
    QString doEvaluate(const QString &xPathExpression, QString *errorMessage) override;

private:
    XmlDocUniquePtr m_doc;
    XmlXPathContextUniquePtr m_xpathContext;
};

QString LibXmlXQuery::doEvaluate(const QString &xPathExpression, QString *errorMessage)
{
    const QByteArray xPathExpressionB = xPathExpression.toUtf8();
    auto xPathExpressionX = reinterpret_cast<const xmlChar *>(xPathExpressionB.constData());

    XmlPathObjectUniquePtr xPathObject(xmlXPathEvalExpression(xPathExpressionX, m_xpathContext.get()));
    if (!xPathObject) {
         *errorMessage = u"xmlXPathEvalExpression() failed for \""_s + xPathExpression
                         + u'"';
        return QString();
    }
    QString result;
    if (xmlNodeSetPtr nodeSet = xPathObject->nodesetval) {
        for (int n = 0, count = nodeSet->nodeNr; n < count; ++n) {
            auto node = nodeSet->nodeTab[n];
            if (node->type == XML_ELEMENT_NODE) {
                result += QString::fromLocal8Bit(formatNode(node, errorMessage));
                if (!errorMessage->isEmpty())
                    return QString();
            }
        }
    }
    return result;
}

std::shared_ptr<XQuery> libXml_createXQuery(const QString &focus, QString *errorMessage)
{
    XmlDocUniquePtr doc(xmlParseFile(QFile::encodeName(focus).constData()));
    if (!doc) {
        *errorMessage = u"libxml2: Cannot set focus to "_s + QDir::toNativeSeparators(focus);
        return {};
    }
    XmlXPathContextUniquePtr xpathContext(xmlXPathNewContext(doc.get()));
    if (!xpathContext) {
        *errorMessage = u"libxml2: xmlXPathNewContext() failed"_s;
        return {};
    }
    return std::shared_ptr<XQuery>(new LibXmlXQuery(doc, xpathContext));
}

// XSLT transformation

static const char xsltPrefix[] = R"(<?xml version="1.0" encoding="UTF-8" ?>
    <xsl:transform version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
)";

QString libXslt_transform(const QString &xml, QString xsl, QString *errorMessage)
{
    ensureInitialized();
    // Read XML data
    if (!xsl.startsWith(u"<?xml")) {
        xsl.prepend(QLatin1StringView(xsltPrefix));
        xsl.append(u"</xsl:transform>"_s);
    }
    const QByteArray xmlData = xml.toUtf8();
    XmlDocUniquePtr xmlDoc(xmlParseMemory(xmlData.constData(), xmlData.size()));
    if (!xmlDoc) {
        *errorMessage = u"xmlParseMemory() failed for XML."_s;
        return xml;
    }

    // Read XSL data as a XML file
    const QByteArray xslData = xsl.toUtf8();
    // xsltFreeStylesheet will delete this pointer
    xmlDocPtr xslDoc = xmlParseMemory(xslData.constData(), xslData.size());
    if (!xslDoc) {
        *errorMessage = u"xmlParseMemory() failed for XSL \""_s + xsl + u"\"."_s;
        return xml;
    };

    // Parse XSL data
    XmlStyleSheetUniquePtr xslt(xsltParseStylesheetDoc(xslDoc));
    if (!xslt) {
        *errorMessage = u"xsltParseStylesheetDoc() failed."_s;
        return xml;
    }

    // Apply XSL
    XmlDocUniquePtr xslResult(xsltApplyStylesheet(xslt.get(), xmlDoc.get(), nullptr));
    xmlChar *buffer = nullptr;
    int bufferSize;
    QString result;
    if (xsltSaveResultToString(&buffer, &bufferSize, xslResult.get(), xslt.get()) == 0) {
        result = QString::fromUtf8(reinterpret_cast<char*>(buffer), bufferSize);
        std::free(buffer);
    } else {
        *errorMessage = u"xsltSaveResultToString() failed."_s;
        result = xml;
    }
    return result.trimmed();
}
