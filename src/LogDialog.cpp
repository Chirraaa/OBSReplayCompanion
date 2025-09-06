#include "LogDialog.h"
#include "Logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QClipboard>
#include <QApplication>

LogDialog::LogDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Application Logs");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumSize(700, 500);
    resize(800, 600);

    setupUI();
    applyStyle();

    // Connect to the logger to receive new messages in real-time
    connect(Logger::instance(), &Logger::newMessage, this, &LogDialog::onNewLogMessage);
}

LogDialog::~LogDialog() {}

void LogDialog::showEvent(QShowEvent *event)
{
    // Populate with existing logs when the dialog is shown.
    m_logView->clear();
    m_logView->appendPlainText(Logger::instance()->getMessages().join('\n'));
    QDialog::showEvent(event);
}

void LogDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    m_logView = new QPlainTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setWordWrapMode(QTextOption::NoWrap);
    QFont font("Consolas", 10);
    font.setStyleHint(QFont::Monospace);
    m_logView->setFont(font);
    mainLayout->addWidget(m_logView);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    m_copyButton = new QPushButton("Copy to Clipboard");
    connect(m_copyButton, &QPushButton::clicked, this, &LogDialog::copyLogsToClipboard);
    buttonLayout->addWidget(m_copyButton);

    m_clearButton = new QPushButton("Clear");
    connect(m_clearButton, &QPushButton::clicked, this, &LogDialog::clearLogs);
    buttonLayout->addWidget(m_clearButton);

    buttonLayout->addStretch();

    m_closeButton = new QPushButton("Close");
    connect(m_closeButton, &QPushButton::clicked, this, &LogDialog::accept);
    buttonLayout->addWidget(m_closeButton);

    mainLayout->addLayout(buttonLayout);
}

void LogDialog::applyStyle()
{
    // Use a similar style to the rest of the application
    setStyleSheet(R"(
        QDialog {
            background-color: #121212;
        }
        QWidget {
            color: #e0e0e0;
            font-family: Inter, sans-serif;
        }
        QPlainTextEdit {
            background-color: #000000;
            border: 1px solid #333333;
            border-radius: 4px;
            color: #cccccc;
        }
        QPushButton {
            background-color: #222222;
            border: 1px solid #444444;
            border-radius: 4px;
            padding: 8px 16px;
            font-weight: bold;
            color: #e0e0e0;
        }
        QPushButton:hover {
            background-color: #333333;
            border-color: #555555;
        }
        QPushButton:pressed {
            background-color: #1a1a1a;
        }
    )");
}


void LogDialog::onNewLogMessage(const QString& message)
{
    // Append new messages as they arrive, only if the dialog is visible
    if (isVisible()) {
        m_logView->appendPlainText(message);
    }
}

void LogDialog::copyLogsToClipboard()
{
    QApplication::clipboard()->setText(m_logView->toPlainText());
}

void LogDialog::clearLogs()
{
    m_logView->clear();
}