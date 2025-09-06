#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QSlider>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QTabWidget>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>
#include <QSet>
#include <QThread>
#include <QElapsedTimer>

#include "GameCapture.h"
#include "KeybindDialog.h"
#include "GlobalHotkey.h"
#include "AudioDeviceFetcher.h"
#include "ProcessMonitor.h"
#include "AudioVisualizer.h"

class LogDialog;

// Forward declare obs_volmeter
struct obs_volmeter;
typedef struct obs_volmeter obs_volmeter_t;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(GameCapture *capture, QWidget *parent = nullptr);
    ~MainWindow();

    void postInitRefresh();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    enum HotkeyID
    {
        HOTKEY_SAVE_CLIP = 1,
        HOTKEY_TOGGLE_CLIPPING,
    };

    enum ClippingState
    {
        DISABLED,
        AWAITING_GAME,
        ACTIVE
    };

    // Setup Methods
    void setupUI();
    void setupMenuBar();
    void setupTrayIcon();
    void setupGlobalHotkeys();
    void applyStyles();
    void loadSettings();
    void saveSettings();
    void startProcessMonitor();
    void stopProcessMonitor();
    void setupAutoStart();
    void removeAutoStart();
    void setupAudioVolmeter();
    void setupMicrophoneVolmeter();
    void playNotificationSound();
    void setSettingsLocked(bool locked);
    void updateUiForState();

    // UI Creation Helpers
    QWidget *createMainControls();
    QWidget *createStatusSection();
    QWidget *createGeneralSettingsTab();
    QWidget *createEncodingSettingsTab();
    QWidget *createAudioSettingsTab();
    QWidget *createNotificationSettingsTab();
    QGroupBox *createAdvancedX264Settings();
    QGroupBox *createAdvancedQsvSettings();
    QGroupBox *createAdvancedAmfSettings();

    // Core Components
    GameCapture *m_capture;
    QSystemTrayIcon *m_trayIcon;
    ProcessMonitor *m_processMonitor;
    QThread *m_processMonitorThread;
    GlobalHotkey *m_globalHotkey;
    KeybindDialog *m_keybindDialog;
    KeybindSettings m_keybindSettings;

    // State
    ClippingState m_clippingState;
    bool m_gameDetected;
    QString m_currentDetectedGame;
    QString m_outputFolder;
    QSet<QString> m_gameExes;

    // UI Elements
    QTabWidget *m_settingsTabs;

    // Main Controls
    QPushButton *m_clippingModeButton;
    QPushButton *m_clipButton;
    QComboBox *m_clipLengthCombo;

    // Status Display
    QLabel *m_clippingModeStatus;
    QLabel *m_statusLabel;

    // General Settings
    QLineEdit *m_outputPathEdit;
    QPushButton *m_browseButton;
    QListWidget *m_gameList;
    QPushButton *m_addGameButton;
    QPushButton *m_removeGameButton;
    QCheckBox *m_autoStartCheckBox;
    QCheckBox *m_minimizeToTrayCheckBox;
    QCheckBox *m_startClippingAutomaticallyCheckBox;
    QComboBox *m_resolutionCombo;
    QComboBox *m_fpsCombo;

    // Encoding Settings
    QComboBox *m_encoderCombo;
    QComboBox *m_rateControlCombo;
    QSpinBox *m_bitrateSpinBox;
    QSpinBox *m_crfSpinBox;
    QLabel *m_bitrateLabel;
    QLabel *m_crfLabel;
    QSpinBox *m_keyframeIntervalSpinBox;

    // Advanced NVENC
    QGroupBox *m_advancedNvencGroup;
    QComboBox *m_nvencPresetCombo;
    QComboBox *m_nvencTuningCombo;
    QComboBox *m_nvencMultipassCombo;
    QComboBox *m_nvencProfileCombo;
    QCheckBox *m_nvencLookaheadCheckBox;
    QCheckBox *m_nvencPsychoVisualTuningCheckBox;
    QSpinBox *m_nvencGpuSpinBox;
    QSpinBox *m_nvencMaxBFramesSpinBox;

    // Advanced x264/x265
    QGroupBox *m_advancedX264Group;
    QComboBox *m_x264PresetCombo;
    QComboBox *m_x264ProfileCombo;
    QComboBox *m_x264TuneCombo;
    QLineEdit *m_x264OptionsEdit;

    // Advanced QSV
    QGroupBox *m_advancedQsvGroup;
    QComboBox *m_qsvPresetCombo;
    QComboBox *m_qsvProfileCombo;
    QCheckBox *m_qsvLowPowerCheckBox;

    // Advanced AMF
    QGroupBox *m_advancedAmfGroup;
    QComboBox *m_amfUsageCombo; // "Preset" in UI
    QComboBox *m_amfProfileCombo;
    QSpinBox *m_amfBramesSpinBox;
    QLineEdit *m_amfOptionsEdit;

    // Audio Settings
    QCheckBox *m_audioEnabledCheckBox;
    QComboBox *m_audioDeviceCombo;
    QPushButton *m_refreshAudioButton;
    QSlider *m_audioVolumeSlider;
    QLabel *m_volumeLabel;
    AudioVisualizer *m_audioVisualizer;
    QCheckBox *m_showAudioLevelsCheckBox;

    // Microphone Settings
    QCheckBox *m_micEnabledCheckBox;
    QComboBox *m_micDeviceCombo;
    QPushButton *m_refreshMicButton;
    QSlider *m_micVolumeSlider;
    QLabel *m_micVolumeLabel;
    AudioVisualizer *m_microphoneVisualizer;
    QCheckBox *m_showMicLevelsCheckBox;

    // Notification Settings
    QCheckBox *m_soundEnabledCheckBox;
    QCheckBox *m_trayNotificationsCheckBox;

    // Menu Bar & Actions
    QMenu *m_settingsMenu;
    QAction *m_keybindAction;
    QMenu* m_helpMenu;
    QAction* m_showLogsAction;
    LogDialog* m_logDialog;


    // Audio Level Monitoring
    QTimer *m_visualizerUpdateTimer;
    obs_volmeter_t *m_audioVolmeter;
    obs_volmeter_t *m_microphoneVolmeter;
    float m_currentAudioLevel;
    float m_currentMicrophoneLevel;

private slots:
    // UI Actions
    void toggleClippingMode();
    void saveClip();
    void addGameExe();
    void removeGameExe();
    void browseOutputFolder();
    void showKeybindSettings();
    void showFromTray();
    void exitApplication();
    void showLogs();

    // Settings Changes
    void onClipLengthChanged();
    void onEncodingSettingsChanged();
    void onRateControlChanged();
    void onAudioSettingsChanged();
    void onMicrophoneSettingsChanged();
    void onVideoSettingsChanged();
    void onNotificationSettingsChanged();
    void onKeybindsChanged(const KeybindSettings &settings);
    void onAutoStartChanged(bool checked);
    void onStartClippingAutomaticallyChanged(bool checked);
    void onShowAudioLevelsChanged(bool enabled);
    void onShowMicLevelsChanged(bool enabled);

    // GameCapture Signals
    void onClippingModeChanged(bool active);
    void refreshEncoders();

    // Global Hotkeys
    void onGlobalHotkeyPressed(int id);

    // Process Monitoring
    void onProcessStarted(const QString &exeName);
    void onProcessStopped(const QString &exeName);

    // Audio Device Fetching
    void refreshAudioDevices();
    void refreshMicrophoneDevices();
    void onAudioDevicesReceived(const QList<QPair<QString, QString>> &devices);
    void onMicrophoneDevicesReceived(const QList<QPair<QString, QString>> &devices);
    void onAudioDeviceChanged();
    void onMicrophoneDeviceChanged();

    // Audio Level Updates
    void updateVisualizers();
    float getAudioLevel();
    float getMicrophoneLevel();
};