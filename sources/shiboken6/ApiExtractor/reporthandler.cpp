// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "reporthandler.h"
#include "typedatabase.h"

#include "qtcompat.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QSet>
#include <cstring>
#include <cstdarg>
#include <cstdio>

using namespace Qt::StringLiterals;

#if defined(_WINDOWS) || defined(NOCOLOR)
    #define COLOR_END ""
    #define COLOR_WHITE ""
    #define COLOR_YELLOW ""
    #define COLOR_GREEN ""
#else
    #define COLOR_END "\033[0m"
    #define COLOR_WHITE "\033[1;37m"
    #define COLOR_YELLOW "\033[1;33m"
    #define COLOR_GREEN "\033[0;32m"
#endif

static bool m_silent = false;
static int m_warningCount = 0;
static int m_suppressedCount = 0;
static ReportHandler::DebugLevel m_debugLevel = ReportHandler::NoDebug;
static QSet<QString> m_reportedWarnings;
static QString m_prefix;
static bool m_withinProgress = false;
static int m_step_warning = 0;
static QElapsedTimer m_timer;

Q_LOGGING_CATEGORY(lcShiboken, "qt.shiboken")
Q_LOGGING_CATEGORY(lcShibokenDoc, "qt.shiboken.doc")

void ReportHandler::install()
{
    qInstallMessageHandler(ReportHandler::messageOutput);
    startTimer();
}

void ReportHandler::startTimer()
{
    m_timer.start();
}

ReportHandler::DebugLevel ReportHandler::debugLevel()
{
    return m_debugLevel;
}

void ReportHandler::setDebugLevel(ReportHandler::DebugLevel level)
{
    m_debugLevel = level;
}

bool ReportHandler::setDebugLevelFromArg(const QString &level)
{
    bool result = true;
    if (level == u"sparse")
        ReportHandler::setDebugLevel(ReportHandler::SparseDebug);
    else if (level == u"medium")
        ReportHandler::setDebugLevel(ReportHandler::MediumDebug);
    else if (level == u"full")
        ReportHandler::setDebugLevel(ReportHandler::FullDebug);
    else
        result = false;
    return result;
}

int ReportHandler::suppressedCount()
{
    return m_suppressedCount;
}

int ReportHandler::warningCount()
{
    return m_warningCount;
}

bool ReportHandler::isSilent()
{
    return m_silent;
}

void ReportHandler::setSilent(bool silent)
{
    m_silent = silent;
}

void ReportHandler::setPrefix(const QString &p)
{
    m_prefix = p;
}

void ReportHandler::messageOutput(QtMsgType type, const QMessageLogContext &context, const QString &text)
{
    // Check for file location separator added by SourceLocation
    int fileLocationPos = text.indexOf(u":\t");
    if (type == QtWarningMsg) {
        if (m_silent || m_reportedWarnings.contains(text))
            return;
        if (auto db = TypeDatabase::instance()) {
            const bool suppressed = fileLocationPos >= 0
                ? db->isSuppressedWarning(QStringView{text}.mid(fileLocationPos + 2))
                : db->isSuppressedWarning(text);
            if (suppressed) {
                ++m_suppressedCount;
                return;
            }
        }
        ++m_warningCount;
        ++m_step_warning;
        m_reportedWarnings.insert(text);
    }
    QString message = m_prefix;
    if (!message.isEmpty())
        message.append(u' ');
    const auto prefixLength = message.size();
    message.append(text);
    // Replace file location tab by space
    if (fileLocationPos >= 0)
        message[prefixLength + fileLocationPos + 1] = u' ';
    fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, context, message)));
}

static QByteArray timeStamp()
{
    const qint64 elapsed = m_timer.elapsed();
    return elapsed > 5000
        ? QByteArray::number(elapsed / 1000) + 's'
        : QByteArray::number(elapsed) + "ms";
}

void ReportHandler::startProgress(const QByteArray& str)
{
    if (m_silent)
        return;

    if (m_withinProgress)
        endProgress();

    m_withinProgress = true;
    const auto ts = '[' + timeStamp() + ']';
    std::printf("%s %8s %-60s", qPrintable(m_prefix), ts.constData(), str.constData());
    std::fflush(stdout);
}

void ReportHandler::endProgress()
{
    if (m_silent)
        return;

    m_withinProgress = false;
    const char *endMessage = m_step_warning == 0
        ?  "[" COLOR_GREEN "OK" COLOR_END "]\n"
        : "[" COLOR_YELLOW "WARNING" COLOR_END "]\n";
    std::fputs(endMessage, stdout);
    std::fflush(stdout);
    m_step_warning = 0;
}

QByteArray ReportHandler::doneMessage()
{
    QByteArray result = "Done, " + m_prefix.toUtf8() + ' ' + timeStamp();
    if (m_warningCount)
        result += ", " + QByteArray::number(m_warningCount) + " warnings";
    if (m_suppressedCount)
        result += " (" + QByteArray::number(m_suppressedCount) + " known issues)";
    return  result;
}
