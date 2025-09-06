#pragma once

#include <QtWidgets/QDialog>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QKeySequenceEdit>
#include <QtWidgets/QGroupBox>
#include <QKeySequence>
#include <QSettings>

// Streamlined KeybindSettings for core clipping functionality
struct KeybindSettings
{
    QKeySequence clipSave = QKeySequence("F9");
    QKeySequence clippingModeToggle = QKeySequence("F10");
};

class KeybindDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KeybindDialog(QWidget *parent = nullptr);
    ~KeybindDialog();

    KeybindSettings getKeybindSettings() const { return m_settings; }
    void setKeybindSettings(const KeybindSettings &settings);
    
    void snapshotOriginalSettings();

signals:
    void keybindsChanged(const KeybindSettings &settings);

private slots:
    void onAccept();
    void onReject();
    void resetToDefaults();

private:
    void setupUI();
    void applyStyle();
    void loadSettings();
    void saveSettings();

    KeybindSettings m_settings;
    KeybindSettings m_originalSettings;

    // UI Elements
    QVBoxLayout *m_mainLayout;
    QGridLayout *m_keybindLayout;

    QKeySequenceEdit *m_clipSaveEdit;
    QKeySequenceEdit *m_clippingModeToggleEdit;

    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
    QPushButton *m_resetButton;
};