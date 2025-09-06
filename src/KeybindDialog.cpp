#include "KeybindDialog.h"
#include <QtWidgets/QApplication>
#include <QSettings>

KeybindDialog::KeybindDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Keybind Settings");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(true);
    setFixedSize(400, 200);

    setupUI();
    applyStyle();
    loadSettings();

    // Note: Storing original settings is now handled by snapshotOriginalSettings()
    // before the dialog is shown. This makes the "Cancel" feature more reliable.
}

KeybindDialog::~KeybindDialog() {}

void KeybindDialog::snapshotOriginalSettings()
{
    // Store a copy of the settings as they were when the dialog was opened.
    m_originalSettings = m_settings;
}

void KeybindDialog::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setSpacing(15);
    m_mainLayout->setContentsMargins(15, 15, 15, 15);

    // Keybinds Group
    QGroupBox *keybindGroup = new QGroupBox("Global Hotkeys");
    m_keybindLayout = new QGridLayout(keybindGroup);
    m_keybindLayout->setSpacing(10);
    m_keybindLayout->setContentsMargins(15, 15, 15, 15);

    // Save Clip Keybind
    m_keybindLayout->addWidget(new QLabel("Save Clip:"), 0, 0);
    m_clipSaveEdit = new QKeySequenceEdit();
    m_clipSaveEdit->setMaximumSequenceLength(1);
    m_keybindLayout->addWidget(m_clipSaveEdit, 0, 1);

    // Toggle Clipping Mode Keybind
    m_keybindLayout->addWidget(new QLabel("Toggle Clipping Mode:"), 1, 0);
    m_clippingModeToggleEdit = new QKeySequenceEdit();
    m_clippingModeToggleEdit->setMaximumSequenceLength(1);
    m_keybindLayout->addWidget(m_clippingModeToggleEdit, 1, 1);

    m_keybindLayout->setColumnStretch(1, 1);
    m_mainLayout->addWidget(keybindGroup);
    m_mainLayout->addStretch();

    // Buttons Layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    m_resetButton = new QPushButton("Reset");
    m_resetButton->setObjectName("resetButton");
    connect(m_resetButton, &QPushButton::clicked, this, &KeybindDialog::resetToDefaults);
    buttonLayout->addWidget(m_resetButton);

    buttonLayout->addStretch();

    m_cancelButton = new QPushButton("Cancel");
    m_cancelButton->setObjectName("cancelButton");
    connect(m_cancelButton, &QPushButton::clicked, this, &KeybindDialog::onReject);
    buttonLayout->addWidget(m_cancelButton);

    m_okButton = new QPushButton("OK");
    m_okButton->setDefault(true);
    connect(m_okButton, &QPushButton::clicked, this, &KeybindDialog::onAccept);
    buttonLayout->addWidget(m_okButton);

    m_mainLayout->addLayout(buttonLayout);
}

void KeybindDialog::applyStyle()
{
    // Apply the same black and white theme as the main window
    setStyleSheet(R"(
        QDialog {
            background-color: #000000;
        }
        QWidget {
            color: #e0e0e0;
            font-family: Inter, sans-serif;
        }
        QGroupBox {
            font-weight: bold;
            border: 1px solid #333333;
            border-radius: 6px;
            margin-top: 8px;
            padding-top: 10px;
            background-color: #121212;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 5px;
        }
        QLabel {
            background-color: transparent;
        }
        QKeySequenceEdit {
            background-color: #111111;
            border: 1px solid #444444;
            border-radius: 4px;
            padding: 5px 8px;
            color: #e0e0e0;
        }
        QKeySequenceEdit:focus {
            border-color: #ffffff;
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
        /* OK Button Style */
        QPushButton[default="true"] {
            background-color: #ffffff;
            color: #000000;
            border: 1px solid #ffffff;
        }
        QPushButton[default="true"]:hover {
            background-color: #e0e0e0;
            border-color: #e0e0e0;
        }
        /* Reset Button Style */
        QPushButton#resetButton {
            background-color: #333333;
            border: 1px solid #888888;
        }
        QPushButton#resetButton:hover {
            background-color: #444444;
        }
    )");
}

void KeybindDialog::setKeybindSettings(const KeybindSettings& settings)
{
    m_settings = settings;
    m_clipSaveEdit->setKeySequence(settings.clipSave);
    m_clippingModeToggleEdit->setKeySequence(settings.clippingModeToggle);
}

void KeybindDialog::onAccept()
{
    m_settings.clipSave = m_clipSaveEdit->keySequence();
    m_settings.clippingModeToggle = m_clippingModeToggleEdit->keySequence();

    saveSettings();
    emit keybindsChanged(m_settings);
    accept();
}

void KeybindDialog::onReject()
{
    // Restore original settings before closing
    setKeybindSettings(m_originalSettings);
    reject();
}

void KeybindDialog::resetToDefaults()
{
    KeybindSettings defaults;
    setKeybindSettings(defaults);
}

void KeybindDialog::loadSettings()
{
    QSettings settings("GameClipRecorder", "Settings");
    m_settings.clipSave = QKeySequence(settings.value("keybind_clip", "F9").toString());
    m_settings.clippingModeToggle = QKeySequence(settings.value("keybind_clipping", "F10").toString());
    setKeybindSettings(m_settings);
}

void KeybindDialog::saveSettings()
{
    QSettings settings("GameClipRecorder", "Settings");
    settings.setValue("keybind_clip", m_settings.clipSave.toString());
    settings.setValue("keybind_clipping", m_settings.clippingModeToggle.toString());
}