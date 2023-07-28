// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "textstream.h"

#include <cstring>

TextStream::TextStream(QIODevice *device, Language l) :
    m_str(device), m_language(l)
{
}

TextStream::TextStream(QString *string, Language l) :
    m_str(string), m_language(l)
{
}

TextStream::TextStream(QByteArray *array, Language l) :
    m_str(array), m_language(l)
{
}

TextStream::~TextStream() = default;

QChar TextStream::lastChar() const
{
    auto s = m_str.string();
    return s != nullptr && !s->isEmpty() ? *(s->crbegin()) : QChar();
}

void TextStream::setIndentation(int i)
{
    Q_ASSERT(i >= 0);
    m_indentation = i;
}

void TextStream::outdent(int n)
{
    m_indentation -= n;
    Q_ASSERT(m_indentation >= 0);
}

qint64 TextStream::pos() const
{
    // QTextStream::pos() only works for QIODevice, be a bit smarter
    if (auto s = m_str.string())
        return s->size();
    // QIODevices need to flushed to tell the correct position.
    const_cast<QTextStream &>(m_str).flush();
    return m_str.pos();
}

void TextStream::setString(QString *string, QIODeviceBase::OpenMode openMode)
{
    m_str.setString(string, openMode);
    m_rstFormattingEnd = false;
}

void TextStream::putRepetitiveChars(char c, int count)
{
    if (count > 0) {
        for (int i = 0; i < count; ++i) {
            const int ofw = m_str.fieldWidth();
            m_str.setFieldWidth(0);
            m_str << c;
            m_str.setFieldWidth(ofw);
        }
    }
}

void TextStream::_setRstFormattingEnd()
{
    m_rstFormattingEnd = true;
}

void TextStream::setLastCharClass(CharClass c)
{
    m_lastCharClass = c;
}

void TextStream::writeIndent()
{
    putRepetitiveChars(' ', m_indentation * m_tabWidth);
}

// Indent handling: If the last character was a new line
// and the upcoming one is none, indent the stream
// Special case for C++ : If the upcoming char is a '#', we don't
// indent (preprocessor directive).

template <class Char>
static TextStream::CharClass charClassHelper(Char c)
{
    switch (c) {
    case '\n':
        return TextStream::CharClass::NewLine;
    case '#':
        return TextStream::CharClass::Hash;
    case ' ':
    case '\t':
        return TextStream::CharClass::Space;
    case '\\':
        return TextStream::CharClass::BackSlash;
    default:
        break;
    }
    return TextStream::CharClass::Other;
}

static inline TextStream::CharClass charClass(char c)
{ return charClassHelper(c); }

static inline TextStream::CharClass charClass(QChar c)
{ return charClassHelper(c.unicode()); }

void TextStream::checkIndent(CharClass upComingCharClass)
{
    if (m_rstFormattingEnd) {
        if (upComingCharClass != CharClass::Space && upComingCharClass != CharClass::NewLine
            && upComingCharClass != CharClass::BackSlash) {
            m_str << '\\';
        }
        m_rstFormattingEnd = false;
    }
    if (m_indentationEnabled && m_lastCharClass == CharClass::NewLine
        && (upComingCharClass != CharClass::NewLine
            && (m_language != Language::Cpp || upComingCharClass != CharClass::Hash))) {
        writeIndent();
    }
    m_lastCharClass = upComingCharClass;
}

template <class Char>
void TextStream::putCharHelper(Char c)
{
    const auto klass = charClass(c);
    checkIndent(klass);
    m_str << c;
}

void TextStream::putString(QStringView v)
{
    if (v.isEmpty())
        return;
    if (v.contains(u'\n')) {
        for (auto c : v)
            putCharHelper(c);
    } else {
        // If there is no newline, write as a blob. This is important to make
        // field formatting (alignment/width) working, else each char will be
        // considered a field.
        const auto klass = charClass(*v.cbegin());
        checkIndent(klass);
        m_str << v;
        m_lastCharClass = CharClass::Other;
    }
}

void TextStream::putChar(QChar c)
{
    putCharHelper(c);
}

void TextStream::putString(const char *s)
{
    const char firstChar = *s;
    if (firstChar == '\0')
        return;
    if (std::strchr(s, '\n') != nullptr) { // See above
        for ( ; *s; ++s)
            putCharHelper(*s);
    } else {
        checkIndent(charClass(firstChar));
        m_str << s;
        m_lastCharClass = CharClass::Other;
    }
}

void TextStream::putChar(char c)
{
    putCharHelper(c);
}

void TextStream::putInt(int t)
{
    checkIndent(CharClass::Other);
    m_str << t;
}

void TextStream::putSizeType(qsizetype t)
{
    checkIndent(CharClass::Other);
    m_str << t;
}

StringStream::StringStream(Language l) : TextStream(&m_buffer, l)
{
}

void StringStream::clear()
{
    m_buffer.clear();
    setLastCharClass(CharClass::NewLine);
}

void indent(TextStream &s)
{
    s.indent();
}

void outdent(TextStream &s)
{
    s.outdent();
}

void enableIndent(TextStream &s)
{
    s.setIndentationEnabled(true);
}

void disableIndent(TextStream &s)
{
    s.setIndentationEnabled(false);
}

void ensureEndl(TextStream &s)
{
    if (s.lastChar() != u'\n')
        s << '\n';
}

void rstBold(TextStream &s)
{
    s.putRawString("**");
}

void rstBoldOff(TextStream &s)
{
    s.putRawString("**");
    s._setRstFormattingEnd();
}

void rstItalic(TextStream &s)
{
    s.putRawChar('*');
}

void rstItalicOff(TextStream &s)
{
    s.putRawChar('*');
    s._setRstFormattingEnd();
}

void rstCode(TextStream &s)
{
    s.putRawString("``");
}

void rstCodeOff(TextStream &s)
{
    s.putRawString("``");
    s._setRstFormattingEnd();
}
