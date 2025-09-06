#include "Logger.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QStringConverter> // Required for the Utf8 enum

Logger* Logger::m_instance = nullptr;

// The global message handler function
void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(context);
    Logger::instance()->logMessage(type, msg);
}

Logger* Logger::instance()
{
    if (!m_instance) {
        m_instance = new Logger(QCoreApplication::instance());
    }
    return m_instance;
}

Logger::Logger(QObject* parent) : QObject(parent), m_logFile(nullptr), m_logStream(nullptr)
{
    // Optional: Log to a file as well for persistence across sessions
    QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(logPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    m_logFile = new QFile(logPath + "/app.log");
    if (m_logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
        m_logStream = new QTextStream(m_logFile);
        m_logStream->setEncoding(QStringConverter::Utf8);
    }
}

Logger::~Logger()
{
    if (m_logStream) {
        m_logStream->flush();
        delete m_logStream;
    }
    if (m_logFile) {
        m_logFile->close();
        delete m_logFile;
    }
}

void Logger::logMessage(QtMsgType type, const QString &message)
{
    QString typeStr;
    switch (type) {
    case QtDebugMsg:
        typeStr = "DEBUG";
        break;
    case QtInfoMsg:
        typeStr = "INFO";
        break;
    case QtWarningMsg:
        typeStr = "WARN";
        break;
    case QtCriticalMsg:
        typeStr = "CRITICAL";
        break;
    case QtFatalMsg:
        typeStr = "FATAL";
        break;
    }

    QString formattedMessage = QString("%1 [%2] %3")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"))
        .arg(typeStr)
        .arg(message);

    QMutexLocker locker(&m_mutex);
    m_messages.append(formattedMessage);
    if (m_messages.size() > 2000) { // Keep last 2000 messages in memory
        m_messages.removeFirst();
    }
    if (m_logStream) {
        (*m_logStream) << formattedMessage << "\n";
        m_logStream->flush();
    }
    emit newMessage(formattedMessage);
}

QStringList Logger::getMessages() const
{
    QMutexLocker locker(&m_mutex);
    return m_messages;
}