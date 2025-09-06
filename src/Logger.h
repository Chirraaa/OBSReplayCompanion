#pragma once

#include <QObject>
#include <QStringList>
#include <QMutex>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

// Custom message handler to be installed with qInstallMessageHandler
void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);

class Logger : public QObject
{
    Q_OBJECT

public:
    static Logger* instance();

    void logMessage(QtMsgType type, const QString &message);
    QStringList getMessages() const;

signals:
    void newMessage(const QString& message);

private:
    Logger(QObject* parent = nullptr);
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static Logger* m_instance;
    QStringList m_messages;
    mutable QMutex m_mutex;
    QFile* m_logFile;
    QTextStream* m_logStream;
};