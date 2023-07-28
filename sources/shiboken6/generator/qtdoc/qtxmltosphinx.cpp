// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtxmltosphinx.h"
#include "exception.h"
#include "qtxmltosphinxinterface.h"
#include <codesniphelpers.h>
#include "rstformat.h"

#include "qtcompat.h"

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QLoggingCategory>
#include <QtCore/QRegularExpression>
#include <QtCore/QXmlStreamReader>

using namespace Qt::StringLiterals;

static inline QString nameAttribute() { return QStringLiteral("name"); }
static inline QString titleAttribute() { return QStringLiteral("title"); }
static inline QString fullTitleAttribute() { return QStringLiteral("fulltitle"); }

QString msgTagWarning(const QXmlStreamReader &reader, const QString &context,
                      const QString &tag, const QString &message)
{
    QString result;
    QTextStream str(&result);
    str << "While handling <";
    const auto currentTag = reader.name();
    if (currentTag.isEmpty())
        str << tag;
    else
        str << currentTag;
    str << "> in " << context << ", line "<< reader.lineNumber()
        << ": " << message;
    return result;
}

QString msgFallbackWarning(const QString &location, const QString &identifier,
                           const QString &fallback)
{
    QString message = u"Falling back to \""_s
        + QDir::toNativeSeparators(fallback) + u"\" for \""_s
        + location + u'"';
    if (!identifier.isEmpty())
        message += u" ["_s + identifier + u']';
    return message;
}

QString msgSnippetsResolveError(const QString &path, const QStringList &locations)
{
    QString result;
    QTextStream(&result) << "Could not resolve \"" << path << R"(" in ")"
        << locations.join(uR"(", ")"_s);
    return result;
}

static bool isHttpLink(const QString &ref)
{
    return ref.startsWith(u"http://") || ref.startsWith(u"https://");
}

static QString trimRight(QString s)
{
    while (!s.isEmpty() && s.crbegin()->isSpace())
        s.chop(1);
    return s;
}

static QString trimLeadingNewlines(QString s)
{
    while (!s.isEmpty() && s.at(0) == u'\n')
        s.remove(0, 1);
    return s;
}

QDebug operator<<(QDebug d, const QtXmlToSphinxLink &l)
{
    static const QHash<QtXmlToSphinxLink::Type, const char *> typeName = {
        {QtXmlToSphinxLink::Method, "Method"},
        {QtXmlToSphinxLink::Function, "Function"},
        {QtXmlToSphinxLink::Class, "Class"},
        {QtXmlToSphinxLink::Attribute, "Attribute"},
        {QtXmlToSphinxLink::Module, "Module"},
        {QtXmlToSphinxLink::Reference, "Reference"},
        {QtXmlToSphinxLink::External, "External"},
    };

    QDebugStateSaver saver(d);
    d.noquote();
    d.nospace();
    d << "QtXmlToSphinxLinkContext(" << typeName.value(l.type, "") << ", ref=\""
      << l.linkRef << '"';
    if (!l.linkText.isEmpty())
        d << ", text=\"" << l.linkText << '"';
    d << ')';
    return d;
}

QDebug operator<<(QDebug debug, const QtXmlToSphinx::TableCell &c)
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();
    debug << "Cell(\"" << c.data << '"';
    if (c.colSpan != 0)
        debug << ", colSpan=" << c.colSpan;
    if (c.rowSpan != 0)
        debug << ", rowSpan=" << c.rowSpan;
    debug << ')';
    return debug;
}

QDebug operator<<(QDebug debug, const QtXmlToSphinx::Table &t)
{
    QDebugStateSaver saver(debug);
    debug.noquote();
    debug.nospace();
    t.formatDebug(debug);
    return debug;
}

static const char *linkKeyWord(QtXmlToSphinxLink::Type type)
{
    switch (type) {
    case QtXmlToSphinxLink::Method:
        return ":meth:";
    case QtXmlToSphinxLink::Function:
        return ":func:";
    case QtXmlToSphinxLink::Class:
        return ":class:";
    case QtXmlToSphinxLink::Attribute:
        return ":attr:";
    case QtXmlToSphinxLink::Module:
        return ":mod:";
    case QtXmlToSphinxLink::Reference:
        return ":ref:";
    case QtXmlToSphinxLink::External:
        break;
    case QtXmlToSphinxLink::FunctionMask:
        break;
     }
    return "";
}

TextStream &operator<<(TextStream &str, const QtXmlToSphinxLink &linkContext)
{
    // Temporarily turn off bold/italic since links do not work within
    if (linkContext.flags & QtXmlToSphinxLink::InsideBold)
        str << "**";
    else if (linkContext.flags & QtXmlToSphinxLink::InsideItalic)
        str << '*';
    str << ' ' << linkKeyWord(linkContext.type) << '`';
    const bool isExternal = linkContext.type == QtXmlToSphinxLink::External;
    if (!linkContext.linkText.isEmpty()) {
        writeEscapedRstText(str, linkContext.linkText);
        if (isExternal && !linkContext.linkText.endsWith(u' '))
            str << ' ';
        str << '<';
    }
    // Convert page titles to RST labels
    str << (linkContext.type == QtXmlToSphinxLink::Reference
        ? toRstLabel(linkContext.linkRef) : linkContext.linkRef);
    if (!linkContext.linkText.isEmpty())
        str << '>';
    str << '`';
    if (isExternal)
        str << '_';
    str << ' ';
    if (linkContext.flags & QtXmlToSphinxLink::InsideBold)
        str << "**";
    else if (linkContext.flags & QtXmlToSphinxLink::InsideItalic)
        str << '*';
    return str;
}

enum class WebXmlTag {
    Unknown,
    heading, brief, para, italic, bold, see_also, snippet, dots, codeline,
    table, header, row, item, argument, teletype, link, inlineimage, image,
    list, term, raw, underline, superscript, code, badcode, legalese,
    rst, section, quotefile,
    // ignored tags
    generatedlist, tableofcontents, quotefromfile, skipto, target, page, group,
    // useless tags
    description, definition, printuntil, relation,
    // Doxygen tags
    title, ref, computeroutput, detaileddescription, name, listitem,
    parametername, parameteritem, ulink, itemizedlist, parameternamelist,
    parameterlist,
    // Doxygen ignored tags
    highlight, linebreak, programlisting, xreftitle, sp, entry, simplesect,
    verbatim, xrefsect, xrefdescription,
};

using WebXmlTagHash = QHash<QStringView, WebXmlTag>;

static const WebXmlTagHash &webXmlTagHash()
{
    static const WebXmlTagHash result = {
        {u"heading", WebXmlTag::heading},
        {u"brief", WebXmlTag::brief},
        {u"para", WebXmlTag::para},
        {u"italic", WebXmlTag::italic},
        {u"bold", WebXmlTag::bold},
        {u"see-also", WebXmlTag::see_also},
        {u"snippet", WebXmlTag::snippet},
        {u"dots", WebXmlTag::dots},
        {u"codeline", WebXmlTag::codeline},
        {u"table", WebXmlTag::table},
        {u"header", WebXmlTag::header},
        {u"row", WebXmlTag::row},
        {u"item", WebXmlTag::item},
        {u"argument", WebXmlTag::argument},
        {u"teletype", WebXmlTag::teletype},
        {u"link", WebXmlTag::link},
        {u"inlineimage", WebXmlTag::inlineimage},
        {u"image", WebXmlTag::image},
        {u"list", WebXmlTag::list},
        {u"term", WebXmlTag::term},
        {u"raw", WebXmlTag::raw},
        {u"underline", WebXmlTag::underline},
        {u"superscript", WebXmlTag::superscript},
        {u"code", WebXmlTag::code},
        {u"badcode", WebXmlTag::badcode},
        {u"legalese", WebXmlTag::legalese},
        {u"rst", WebXmlTag::rst},
        {u"section", WebXmlTag::section},
        {u"quotefile", WebXmlTag::quotefile},
        {u"generatedlist", WebXmlTag::generatedlist},
        {u"tableofcontents", WebXmlTag::tableofcontents},
        {u"quotefromfile", WebXmlTag::quotefromfile},
        {u"skipto", WebXmlTag::skipto},
        {u"target", WebXmlTag::target},
        {u"page", WebXmlTag::page},
        {u"group", WebXmlTag::group},
        {u"description", WebXmlTag::description},
        {u"definition", WebXmlTag::definition},
        {u"printuntil", WebXmlTag::printuntil},
        {u"relation", WebXmlTag::relation},
        {u"title", WebXmlTag::title},
        {u"ref", WebXmlTag::ref},
        {u"computeroutput", WebXmlTag::computeroutput},
        {u"detaileddescription", WebXmlTag::detaileddescription},
        {u"name", WebXmlTag::name},
        {u"listitem", WebXmlTag::listitem},
        {u"parametername", WebXmlTag::parametername},
        {u"parameteritem", WebXmlTag::parameteritem},
        {u"ulink", WebXmlTag::ulink},
        {u"itemizedlist", WebXmlTag::itemizedlist},
        {u"parameternamelist", WebXmlTag::parameternamelist},
        {u"parameterlist", WebXmlTag::parameterlist},
        {u"highlight", WebXmlTag::highlight},
        {u"linebreak", WebXmlTag::linebreak},
        {u"programlisting", WebXmlTag::programlisting},
        {u"xreftitle", WebXmlTag::xreftitle},
        {u"sp", WebXmlTag::sp},
        {u"entry", WebXmlTag::entry},
        {u"simplesect", WebXmlTag::simplesect},
        {u"verbatim", WebXmlTag::verbatim},
        {u"xrefsect", WebXmlTag::xrefsect},
        {u"xrefdescription", WebXmlTag::xrefdescription},
    };
    return result;
}

QtXmlToSphinx::QtXmlToSphinx(const QtXmlToSphinxDocGeneratorInterface *docGenerator,
                             const QtXmlToSphinxParameters &parameters,
                             const QString& doc, const QString& context)
        : m_output(static_cast<QString *>(nullptr)),
          m_context(context),
          m_generator(docGenerator), m_parameters(parameters)
{
    m_result = transform(doc);
}

QtXmlToSphinx::~QtXmlToSphinx() = default;

void QtXmlToSphinx::callHandler(WebXmlTag t, QXmlStreamReader &r)
{
    switch (t) {
    case WebXmlTag::heading:
        handleHeadingTag(r);
        break;
    case WebXmlTag::brief:
    case WebXmlTag::para:
        handleParaTag(r);
        break;
    case WebXmlTag::italic:
        handleItalicTag(r);
        break;
    case WebXmlTag::bold:
        handleBoldTag(r);
        break;
    case WebXmlTag::see_also:
        handleSeeAlsoTag(r);
        break;
    case WebXmlTag::snippet:
        handleSnippetTag(r);
        break;
    case WebXmlTag::dots:
    case WebXmlTag::codeline:
        handleDotsTag(r);
        break;
    case WebXmlTag::table:
        handleTableTag(r);
        break;
    case WebXmlTag::header:
        handleHeaderTag(r);
        break;
    case WebXmlTag::row:
        handleRowTag(r);
        break;
    case WebXmlTag::item:
        handleItemTag(r);
        break;
    case WebXmlTag::argument:
        handleArgumentTag(r);
        break;
    case WebXmlTag::teletype:
        handleArgumentTag(r);
        break;
    case WebXmlTag::link:
        handleLinkTag(r);
        break;
    case WebXmlTag::inlineimage:
        handleInlineImageTag(r);
        break;
    case WebXmlTag::image:
        handleImageTag(r);
        break;
    case WebXmlTag::list:
        handleListTag(r);
        break;
    case WebXmlTag::term:
        handleTermTag(r);
        break;
    case WebXmlTag::raw:
        handleRawTag(r);
        break;
    case WebXmlTag::underline:
        handleItalicTag(r);
        break;
    case WebXmlTag::superscript:
        handleSuperScriptTag(r);
        break;
    case WebXmlTag::code:
    case WebXmlTag::badcode:
    case WebXmlTag::legalese:
        handleCodeTag(r);
        break;
    case WebXmlTag::rst:
        handleRstPassTroughTag(r);
        break;
    case WebXmlTag::section:
        handleAnchorTag(r);
        break;
    case WebXmlTag::quotefile:
        handleQuoteFileTag(r);
        break;
    case WebXmlTag::generatedlist:
    case WebXmlTag::tableofcontents:
    case WebXmlTag::quotefromfile:
    case WebXmlTag::skipto:
        handleIgnoredTag(r);
        break;
    case WebXmlTag::target:
        handleTargetTag(r);
        break;
    case WebXmlTag::page:
    case WebXmlTag::group:
        handlePageTag(r);
        break;
    case WebXmlTag::description:
    case WebXmlTag::definition:
    case WebXmlTag::printuntil:
    case WebXmlTag::relation:
        handleUselessTag(r);
        break;
    case WebXmlTag::title:
        handleHeadingTag(r);
        break;
    case WebXmlTag::ref:
    case WebXmlTag::computeroutput:
    case WebXmlTag::detaileddescription:
    case WebXmlTag::name:
        handleParaTag(r);
        break;
    case WebXmlTag::listitem:
    case WebXmlTag::parametername:
    case WebXmlTag::parameteritem:
        handleItemTag(r);
        break;
    case WebXmlTag::ulink:
        handleLinkTag(r);
        break;
    case WebXmlTag::itemizedlist:
    case WebXmlTag::parameternamelist:
    case WebXmlTag::parameterlist:
        handleListTag(r);
        break;
    case WebXmlTag::highlight:
    case WebXmlTag::linebreak:
    case WebXmlTag::programlisting:
    case WebXmlTag::xreftitle:
    case WebXmlTag::sp:
    case WebXmlTag::entry:
    case WebXmlTag::simplesect:
    case WebXmlTag::verbatim:
    case WebXmlTag::xrefsect:
    case WebXmlTag::xrefdescription:
        handleIgnoredTag(r);
        break;
    case WebXmlTag::Unknown:
        break;
    }
}

void QtXmlToSphinx::formatCurrentTable()
{
    Q_ASSERT(!m_tables.isEmpty());
    auto &table = m_tables.back();
    if (table.isEmpty())
        return;
    table.normalize();
    m_output << '\n';
    table.format(m_output);
}

void QtXmlToSphinx::pushOutputBuffer()
{
    m_buffers.append(std::make_shared<QString>());
    m_output.setString(m_buffers.top().get());
}

QString QtXmlToSphinx::popOutputBuffer()
{
    Q_ASSERT(!m_buffers.isEmpty());
    QString result(*m_buffers.top().get());
    m_buffers.pop();
    m_output.setString(m_buffers.isEmpty() ? nullptr : m_buffers.top().get());
    return result;
}

static const QString autoTranslatedPlaceholder = u"AUTO_GENERATED\n"_s;
static const QString autoTranslatedNote =
uR"(.. warning::
    This section contains snippets that were automatically
    translated from C++ to Python and may contain errors.

)"_s;

void QtXmlToSphinx::setAutoTranslatedNote(QString *str) const
{
    if (m_containsAutoTranslations)
        str->replace(autoTranslatedPlaceholder, autoTranslatedNote);
    else
        str->remove(autoTranslatedPlaceholder);
}

QString QtXmlToSphinx::transform(const QString& doc)
{
    Q_ASSERT(m_buffers.isEmpty());
    if (doc.trimmed().isEmpty())
        return doc;

    pushOutputBuffer();

    QXmlStreamReader reader(doc);

    m_output << autoTranslatedPlaceholder;
    Indentation indentation(m_output);

    while (!reader.atEnd()) {
        QXmlStreamReader::TokenType token = reader.readNext();
        if (reader.hasError()) {
            QString message;
            QTextStream(&message) << "XML Error "
                << reader.errorString() << " at " << reader.lineNumber()
                << ':' << reader.columnNumber() << '\n' << doc;
            m_output << message;
            throw Exception(message);
            break;
        }

        if (token == QXmlStreamReader::StartElement) {
            WebXmlTag tag = webXmlTagHash().value(reader.name(), WebXmlTag::Unknown);
            if (!m_tagStack.isEmpty() && tag == WebXmlTag::raw)
                tag = WebXmlTag::Unknown;
            m_tagStack.push(tag);
        }

        if (!m_tagStack.isEmpty())
            callHandler(m_tagStack.top(), reader);

        if (token == QXmlStreamReader::EndElement) {
            m_tagStack.pop();
            m_lastTagName = reader.name().toString();
        }
    }

    if (!m_inlineImages.isEmpty()) {
        // Write out inline image definitions stored in handleInlineImageTag().
        m_output << '\n' << disableIndent;
        for (const InlineImage &img : std::as_const(m_inlineImages))
            m_output << ".. |" << img.tag << "| image:: " << img.href << '\n';
        m_output << '\n' << enableIndent;
        m_inlineImages.clear();
    }

    m_output.flush();
    QString retval = popOutputBuffer();
    Q_ASSERT(m_buffers.isEmpty());
    setAutoTranslatedNote(&retval);
    return retval;
}

static QString resolveFile(const QStringList &locations, const QString &path)
{
    for (QString location : locations) {
        location.append(u'/');
        location.append(path);
        if (QFileInfo::exists(location))
            return location;
    }
    return QString();
}

enum class SnippetType
{
    Other, // .qdoc, .qml,...
    CppSource, CppHeader // Potentially converted to Python
};

SnippetType snippetType(const QString &path)
{
    if (path.endsWith(u".cpp"))
        return SnippetType::CppSource;
    if (path.endsWith(u".h"))
        return SnippetType::CppHeader;
    return SnippetType::Other;
}

// Return the name of a .cpp/.h snippet converted to Python by snippets-translate
static QString pySnippetName(const QString &path, SnippetType type)
{
    switch (type) {
    case SnippetType::CppSource:
        return path.left(path.size() - 3) + u"py"_s;
        break;
    case SnippetType::CppHeader:
        return path + u".py"_s;
        break;
    default:
        break;
    }
    return {};
}

QtXmlToSphinx::Snippet QtXmlToSphinx::readSnippetFromLocations(const QString &path,
                                                const QString &identifier,
                                                const QString &fallbackPath,
                                                QString *errorMessage)
{
    // For anything else but C++ header/sources (no conversion to Python),
    // use existing fallback paths first.
    const auto type = snippetType(path);
    if (type == SnippetType::Other && !fallbackPath.isEmpty()) {
        const QString code = readFromLocation(fallbackPath, identifier, errorMessage);
        return {code, code.isNull() ? Snippet::Error : Snippet::Fallback};
    }

    // For C++ header/sources, try snippets converted to Python first.
    QString resolvedPath;
    const auto &locations = m_parameters.codeSnippetDirs;

    if (type != SnippetType::Other) {
        if (!fallbackPath.isEmpty() && !m_parameters.codeSnippetRewriteOld.isEmpty()) {
            // Try looking up Python converted snippets by rewriting snippets paths
            QString rewrittenPath = pySnippetName(fallbackPath, type);
            if (!rewrittenPath.isEmpty()) {
                rewrittenPath.replace(m_parameters.codeSnippetRewriteOld,
                                      m_parameters.codeSnippetRewriteNew);
                const QString code = readFromLocation(rewrittenPath, identifier, errorMessage);
                m_containsAutoTranslations = true;
                return {code, code.isNull() ? Snippet::Error : Snippet::Converted};
            }
        }

        resolvedPath = resolveFile(locations, pySnippetName(path, type));
        if (!resolvedPath.isEmpty()) {
            const QString code = readFromLocation(resolvedPath, identifier, errorMessage);
            return {code, code.isNull() ? Snippet::Error : Snippet::Converted};
        }
    }

    resolvedPath = resolveFile(locations, path);
    if (!resolvedPath.isEmpty()) {
        const QString code = readFromLocation(resolvedPath, identifier, errorMessage);
        return {code, code.isNull() ? Snippet::Error : Snippet::Resolved};
    }

    if (!fallbackPath.isEmpty()) {
        *errorMessage = msgFallbackWarning(path, identifier, fallbackPath);
        const QString code = readFromLocation(fallbackPath, identifier, errorMessage);
        return {code, code.isNull() ? Snippet::Error : Snippet::Fallback};
    }

    *errorMessage = msgSnippetsResolveError(path, locations);
    return {{}, Snippet::Error};
}

// Helpers for extracting qdoc snippets "#/// [id]"
static QString fileNameOfDevice(const QIODevice *inputFile)
{
    const auto *file = qobject_cast<const QFile *>(inputFile);
    return file ? QDir::toNativeSeparators(file->fileName()) : u"<stdin>"_s;
}

static QString msgSnippetNotFound(const QIODevice &inputFile,
                                  const QString &identifier)
{
    return u"Code snippet file found ("_s + fileNameOfDevice(&inputFile)
           + u"), but snippet ["_s + identifier + u"] not found."_s;
}

static QString msgEmptySnippet(const QIODevice &inputFile, int lineNo,
                               const QString &identifier)
{
    return u"Empty code snippet ["_s + identifier + u"] at "_s
           + fileNameOfDevice(&inputFile) + u':' + QString::number(lineNo);
}

// Pattern to match qdoc snippet IDs with "#/// [id]" comments and helper to find ID
static const QRegularExpression &snippetIdPattern()
{
    static const QRegularExpression result(uR"RX((//|#) *! *\[([^]]+)\])RX"_s);
    Q_ASSERT(result.isValid());
    return result;
}

static bool matchesSnippetId(QRegularExpressionMatchIterator it,
                             const QString &identifier)
{
    while (it.hasNext()) {
        if (it.next().captured(2) == identifier)
            return true;
    }
    return false;
}

QString QtXmlToSphinx::readSnippet(QIODevice &inputFile, const QString &identifier,
                                   QString *errorMessage)
{
    const QByteArray identifierBA = identifier.toUtf8();
    // Lambda that matches the snippet id
    const auto snippetIdPred = [&identifierBA, &identifier](const QByteArray &lineBA)
    {
        const bool isComment = lineBA.contains('/') || lineBA.contains('#');
        if (!isComment || !lineBA.contains(identifierBA))
            return false;
        const QString line = QString::fromUtf8(lineBA);
        return matchesSnippetId(snippetIdPattern().globalMatch(line), identifier);
    };

    // Find beginning, skip over
    int lineNo = 1;
    for (; !inputFile.atEnd() && !snippetIdPred(inputFile.readLine());
         ++lineNo) {
    }

    if (inputFile.atEnd()) {
        *errorMessage = msgSnippetNotFound(inputFile, identifier);
        return {};
    }

    QString code;
    for (; !inputFile.atEnd(); ++lineNo) {
        const QString line = QString::fromUtf8(inputFile.readLine());
        auto it = snippetIdPattern().globalMatch(line);
        if (it.hasNext()) { // Skip snippet id lines
            if (matchesSnippetId(it, identifier))
                break;
        } else {
            code += line;
        }
    }

    if (code.isEmpty())
        *errorMessage = msgEmptySnippet(inputFile, lineNo, identifier);

    return code;
}

QString QtXmlToSphinx::readFromLocation(const QString &location, const QString &identifier,
                                        QString *errorMessage)
{
    QFile inputFile;
    inputFile.setFileName(location);
    if (!inputFile.open(QIODevice::ReadOnly)) {
        QTextStream(errorMessage) << "Could not read code snippet file: "
            << QDir::toNativeSeparators(inputFile.fileName())
            << ": " << inputFile.errorString();
        return QString(); // null
    }

    QString code = u""_s; // non-null
    if (identifier.isEmpty()) {
        while (!inputFile.atEnd())
            code += QString::fromUtf8(inputFile.readLine());
        return CodeSnipHelpers::fixSpaces(code);
    }

    code = readSnippet(inputFile, identifier, errorMessage);
    return code.isEmpty() ? QString{} : CodeSnipHelpers::fixSpaces(code); // maintain isNull()
}

void QtXmlToSphinx::handleHeadingTag(QXmlStreamReader& reader)
{
    static int headingSize = 0;
    static char type;
    static char types[] = { '-', '^' };
    QXmlStreamReader::TokenType token = reader.tokenType();
    if (token == QXmlStreamReader::StartElement) {
        uint typeIdx = reader.attributes().value(u"level"_s).toUInt();
        if (typeIdx >= sizeof(types))
            type = types[sizeof(types)-1];
        else
            type = types[typeIdx];
    } else if (token == QXmlStreamReader::EndElement) {
        m_output << disableIndent << Pad(type, headingSize) << "\n\n"
            << enableIndent;
    } else if (token == QXmlStreamReader::Characters) {
        m_output << "\n\n" << disableIndent;
        headingSize = writeEscapedRstText(m_output, reader.text().trimmed());
        m_output << '\n' << enableIndent;
    }
}

void QtXmlToSphinx::handleParaTag(QXmlStreamReader& reader)
{
    switch (reader.tokenType()) {
    case QXmlStreamReader::StartElement:
        handleParaTagStart();
        break;
    case QXmlStreamReader::EndElement:
        handleParaTagEnd();
        break;
    case QXmlStreamReader::Characters:
        handleParaTagText(reader);
        break;
    default:
        break;
    }
}

void QtXmlToSphinx::handleParaTagStart()
{
      pushOutputBuffer();
}

void QtXmlToSphinx::handleParaTagText(QXmlStreamReader& reader)
{
    const auto text = reader.text();
    const QChar end = m_output.lastChar();
    if (!text.isEmpty() && m_output.indentation() == 0 && !end.isNull()) {
        QChar start = text[0];
        if ((end == u'*' || end == u'`') && start != u' ' && !start.isPunct())
            m_output << '\\';
    }
    m_output << escape(text);
}

void QtXmlToSphinx::handleParaTagEnd()
{
    QString result = popOutputBuffer().simplified();
    if (result.startsWith(u"**Warning:**"))
        result.replace(0, 12, QStringLiteral(".. warning:: "));
    else if (result.startsWith(u"**Note:**"))
        result.replace(0, 9, QStringLiteral(".. note:: "));
    m_output << result << "\n\n";
}

void QtXmlToSphinx::handleItalicTag(QXmlStreamReader& reader)
{
    switch (reader.tokenType()) {
    case QXmlStreamReader::StartElement:
        if (m_formattingDepth++ == 0) {
            m_insideItalic = true;
            m_output << rstItalic;
        }
        break;
    case QXmlStreamReader::EndElement:
        if (--m_formattingDepth == 0) {
            m_insideItalic = false;
            m_output << rstItalicOff;
        }
        break;
    case QXmlStreamReader::Characters:
        m_output << escape(reader.text().trimmed());
        break;
    default:
        break;
    }
}

void QtXmlToSphinx::handleBoldTag(QXmlStreamReader& reader)
{
    switch (reader.tokenType()) {
    case QXmlStreamReader::StartElement:
        if (m_formattingDepth++ == 0) {
            m_insideBold = true;
            m_output << rstBold;
        }
        break;
    case QXmlStreamReader::EndElement:
        if (--m_formattingDepth == 0) {
            m_insideBold = false;
            m_output << rstBoldOff;
        }
        break;
    case QXmlStreamReader::Characters:
        m_output << escape(reader.text().trimmed());
        break;
    default:
        break;
    }
}

void QtXmlToSphinx::handleArgumentTag(QXmlStreamReader& reader)
{
    switch (reader.tokenType()) {
    case QXmlStreamReader::StartElement:
        if (m_formattingDepth++ == 0)
            m_output << rstCode;
        break;
    case QXmlStreamReader::EndElement:
        if (--m_formattingDepth == 0)
            m_output << rstCodeOff;
        break;
    case QXmlStreamReader::Characters:
        m_output << reader.text().trimmed();
        break;
    default:
        break;
    }
}

static inline QString functionLinkType() { return QStringLiteral("function"); }
static inline QString classLinkType() { return QStringLiteral("class"); }

static inline QString fixLinkType(QStringView type)
{
    // TODO: create a flag PROPERTY-AS-FUNCTION to ask if the properties
    // are recognized as such or not in the binding
    if (type == u"property")
        return functionLinkType();
    if (type == u"typedef")
        return classLinkType();
    return type.toString();
}

static inline QString linkSourceAttribute(const QString &type)
{
    if (type == functionLinkType() || type == classLinkType())
        return u"raw"_s;
    return type == u"enum" || type == u"page"
        ? type : u"href"_s;
}

// "See also" links may appear as nested links:
//     <see-also>QAbstractXmlReceiver<link raw="isValid()" href="qxmlquery.html#isValid" type="function">isValid()</link>
//     which is handled in handleLinkTag
// or direct text:
//     <see-also>rootIsDecorated()</see-also>
//     which is handled here.

void QtXmlToSphinx::handleSeeAlsoTag(QXmlStreamReader& reader)
{
    switch (reader.tokenType()) {
    case QXmlStreamReader::StartElement:
        m_output << ".. seealso:: ";
        break;
    case QXmlStreamReader::Characters: {
        // Direct embedded link: <see-also>rootIsDecorated()</see-also>
        const auto  textR = reader.text().trimmed();
        if (!textR.isEmpty()) {
            const QString text = textR.toString();
            if (m_seeAlsoContext.isNull()) {
                const QString type = text.endsWith(u"()")
                    ? functionLinkType() : classLinkType();
                m_seeAlsoContext.reset(handleLinkStart(type, text));
            }
            handleLinkText(m_seeAlsoContext.data(), text);
        }
    }
        break;
    case QXmlStreamReader::EndElement:
        if (!m_seeAlsoContext.isNull()) { // direct, no nested </link> seen
            handleLinkEnd(m_seeAlsoContext.data());
            m_seeAlsoContext.reset();
        }
        m_output << "\n\n";
        break;
    default:
        break;
    }
}

static inline QString fallbackPathAttribute() { return QStringLiteral("path"); }

template <class Indent> // const char*/class Indentor
void formatSnippet(TextStream &str, Indent indent, const QString &snippet)
{
    const auto lines = QStringView{snippet}.split(u'\n');
    for (const auto &line : lines) {
        if (!line.trimmed().isEmpty())
            str << indent << line;
        str << '\n';
    }
}

static QString msgSnippetComparison(const QString &location, const QString &identifier,
                                    const QString &pythonCode, const QString &fallbackCode)
{
    StringStream str;
    str.setTabWidth(2);
    str << "Python snippet " << location;
    if (!identifier.isEmpty())
        str << " [" << identifier << ']';
    str << ":\n" << indent << pythonCode << ensureEndl << outdent
        << "Corresponding fallback snippet:\n"
        << indent << fallbackCode << ensureEndl << outdent << "-- end --\n";
    return str;
}

void QtXmlToSphinx::handleSnippetTag(QXmlStreamReader& reader)
{
    QXmlStreamReader::TokenType token = reader.tokenType();
    if (token == QXmlStreamReader::StartElement) {
        const bool consecutiveSnippet = m_lastTagName == u"snippet"
            || m_lastTagName == u"dots" || m_lastTagName == u"codeline";
        if (consecutiveSnippet) {
            m_output.flush();
            m_output.string()->chop(2);
        }
        QString location = reader.attributes().value(u"location"_s).toString();
        QString identifier = reader.attributes().value(u"identifier"_s).toString();
        QString fallbackPath;
        if (reader.attributes().hasAttribute(fallbackPathAttribute()))
            fallbackPath = reader.attributes().value(fallbackPathAttribute()).toString();
        QString errorMessage;

        const Snippet snippet = readSnippetFromLocations(location, identifier,
                                                         fallbackPath, &errorMessage);
        if (!errorMessage.isEmpty())
            warn(msgTagWarning(reader, m_context, m_lastTagName, errorMessage));

        if (m_parameters.snippetComparison && snippet.result == Snippet::Converted
            && !fallbackPath.isEmpty()) {
            const QString fallbackCode = readFromLocation(fallbackPath, identifier, &errorMessage);
            debug(msgSnippetComparison(location, identifier, snippet.code, fallbackCode));
        }

        if (!consecutiveSnippet)
            m_output << "::\n\n";

        Indentation indentation(m_output);
        if (snippet.result == Snippet::Error)
            m_output << "<Code snippet \"" << location << ':' << identifier << "\" not found>\n";
        else
            m_output << snippet.code << ensureEndl;
        m_output << '\n';
    }
}

void QtXmlToSphinx::handleDotsTag(QXmlStreamReader& reader)
{
    QXmlStreamReader::TokenType token = reader.tokenType();
    if (token == QXmlStreamReader::StartElement) {
        const bool consecutiveSnippet = m_lastTagName == u"snippet"
            || m_lastTagName == u"dots" || m_lastTagName == u"codeline";
        if (consecutiveSnippet) {
            m_output.flush();
            m_output.string()->chop(2);
        } else {
            m_output << "::\n\n";
        }
        pushOutputBuffer();
        int indent = reader.attributes().value(u"indent"_s).toInt()
                     + m_output.indentation() * m_output.tabWidth();
        for (int i = 0; i < indent; ++i)
            m_output << ' ';
    } else if (token == QXmlStreamReader::Characters) {
        m_output << reader.text().toString().trimmed();
    } else if (token == QXmlStreamReader::EndElement) {
        m_output << disableIndent << popOutputBuffer() << "\n\n\n" << enableIndent;
    }
}

void QtXmlToSphinx::handleTableTag(QXmlStreamReader& reader)
{
    QXmlStreamReader::TokenType token = reader.tokenType();
    if (token == QXmlStreamReader::StartElement) {
        if (parentTag() == WebXmlTag::para)
            handleParaTagEnd(); // End <para> to prevent the table from being rst-escaped
        m_tables.push({});
    } else if (token == QXmlStreamReader::EndElement) {
        // write the table on m_output
        formatCurrentTable();
        m_tables.pop();
        if (parentTag() == WebXmlTag::para)
            handleParaTagStart();
    }
}

void QtXmlToSphinx::handleTermTag(QXmlStreamReader& reader)
{
    QXmlStreamReader::TokenType token = reader.tokenType();
    if (token == QXmlStreamReader::StartElement) {
        pushOutputBuffer();
    } else if (token == QXmlStreamReader::Characters) {
        m_output << reader.text().toString().replace(u"::"_s, u"."_s);
    } else if (token == QXmlStreamReader::EndElement) {
        TableCell cell;
        cell.data = popOutputBuffer().trimmed();
        m_tables.back().appendRow(TableRow(1, cell));
    }
}


void QtXmlToSphinx::handleItemTag(QXmlStreamReader& reader)
{
    QXmlStreamReader::TokenType token = reader.tokenType();
    if (token == QXmlStreamReader::StartElement) {
        auto &table = m_tables.back();
        if (table.isEmpty())
            table.appendRow({});
        TableRow& row = table.last();
        TableCell cell;
        cell.colSpan = reader.attributes().value(u"colspan"_s).toShort();
        cell.rowSpan = reader.attributes().value(u"rowspan"_s).toShort();
        row << cell;
        pushOutputBuffer();
    } else if (token == QXmlStreamReader::EndElement) {
        QString data = trimLeadingNewlines(trimRight(popOutputBuffer()));
        auto &table = m_tables.back();
        if (!table.isEmpty()) {
            TableRow& row = table.last();
            if (!row.isEmpty())
                row.last().data = data;
        }
    }
}

void QtXmlToSphinx::handleHeaderTag(QXmlStreamReader &reader)
{
    // <header> in WebXML is either a table header or a description of a
    // C++ header with "name"/"href" attributes.
    if (reader.tokenType() == QXmlStreamReader::StartElement
        && !reader.attributes().hasAttribute(u"name"_s)) {
        auto &table = m_tables.back();
        table.setHeaderEnabled(true);
        table.appendRow({});
    }
}

void QtXmlToSphinx::handleRowTag(QXmlStreamReader& reader)
{
    if (reader.tokenType() == QXmlStreamReader::StartElement)
        m_tables.back().appendRow({});
}

enum ListType { BulletList, OrderedList, EnumeratedList };

static inline ListType webXmlListType(QStringView t)
{
    if (t == u"enum")
        return EnumeratedList;
    if (t == u"ordered")
        return OrderedList;
    return BulletList;
}

void QtXmlToSphinx::handleListTag(QXmlStreamReader& reader)
{
    static ListType listType = BulletList;
    QXmlStreamReader::TokenType token = reader.tokenType();
    if (token == QXmlStreamReader::StartElement) {
        m_tables.push({});
        auto &table = m_tables.back();
        listType = webXmlListType(reader.attributes().value(u"type"_s));
        if (listType == EnumeratedList) {
            table.appendRow(TableRow{TableCell(u"Constant"_s),
                                     TableCell(u"Description"_s)});
            table.setHeaderEnabled(true);
        }
        m_output.indent();
    } else if (token == QXmlStreamReader::EndElement) {
        m_output.outdent();
        const auto &table = m_tables.back();
        if (!table.isEmpty()) {
            switch (listType) {
            case BulletList:
            case OrderedList: {
                m_output << '\n';
                const char *separator = listType == BulletList ? "* " : "#. ";
                const char *indentLine = listType == BulletList ? "  " : "   ";
                for (const TableCell &cell : table.constFirst()) {
                    const auto itemLines = QStringView{cell.data}.split(u'\n');
                    m_output << separator << itemLines.constFirst() << '\n';
                    for (qsizetype i = 1, max = itemLines.size(); i < max; ++i)
                        m_output << indentLine << itemLines[i] << '\n';
                }
                m_output << '\n';
            }
                break;
            case EnumeratedList:
                formatCurrentTable();
                break;
            }
        }
        m_tables.pop();
    }
}

void QtXmlToSphinx::handleLinkTag(QXmlStreamReader& reader)
{
    switch (reader.tokenType()) {
    case QXmlStreamReader::StartElement: {
        // <link> embedded in <see-also> means the characters of <see-also> are no link.
        m_seeAlsoContext.reset();
        const QString type = fixLinkType(reader.attributes().value(u"type"_s));
        const QString ref = reader.attributes().value(linkSourceAttribute(type)).toString();
        m_linkContext.reset(handleLinkStart(type, ref));
    }
        break;
    case QXmlStreamReader::Characters:
        Q_ASSERT(!m_linkContext.isNull());
        handleLinkText(m_linkContext.data(), reader.text().toString());
        break;
    case QXmlStreamReader::EndElement:
        Q_ASSERT(!m_linkContext.isNull());
        handleLinkEnd(m_linkContext.data());
        m_linkContext.reset();
        break;
    default:
        break;
    }
}

QtXmlToSphinxLink *QtXmlToSphinx::handleLinkStart(const QString &type, QString ref) const
{
    ref.replace(u"::"_s, u"."_s);
    ref.remove(u"()"_s);
    auto *result = new QtXmlToSphinxLink(ref);

    if (m_insideBold)
        result->flags |= QtXmlToSphinxLink::InsideBold;
    else if (m_insideItalic)
        result->flags |= QtXmlToSphinxLink::InsideItalic;

    if (type == u"external" || isHttpLink(ref)) {
        result->type = QtXmlToSphinxLink::External;
    } else if (type == functionLinkType() && !m_context.isEmpty()) {
        result->type = QtXmlToSphinxLink::Method;
        const auto rawlinklist = QStringView{result->linkRef}.split(u'.');
        if (rawlinklist.size() == 1 || rawlinklist.constFirst() == m_context) {
            const auto lastRawLink = rawlinklist.constLast().toString();
            QString context = m_generator->resolveContextForMethod(m_context, lastRawLink);
            if (!result->linkRef.startsWith(context))
                result->linkRef.prepend(context + u'.');
        } else {
            result->linkRef = m_generator->expandFunction(result->linkRef);
        }
    } else if (type == functionLinkType() && m_context.isEmpty()) {
        result->type = QtXmlToSphinxLink::Function;
    } else if (type == classLinkType()) {
        result->type = QtXmlToSphinxLink::Class;
        result->linkRef = m_generator->expandClass(m_context, result->linkRef);
    } else if (type == u"enum") {
        result->type = QtXmlToSphinxLink::Attribute;
    } else if (type == u"page") {
        // Module, external web page or reference
        if (result->linkRef == m_parameters.moduleName)
            result->type = QtXmlToSphinxLink::Module;
        else
            result->type = QtXmlToSphinxLink::Reference;
    } else {
        result->type = QtXmlToSphinxLink::Reference;
    }
    return result;
}

// <link raw="Model/View Classes" href="model-view-programming.html#model-view-classes"
//  type="page" page="Model/View Programming">Model/View Classes</link>
// <link type="page" page="https://doc.qt.io/qt-5/class.html">QML types</link>
// <link raw="Qt Quick" href="qtquick-index.html" type="page" page="Qt Quick">Qt Quick</link>
// <link raw="QObject" href="qobject.html" type="class">QObject</link>
// <link raw="Qt::Window" href="qt.html#WindowType-enum" type="enum" enum="Qt::WindowType">Qt::Window</link>
// <link raw="QNetworkSession::reject()" href="qnetworksession.html#reject" type="function">QNetworkSession::reject()</link>

static QString fixLinkText(const QtXmlToSphinxLink *linkContext,
                           QString linktext)
{
    if (linkContext->type == QtXmlToSphinxLink::External
        || linkContext->type == QtXmlToSphinxLink::Reference) {
        return linktext;
    }
    // For the language reference documentation, strip the module name.
    // Clear the link text if that matches the function/class/enumeration name.
    const int lastSep = linktext.lastIndexOf(u"::");
    if (lastSep != -1)
        linktext.remove(0, lastSep + 2);
    else
         QtXmlToSphinx::stripPythonQualifiers(&linktext);
    if (linkContext->linkRef == linktext)
        return QString();
    if ((linkContext->type & QtXmlToSphinxLink::FunctionMask) != 0
        && (linkContext->linkRef + u"()"_s) == linktext) {
        return QString();
    }
    return  linktext;
}

void QtXmlToSphinx::handleLinkText(QtXmlToSphinxLink *linkContext, const QString &linktext)
{
    linkContext->linkText = fixLinkText(linkContext, linktext);
}

void QtXmlToSphinx::handleLinkEnd(QtXmlToSphinxLink *linkContext)
{
    m_output << m_generator->resolveLink(*linkContext);
}

WebXmlTag QtXmlToSphinx::parentTag() const
{
    const auto index = m_tagStack.size() - 2;
    return index >= 0 ? m_tagStack.at(index) : WebXmlTag::Unknown;
}

// Copy images that are placed in a subdirectory "images" under the webxml files
// by qdoc to a matching subdirectory under the "rst/PySide6/<module>" directory
static bool copyImage(const QString &href, const QString &docDataDir,
                      const QString &context, const QString &outputDir,
                      const QLoggingCategory &lc, QString *errorMessage)
{
    const QChar slash = u'/';
    const int lastSlash = href.lastIndexOf(slash);
    const QString imagePath = lastSlash != -1 ? href.left(lastSlash) : QString();
    const QString imageFileName = lastSlash != -1 ? href.right(href.size() - lastSlash - 1) : href;
    QFileInfo imageSource(docDataDir + slash + href);
    if (!imageSource.exists()) {
        QTextStream(errorMessage) << "Image " << href << " does not exist in "
            << QDir::toNativeSeparators(docDataDir);
        return false;
    }
    // Determine directory from context, "Pyside2.QtGui.QPainter" ->"Pyside2/QtGui".
    // FIXME: Not perfect yet, should have knowledge about namespaces (DataVis3D) or
    // nested classes "Pyside2.QtGui.QTouchEvent.QTouchPoint".
    QString relativeTargetDir = context;
    const int lastDot = relativeTargetDir.lastIndexOf(u'.');
    if (lastDot != -1)
        relativeTargetDir.truncate(lastDot);
    relativeTargetDir.replace(u'.', slash);
    if (!imagePath.isEmpty())
        relativeTargetDir += slash + imagePath;

    const QString targetDir = outputDir + slash + relativeTargetDir;
    const QString targetFileName = targetDir + slash + imageFileName;
    if (QFileInfo::exists(targetFileName))
        return true;
    if (!QFileInfo::exists(targetDir)) {
        const QDir outDir(outputDir);
        if (!outDir.mkpath(relativeTargetDir)) {
            QTextStream(errorMessage) << "Cannot create " << QDir::toNativeSeparators(relativeTargetDir)
                << " under " << QDir::toNativeSeparators(outputDir);
            return false;
        }
    }

    QFile source(imageSource.absoluteFilePath());
    if (!source.copy(targetFileName)) {
        QTextStream(errorMessage) << "Cannot copy " << QDir::toNativeSeparators(source.fileName())
            << " to " << QDir::toNativeSeparators(targetFileName) << ": "
            << source.errorString();
        return false;
    }
    qCDebug(lc).noquote().nospace() << __FUNCTION__ << " href=\""
        << href << "\", context=\"" << context << "\", docDataDir=\""
        << docDataDir << "\", outputDir=\"" << outputDir << "\", copied \""
        << source.fileName() << "\"->\"" << targetFileName << '"';
    return true;
}

bool QtXmlToSphinx::copyImage(const QString &href) const
{
    QString errorMessage;
    const bool result =
        ::copyImage(href, m_parameters.docDataDir, m_context,
                    m_parameters.outputDirectory,
                    m_generator->loggingCategory(),
                    &errorMessage);
    if (!result)
        throw Exception(errorMessage);
    return result;
}

void QtXmlToSphinx::handleImageTag(QXmlStreamReader& reader)
{
    if (reader.tokenType() != QXmlStreamReader::StartElement)
        return;
    const QString href = reader.attributes().value(u"href"_s).toString();
    if (copyImage(href))
        m_output << ".. image:: " <<  href << "\n\n";
}

void QtXmlToSphinx::handleInlineImageTag(QXmlStreamReader& reader)
{
    if (reader.tokenType() != QXmlStreamReader::StartElement)
        return;
    const QString href = reader.attributes().value(u"href"_s).toString();
    if (!copyImage(href))
        return;
    // Handle inline images by substitution references. Insert a unique tag
    // enclosed by '|' and define it further down. Determine tag from the base
    //file name with number.
    QString tag = href;
    int pos = tag.lastIndexOf(u'/');
    if (pos != -1)
        tag.remove(0, pos + 1);
    pos = tag.indexOf(u'.');
    if (pos != -1)
        tag.truncate(pos);
    tag += QString::number(m_inlineImages.size() + 1);
    m_inlineImages.append(InlineImage{tag, href});
    m_output << '|' << tag << '|' << ' ';
}

void QtXmlToSphinx::handleRawTag(QXmlStreamReader& reader)
{
    QXmlStreamReader::TokenType token = reader.tokenType();
    if (token == QXmlStreamReader::StartElement) {
        QString format = reader.attributes().value(u"format"_s).toString();
        m_output << ".. raw:: " << format.toLower() << "\n\n";
    } else if (token == QXmlStreamReader::Characters) {
        Indentation indent(m_output);
        m_output << reader.text();
    } else if (token == QXmlStreamReader::EndElement) {
        m_output << "\n\n";
    }
}

void QtXmlToSphinx::handleCodeTag(QXmlStreamReader& reader)
{
    QXmlStreamReader::TokenType token = reader.tokenType();
    if (token == QXmlStreamReader::StartElement) {
        m_output << "::\n\n" << indent;
    } else if (token == QXmlStreamReader::Characters) {
        Indentation indent(m_output);
        m_output << reader.text();
    } else if (token == QXmlStreamReader::EndElement) {
        m_output << outdent << "\n\n";
    }
}

void QtXmlToSphinx::handleUnknownTag(QXmlStreamReader& reader)
{
    QXmlStreamReader::TokenType token = reader.tokenType();
    if (token == QXmlStreamReader::StartElement) {
        qCDebug(m_generator->loggingCategory()).noquote().nospace()
            << "Unknown QtDoc tag: \"" << reader.name().toString() << "\".";
    }
}

void QtXmlToSphinx::handleSuperScriptTag(QXmlStreamReader& reader)
{
    QXmlStreamReader::TokenType token = reader.tokenType();
    if (token == QXmlStreamReader::StartElement) {
        m_output << " :sup:`";
        pushOutputBuffer();
    } else if (token == QXmlStreamReader::Characters) {
        m_output << reader.text().toString();
    } else if (token == QXmlStreamReader::EndElement) {
        m_output << popOutputBuffer();
        m_output << '`';
    }
}

void QtXmlToSphinx::handlePageTag(QXmlStreamReader &reader)
{
    if (reader.tokenType() != QXmlStreamReader::StartElement)
        return;

    m_output << disableIndent;

    const auto  title = reader.attributes().value(titleAttribute());
    if (!title.isEmpty())
        m_output << rstLabel(title.toString());

    const auto  fullTitle = reader.attributes().value(fullTitleAttribute());
    const int size = fullTitle.isEmpty()
       ? writeEscapedRstText(m_output, title)
       : writeEscapedRstText(m_output, fullTitle);

    m_output << '\n' << Pad('*', size) << "\n\n"
        << enableIndent;
}

void QtXmlToSphinx::handleTargetTag(QXmlStreamReader &reader)
{
    if (reader.tokenType() != QXmlStreamReader::StartElement)
        return;
    const auto  name = reader.attributes().value(nameAttribute());
    if (!name.isEmpty())
        m_output << rstLabel(name.toString());
}

void QtXmlToSphinx::handleIgnoredTag(QXmlStreamReader&)
{
}

void QtXmlToSphinx::handleUselessTag(QXmlStreamReader&)
{
    // Tag "description" just marks the init of "Detailed description" title.
    // Tag "definition" just marks enums. We have a different way to process them.
}

void QtXmlToSphinx::handleAnchorTag(QXmlStreamReader& reader)
{
    QXmlStreamReader::TokenType token = reader.tokenType();
    if (token == QXmlStreamReader::StartElement) {
        QString anchor;
        if (reader.attributes().hasAttribute(u"id"_s))
            anchor = reader.attributes().value(u"id"_s).toString();
        else if (reader.attributes().hasAttribute(u"name"_s))
            anchor = reader.attributes().value(u"name"_s).toString();
        if (!anchor.isEmpty() && m_opened_anchor != anchor) {
            m_opened_anchor = anchor;
            if (!m_context.isEmpty())
                anchor.prepend(m_context + u'_');
            m_output << rstLabel(anchor);
        }
   } else if (token == QXmlStreamReader::EndElement) {
       m_opened_anchor.clear();
   }
}

void QtXmlToSphinx::handleRstPassTroughTag(QXmlStreamReader& reader)
{
    if (reader.tokenType() == QXmlStreamReader::Characters)
        m_output << reader.text();
}

void QtXmlToSphinx::handleQuoteFileTag(QXmlStreamReader& reader)
{
    QXmlStreamReader::TokenType token = reader.tokenType();
    if (token == QXmlStreamReader::Characters) {
        QString location = reader.text().toString();
        location.prepend(m_parameters.libSourceDir + u'/');
        QString errorMessage;
        QString code = readFromLocation(location, QString(), &errorMessage);
        if (!errorMessage.isEmpty())
            warn(msgTagWarning(reader, m_context, m_lastTagName, errorMessage));
        m_output << "::\n\n";
        Indentation indentation(m_output);
        if (code.isEmpty())
            m_output << "<Code snippet \"" << location << "\" not found>\n";
        else
            m_output << code << ensureEndl;
        m_output << '\n';
    }
}

bool QtXmlToSphinx::Table::hasEmptyLeadingRow() const
{
    return !m_rows.isEmpty() && m_rows.constFirst().isEmpty();
}

bool QtXmlToSphinx::Table::hasEmptyTrailingRow() const
{
    return !m_rows.isEmpty() && m_rows.constLast().isEmpty();
}

void QtXmlToSphinx::Table::normalize()
{
    if (m_normalized)
        return;

    // Empty leading/trailing rows have been observed with nested tables
    if (hasEmptyLeadingRow() || hasEmptyLeadingRow()) {
        qWarning() << "QtXmlToSphinx: Table with leading/trailing empty columns found: " << *this;
        while (hasEmptyTrailingRow())
            m_rows.pop_back();
        while (hasEmptyLeadingRow())
            m_rows.pop_front();
    }

    if (isEmpty())
        return;

    //QDoc3 generates tables with wrong number of columns. We have to
    //check and if necessary, merge the last columns.
    qsizetype maxCols = -1;
    for (const auto &row : std::as_const(m_rows)) {
        if (row.size() > maxCols)
            maxCols = row.size();
    }
    if (maxCols <= 0)
        return;
    // add col spans
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        for (qsizetype col = 0; col < m_rows.at(row).size(); ++col) {
            QtXmlToSphinx::TableCell& cell = m_rows[row][col];
            bool mergeCols = (col >= maxCols);
            if (cell.colSpan > 0) {
                QtXmlToSphinx::TableCell newCell;
                newCell.colSpan = -1;
                for (int i = 0, max = cell.colSpan-1; i < max; ++i)
                    m_rows[row].insert(col + 1, newCell);
                cell.colSpan = 0;
                col++;
            } else if (mergeCols) {
                m_rows[row][maxCols - 1].data += u' ' + cell.data;
            }
        }
    }

    // row spans
    const qsizetype numCols = m_rows.constFirst().size();
    for (qsizetype col = 0; col < numCols; ++col) {
        for (qsizetype row = 0; row < m_rows.size(); ++row) {
            if (col < m_rows[row].size()) {
                QtXmlToSphinx::TableCell& cell = m_rows[row][col];
                if (cell.rowSpan > 0) {
                    QtXmlToSphinx::TableCell newCell;
                    newCell.rowSpan = -1;
                    qsizetype targetRow = row + 1;
                    const qsizetype targetEndRow =
                        std::min(targetRow + cell.rowSpan - 1, m_rows.size());
                    cell.rowSpan = 0;
                    for ( ; targetRow < targetEndRow; ++targetRow)
                        m_rows[targetRow].insert(col, newCell);
                    row++;
                }
            }
        }
    }
    m_normalized = true;
}

void QtXmlToSphinx::Table::format(TextStream& s) const
{
    if (isEmpty())
        return;

    Q_ASSERT(isNormalized());

    // calc width and height of each column and row
    const qsizetype headerColumnCount = m_rows.constFirst().size();
    QList<qsizetype> colWidths(headerColumnCount, 0);
    QList<qsizetype> rowHeights(m_rows.size(), 0);
    for (qsizetype i = 0, maxI = m_rows.size(); i < maxI; ++i) {
        const QtXmlToSphinx::TableRow& row = m_rows.at(i);
        for (qsizetype j = 0, maxJ = std::min(row.size(), colWidths.size()); j < maxJ; ++j) {
            // cache this would be a good idea
            const auto rowLines = QStringView{row[j].data}.split(u'\n');
            for (const auto &str : rowLines)
                colWidths[j] = std::max(colWidths[j], str.size());
            rowHeights[i] = std::max(rowHeights[i], rowLines.size());
        }
    }

    if (!*std::max_element(colWidths.begin(), colWidths.end()))
        return; // empty table (table with empty cells)

    // create a horizontal line to be used later.
    QString horizontalLine = u"+"_s;
    for (auto colWidth : colWidths)
        horizontalLine += QString(colWidth, u'-') + u'+';

    // write table rows
    for (qsizetype i = 0, maxI = m_rows.size(); i < maxI; ++i) { // for each row
        const QtXmlToSphinx::TableRow& row = m_rows.at(i);

        // print line
        s << '+';
        for (qsizetype col = 0; col < headerColumnCount; ++col) {
            char c;
            if (col >= row.size() || row[col].rowSpan == -1)
                c = ' ';
            else if (i == 1 && hasHeader())
                c = '=';
            else
                c = '-';
            s << Pad(c, colWidths.at(col)) << '+';
        }
        s << '\n';


        // Print the table cells
        for (qsizetype rowLine = 0; rowLine < rowHeights.at(i); ++rowLine) { // for each line in a row
            qsizetype j = 0;
            for (qsizetype maxJ = std::min(row.size(), headerColumnCount); j < maxJ; ++j) { // for each column
                const QtXmlToSphinx::TableCell& cell = row[j];
                // FIXME: Cache this!!!
                const auto rowLines = QStringView{cell.data}.split(u'\n');

                if (!j || !cell.colSpan)
                    s << '|';
                else
                    s << ' ';
                const auto width = int(colWidths.at(j));
                if (rowLine < rowLines.size())
                    s << AlignedField(rowLines.at(rowLine), width);
                else
                    s << Pad(' ', width);
            }
            for ( ; j < headerColumnCount; ++j) // pad
                s << '|' << Pad(' ', colWidths.at(j));
            s << "|\n";
        }
    }
    s << horizontalLine << "\n\n";
}

void QtXmlToSphinx::Table::formatDebug(QDebug &debug) const
{
    const auto rowCount = m_rows.size();
    debug << "Table(" <<rowCount << " rows";
    if (m_hasHeader)
        debug << ", [header]";
    if (m_normalized)
        debug << ", [normalized]";
    for (qsizetype r = 0; r < rowCount; ++r) {
        const auto &row = m_rows.at(r);
        const auto &colCount = row.size();
        debug << ", row " << r << " [" << colCount << "]={";
        for (qsizetype c = 0; c < colCount; ++c) {
            if (c > 0)
                debug << ", ";
            debug << row.at(c);
        }
        debug << '}';
    }
    debug << ')';
}

void QtXmlToSphinx::stripPythonQualifiers(QString *s)
{
    const int lastSep = s->lastIndexOf(u'.');
    if (lastSep != -1)
        s->remove(0, lastSep + 1);
}

void QtXmlToSphinx::warn(const QString &message) const
{
    qCWarning(m_generator->loggingCategory(), "%s", qPrintable(message));
}

void QtXmlToSphinx::debug(const QString &message) const
{
    qCDebug(m_generator->loggingCategory(), "%s", qPrintable(message));
}
