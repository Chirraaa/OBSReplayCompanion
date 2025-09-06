#pragma once

#include <QDialog>

// Forward declarations
class QPlainTextEdit;
class QPushButton;

class LogDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LogDialog(QWidget *parent = nullptr);
    ~LogDialog();

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void onNewLogMessage(const QString& message);
    void copyLogsToClipboard();
    void clearLogs();

private:
    void setupUI();
    void applyStyle();

    QPlainTextEdit* m_logView;
    QPushButton* m_copyButton;
    QPushButton* m_clearButton;
    QPushButton* m_closeButton;
};