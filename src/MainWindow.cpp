#include "MainWindow.h"
#include "LogDialog.h"
#include <QtWidgets/QApplication>
#include <QMessageBox>
#include <QStandardPaths>
#include <QFileInfo>
#include <windows.h>
#include <QCoreApplication>
#include <QDir>
#include <QCloseEvent>
#include <QPixmap>
#include <QIcon>
#include <QMenuBar>
#include <QAction>
#include <QDebug>
#include <QSettings>
#include <QVariantMap>
#include <QIcon>
#include <mmsystem.h>
#include <QRegularExpressionValidator>
#include <tlhelp32.h>

#include <obs.hpp>
#include <obs-frontend-api.h>

// Helper function to update profile combo boxes based on codec
static void updateProfileComboBox(QComboBox *combo, bool is_hevc, const QStringList &h264_profiles, const QStringList &hevc_profiles)
{
    combo->blockSignals(true);
    QString current_profile = combo->currentText();
    combo->clear();

    if (is_hevc)
    {
        combo->addItems(hevc_profiles);
    }
    else
    {
        combo->addItems(h264_profiles);
    }

    int index = combo->findText(current_profile);
    if (index != -1)
    {
        combo->setCurrentIndex(index);
    }
    else if (combo->count() > 0)
    {
        combo->setCurrentIndex(0); // Default to the first item
    }
    combo->blockSignals(false);
}

MainWindow::MainWindow(GameCapture *capture, QWidget *parent)
    : QMainWindow(parent),
      m_capture(capture),
      m_trayIcon(nullptr),
      m_processMonitor(nullptr),
      m_processMonitorThread(nullptr),
      m_globalHotkey(nullptr),
      m_keybindDialog(nullptr),
      m_logDialog(nullptr),
      m_clippingState(DISABLED),
      m_gameDetected(false),
      m_audioVisualizer(nullptr),
      m_microphoneVisualizer(nullptr),
      m_visualizerUpdateTimer(nullptr), // <-- MODIFIED
      m_audioVolmeter(nullptr),
      m_microphoneVolmeter(nullptr),
      m_currentAudioLevel(0.0f),
      m_currentMicrophoneLevel(0.0f)
{
    // Set window icon
    setWindowIcon(QIcon(":/logo.ico"));
    setWindowTitle("OBS Replay Companion");
    setMinimumSize(420, 550);
    resize(450, 800);

    m_outputFolder = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Clips";

    setupMenuBar();
    setupUI();
    setupTrayIcon();
    applyStyles();
    loadSettings();
    setupGlobalHotkeys();

    connect(m_capture, &GameCapture::clippingModeChanged, this, &MainWindow::onClippingModeChanged);
    connect(m_capture, &GameCapture::recordingStarted, [this]()
            {
    m_statusLabel->setText("Saving clip...");
    m_statusLabel->setStyleSheet("color: #b0b0b0;"); });
    connect(m_capture, &GameCapture::recordingFinished, [this](bool success, const QString &filename)
            {
    // Re-enable the save clip button here.
    m_clipButton->setEnabled(true);

    if (success) {
        m_statusLabel->setText("Clip saved successfully!");
        m_statusLabel->setStyleSheet("color: #ffffff;");
        if (m_soundEnabledCheckBox->isChecked()) {
            playNotificationSound();
        }
        if (m_trayNotificationsCheckBox->isChecked() && m_trayIcon && m_trayIcon->isVisible()) {
            m_trayIcon->showMessage("Clip Saved",
                QString("Clip saved: %1").arg(QFileInfo(filename).fileName()),
                QSystemTrayIcon::Information, 3000);
        }
    } else {
        m_statusLabel->setText("Failed to save clip!");
        m_statusLabel->setStyleSheet("color: #909090;");
    } });


    // Initial refresh
    refreshAudioDevices();
    refreshMicrophoneDevices();

    qDebug() << "Application startup complete. UI is initialized.";
}

MainWindow::~MainWindow()
{
    stopProcessMonitor();
    saveSettings();
    if (m_autoStartCheckBox && !m_autoStartCheckBox->isChecked())
    {
        removeAutoStart();
    }

    // Ensure volmeters are destroyed on exit
    if (m_audioVolmeter)
    {
        obs_volmeter_destroy(m_audioVolmeter);
    }
    if (m_microphoneVolmeter)
    {
        obs_volmeter_destroy(m_microphoneVolmeter);
    }
}

void MainWindow::postInitRefresh()
{
    if (!m_capture || !m_capture->IsInitialized())
        return;

    refreshEncoders();
    onEncodingSettingsChanged();
    onAudioSettingsChanged();
    onMicrophoneSettingsChanged();

    updateUiForState();

    bool autostarted = QCoreApplication::arguments().contains("--autostart");
    if (autostarted && m_startClippingAutomaticallyCheckBox->isChecked())
    {
        qDebug() << "Autostart detected, enabling clipping mode.";
        QTimer::singleShot(100, this, &MainWindow::toggleClippingMode);
    }
}

void MainWindow::setupUI()
{
    QWidget *centralWidget = new QWidget;
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    mainLayout->addWidget(createMainControls());
    mainLayout->addWidget(createStatusSection());

    m_settingsTabs = new QTabWidget();
    m_settingsTabs->addTab(createGeneralSettingsTab(), "General");
    m_settingsTabs->addTab(createEncodingSettingsTab(), "Encoding");
    m_settingsTabs->addTab(createAudioSettingsTab(), "Audio");
    m_settingsTabs->addTab(createNotificationSettingsTab(), "Notifications");
    mainLayout->addWidget(m_settingsTabs);

    m_visualizerUpdateTimer = new QTimer(this);
    connect(m_visualizerUpdateTimer, &QTimer::timeout, this, &MainWindow::updateVisualizers);
}

QWidget *MainWindow::createMainControls()
{
    QWidget *container = new QWidget();
    QHBoxLayout *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    m_clippingModeButton = new QPushButton("Enable Clipping");
    m_clippingModeButton->setObjectName("ClippingButton");
    m_clippingModeButton->setMinimumHeight(40);
    m_clippingModeButton->setCheckable(true);
    connect(m_clippingModeButton, &QPushButton::clicked, this, &MainWindow::toggleClippingMode);
    layout->addWidget(m_clippingModeButton, 1);

    m_clipButton = new QPushButton("Save Clip");
    m_clipButton->setObjectName("SaveClipButton");
    m_clipButton->setMinimumHeight(40);
    m_clipButton->setEnabled(false);
    connect(m_clipButton, &QPushButton::clicked, this, &MainWindow::saveClip);
    layout->addWidget(m_clipButton, 1);

    layout->addStretch(1);
    layout->addWidget(new QLabel("Clip Length:"));
    m_clipLengthCombo = new QComboBox;
    m_clipLengthCombo->addItems({"15s", "30s", "45s", "60s", "90s", "120s", "180s"});
    m_clipLengthCombo->setCurrentText("60s");
    connect(m_clipLengthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onClipLengthChanged);
    layout->addWidget(m_clipLengthCombo);

    return container;
}

QWidget *MainWindow::createStatusSection()
{
    QGroupBox *group = new QGroupBox("Status");
    QVBoxLayout *layout = new QVBoxLayout(group);
    layout->setSpacing(8);

    m_clippingModeStatus = new QLabel("Clipping is Disabled");
    m_clippingModeStatus->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_clippingModeStatus);

    m_statusLabel = new QLabel("Initializing...");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("font-style: italic; color: #888888;");
    layout->addWidget(m_statusLabel);

    return group;
}

QWidget *MainWindow::createGeneralSettingsTab()
{
    QWidget *tab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(tab);
    layout->setSpacing(15);

    QGroupBox *outputGroup = new QGroupBox("Output Folder");
    QVBoxLayout *outputLayout = new QVBoxLayout(outputGroup);
    m_outputPathEdit = new QLineEdit;
    m_browseButton = new QPushButton("Browse");
    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::browseOutputFolder);
    QHBoxLayout *pathLayout = new QHBoxLayout;
    pathLayout->addWidget(m_outputPathEdit);
    pathLayout->addWidget(m_browseButton);
    outputLayout->addLayout(pathLayout);
    layout->addWidget(outputGroup);

    QGroupBox *videoGroup = new QGroupBox("Video Settings");
    QGridLayout *videoLayout = new QGridLayout(videoGroup);

    videoLayout->addWidget(new QLabel("Resolution:"), 0, 0);
    m_resolutionCombo = new QComboBox;
    m_resolutionCombo->setEditable(true);
    m_resolutionCombo->addItems({"1920x1080", "2560x1440", "1280x720"});
    if (m_resolutionCombo->lineEdit())
    {
        m_resolutionCombo->lineEdit()->setValidator(new QRegularExpressionValidator(QRegularExpression("\\d{3,5}x\\d{3,5}"), this));
        m_resolutionCombo->lineEdit()->setPlaceholderText("e.g., 1920x1080");
    }
    videoLayout->addWidget(m_resolutionCombo, 0, 1);

    videoLayout->addWidget(new QLabel("FPS:"), 1, 0);
    m_fpsCombo = new QComboBox;
    m_fpsCombo->addItems({"30", "50", "60", "75", "90", "120", "144", "240"});
    videoLayout->addWidget(m_fpsCombo, 1, 1);

    QLabel *restartLabel = new QLabel("Changes to video settings require an application restart to take effect.");
    restartLabel->setStyleSheet("font-style: italic; color: #aaaaaa;");
    restartLabel->setWordWrap(true);
    videoLayout->addWidget(restartLabel, 2, 0, 1, 2);
    videoLayout->setColumnStretch(1, 1);
    connect(m_resolutionCombo, &QComboBox::currentTextChanged, this, &MainWindow::onVideoSettingsChanged);
    connect(m_fpsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onVideoSettingsChanged);

    layout->addWidget(videoGroup);

    QGroupBox *gamesGroup = new QGroupBox("Monitored Games");
    QVBoxLayout *gamesLayout = new QVBoxLayout(gamesGroup);
    m_gameList = new QListWidget;
    m_gameList->setMaximumHeight(120);
    gamesLayout->addWidget(m_gameList);
    QHBoxLayout *gameButtonsLayout = new QHBoxLayout;
    m_addGameButton = new QPushButton("Add Game");
    m_removeGameButton = new QPushButton("Remove");
    connect(m_addGameButton, &QPushButton::clicked, this, &MainWindow::addGameExe);
    connect(m_removeGameButton, &QPushButton::clicked, this, &MainWindow::removeGameExe);
    gameButtonsLayout->addWidget(m_addGameButton);
    gameButtonsLayout->addWidget(m_removeGameButton);
    gamesLayout->addLayout(gameButtonsLayout);
    layout->addWidget(gamesGroup);

    QGroupBox *settingsGroup = new QGroupBox("Application");
    QVBoxLayout *settingsLayout = new QVBoxLayout(settingsGroup);
    m_autoStartCheckBox = new QCheckBox("Start with Windows");
    m_minimizeToTrayCheckBox = new QCheckBox("Minimize to system tray on close");
    m_minimizeToTrayCheckBox->setChecked(true);
    settingsLayout->addWidget(m_autoStartCheckBox);
    settingsLayout->addWidget(m_minimizeToTrayCheckBox);
    m_startClippingAutomaticallyCheckBox = new QCheckBox("Enable clipping when app starts with Windows");
    settingsLayout->addWidget(m_startClippingAutomaticallyCheckBox);                                      
    layout->addWidget(settingsGroup);

    layout->addStretch();
    return tab;
}

QGroupBox *MainWindow::createAdvancedX264Settings()
{
    m_advancedX264Group = new QGroupBox("Advanced Software (x264/x265) Settings");
    QGridLayout *layout = new QGridLayout(m_advancedX264Group);
    layout->setSpacing(10);

    layout->addWidget(new QLabel("CPU Usage Preset (higher = less CPU):"), 0, 0);
    m_x264PresetCombo = new QComboBox;
    m_x264PresetCombo->addItems({"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow", "placebo"});
    connect(m_x264PresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onEncodingSettingsChanged);
    layout->addWidget(m_x264PresetCombo, 0, 1);

    layout->addWidget(new QLabel("Profile:"), 1, 0);
    m_x264ProfileCombo = new QComboBox;
    m_x264ProfileCombo->addItems({"high", "main", "baseline"});
    connect(m_x264ProfileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onEncodingSettingsChanged);
    layout->addWidget(m_x264ProfileCombo, 1, 1);

    layout->addWidget(new QLabel("Tune:"), 2, 0);
    m_x264TuneCombo = new QComboBox;
    m_x264TuneCombo->addItems({"none", "film", "animation", "grain", "stillimage", "psnr", "ssim", "fastdecode", "zerolatency"});
    connect(m_x264TuneCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onEncodingSettingsChanged);
    layout->addWidget(m_x264TuneCombo, 2, 1);

    layout->addWidget(new QLabel("x264 Options (separated by space):"), 3, 0);
    m_x264OptionsEdit = new QLineEdit();
    connect(m_x264OptionsEdit, &QLineEdit::textChanged, this, &MainWindow::onEncodingSettingsChanged);
    layout->addWidget(m_x264OptionsEdit, 3, 1);

    layout->setColumnStretch(1, 1);
    return m_advancedX264Group;
}

QGroupBox *MainWindow::createAdvancedQsvSettings()
{
    m_advancedQsvGroup = new QGroupBox("Advanced Intel (QSV) Settings");
    QGridLayout *layout = new QGridLayout(m_advancedQsvGroup);
    layout->setSpacing(10);

    layout->addWidget(new QLabel("Preset:"), 0, 0);
    m_qsvPresetCombo = new QComboBox;
    m_qsvPresetCombo->addItem("Fastest", "veryfast");
    m_qsvPresetCombo->addItem("Balanced", "balanced");
    m_qsvPresetCombo->addItem("Highest Quality", "quality");
    connect(m_qsvPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onEncodingSettingsChanged);
    layout->addWidget(m_qsvPresetCombo, 0, 1);

    layout->addWidget(new QLabel("Profile:"), 1, 0);
    m_qsvProfileCombo = new QComboBox;
    m_qsvProfileCombo->addItems({"high", "main", "baseline"});
    connect(m_qsvProfileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onEncodingSettingsChanged);
    layout->addWidget(m_qsvProfileCombo, 1, 1);

    m_qsvLowPowerCheckBox = new QCheckBox("Low-Power Mode");
    connect(m_qsvLowPowerCheckBox, &QCheckBox::toggled, this, &MainWindow::onEncodingSettingsChanged);
    layout->addWidget(m_qsvLowPowerCheckBox, 2, 0, 1, 2);

    layout->setColumnStretch(1, 1);
    return m_advancedQsvGroup;
}

QGroupBox *MainWindow::createAdvancedAmfSettings()
{
    m_advancedAmfGroup = new QGroupBox("Advanced AMD (AMF/VCE) Settings");
    QGridLayout *layout = new QGridLayout(m_advancedAmfGroup);
    layout->setSpacing(10);

    layout->addWidget(new QLabel("Preset:"), 0, 0);
    m_amfUsageCombo = new QComboBox;
    m_amfUsageCombo->addItem("Quality", "quality");
    m_amfUsageCombo->addItem("Balanced", "balanced");
    m_amfUsageCombo->addItem("Speed", "speed");
    connect(m_amfUsageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onEncodingSettingsChanged);
    layout->addWidget(m_amfUsageCombo, 0, 1);

    layout->addWidget(new QLabel("Profile:"), 1, 0);
    m_amfProfileCombo = new QComboBox;
    m_amfProfileCombo->addItems({"high", "main", "baseline"});
    connect(m_amfProfileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onEncodingSettingsChanged);
    layout->addWidget(m_amfProfileCombo, 1, 1);

    layout->addWidget(new QLabel("Max B-frames:"), 2, 0);
    m_amfBramesSpinBox = new QSpinBox();
    m_amfBramesSpinBox->setRange(0, 16);
    connect(m_amfBramesSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onEncodingSettingsChanged);
    layout->addWidget(m_amfBramesSpinBox, 2, 1);

    layout->addWidget(new QLabel("AMF/FFmpeg Options:"), 3, 0);
    m_amfOptionsEdit = new QLineEdit();
    connect(m_amfOptionsEdit, &QLineEdit::textChanged, this, &MainWindow::onEncodingSettingsChanged);
    layout->addWidget(m_amfOptionsEdit, 3, 1);

    layout->setColumnStretch(1, 1);
    return m_advancedAmfGroup;
}

QWidget *MainWindow::createEncodingSettingsTab()
{
    QWidget *tab = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(tab);
    mainLayout->setSpacing(10);

    QGroupBox *basicGroup = new QGroupBox("Basic Settings");
    QGridLayout *basicLayout = new QGridLayout(basicGroup);
    basicLayout->setSpacing(10);
    basicLayout->addWidget(new QLabel("Encoder:"), 0, 0);
    m_encoderCombo = new QComboBox;
    connect(m_encoderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onEncodingSettingsChanged);
    basicLayout->addWidget(m_encoderCombo, 0, 1);
    basicLayout->addWidget(new QLabel("Rate Control:"), 1, 0);
    m_rateControlCombo = new QComboBox;
    m_rateControlCombo->addItems({"CBR (Constant Bitrate)", "CQP/CRF (Constant Quality)"});
    connect(m_rateControlCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onRateControlChanged);
    basicLayout->addWidget(m_rateControlCombo, 1, 1);
    m_bitrateLabel = new QLabel("Bitrate:");
    basicLayout->addWidget(m_bitrateLabel, 2, 0);
    m_bitrateSpinBox = new QSpinBox;
    m_bitrateSpinBox->setRange(1000, 100000);
    m_bitrateSpinBox->setValue(8000);
    m_bitrateSpinBox->setSuffix(" kbps");
    connect(m_bitrateSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onEncodingSettingsChanged);
    basicLayout->addWidget(m_bitrateSpinBox, 2, 1);
    m_crfLabel = new QLabel("Quality Level (CQ/CRF):");
    basicLayout->addWidget(m_crfLabel, 3, 0);
    m_crfSpinBox = new QSpinBox;
    m_crfSpinBox->setRange(1, 51);
    m_crfSpinBox->setValue(22);
    connect(m_crfSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onEncodingSettingsChanged);
    basicLayout->addWidget(m_crfSpinBox, 3, 1);

    basicLayout->addWidget(new QLabel("Keyframe Interval (0=auto):"), 4, 0);
    m_keyframeIntervalSpinBox = new QSpinBox();
    m_keyframeIntervalSpinBox->setRange(0, 10);
    m_keyframeIntervalSpinBox->setSuffix("s");
    connect(m_keyframeIntervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onEncodingSettingsChanged);
    basicLayout->addWidget(m_keyframeIntervalSpinBox, 4, 1);

    basicLayout->setColumnStretch(1, 1);
    mainLayout->addWidget(basicGroup);

    m_advancedNvencGroup = new QGroupBox("Advanced NVIDIA (NVENC) Settings");
    QGridLayout *advancedLayout = new QGridLayout(m_advancedNvencGroup);
    advancedLayout->setSpacing(10);
    advancedLayout->addWidget(new QLabel("Preset:"), 0, 0);
    m_nvencPresetCombo = new QComboBox;
    m_nvencPresetCombo->addItem("P1: Fastest (Lowest Quality)", "p1");
    m_nvencPresetCombo->addItem("P2: Faster", "p2");
    m_nvencPresetCombo->addItem("P3: Fast", "p3");
    m_nvencPresetCombo->addItem("P4: Medium", "p4");
    m_nvencPresetCombo->addItem("P5: Slow (Good Quality)", "p5");
    m_nvencPresetCombo->addItem("P6: Slower", "p6");
    m_nvencPresetCombo->addItem("P7: Slowest (Best Quality)", "p7");
    connect(m_nvencPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onEncodingSettingsChanged);
    advancedLayout->addWidget(m_nvencPresetCombo, 0, 1);
    advancedLayout->addWidget(new QLabel("Tuning:"), 1, 0);
    m_nvencTuningCombo = new QComboBox;
    m_nvencTuningCombo->addItem("High Quality", "hq");
    m_nvencTuningCombo->addItem("Low Latency", "ll");
    m_nvencTuningCombo->addItem("Ultra Low Latency", "ull");
    connect(m_nvencTuningCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onEncodingSettingsChanged);
    advancedLayout->addWidget(m_nvencTuningCombo, 1, 1);
    advancedLayout->addWidget(new QLabel("Multipass Mode:"), 2, 0);
    m_nvencMultipassCombo = new QComboBox;
    m_nvencMultipassCombo->addItem("Disabled", "disabled");
    m_nvencMultipassCombo->addItem("Quarter Resolution", "qres");
    m_nvencMultipassCombo->addItem("Full Resolution", "fullres");
    connect(m_nvencMultipassCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onEncodingSettingsChanged);
    advancedLayout->addWidget(m_nvencMultipassCombo, 2, 1);
    advancedLayout->addWidget(new QLabel("Profile:"), 3, 0);
    m_nvencProfileCombo = new QComboBox;
    m_nvencProfileCombo->addItems({"high", "main", "baseline"});
    connect(m_nvencProfileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onEncodingSettingsChanged);
    advancedLayout->addWidget(m_nvencProfileCombo, 3, 1);
    m_nvencLookaheadCheckBox = new QCheckBox("Look-ahead");
    connect(m_nvencLookaheadCheckBox, &QCheckBox::toggled, this, &MainWindow::onEncodingSettingsChanged);
    advancedLayout->addWidget(m_nvencLookaheadCheckBox, 4, 0);
    m_nvencPsychoVisualTuningCheckBox = new QCheckBox("Psycho Visual Tuning");
    connect(m_nvencPsychoVisualTuningCheckBox, &QCheckBox::toggled, this, &MainWindow::onEncodingSettingsChanged);
    advancedLayout->addWidget(m_nvencPsychoVisualTuningCheckBox, 4, 1);
    advancedLayout->addWidget(new QLabel("GPU:"), 5, 0);
    m_nvencGpuSpinBox = new QSpinBox;
    m_nvencGpuSpinBox->setRange(0, 8);
    connect(m_nvencGpuSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onEncodingSettingsChanged);
    advancedLayout->addWidget(m_nvencGpuSpinBox, 5, 1);
    advancedLayout->addWidget(new QLabel("Max B-frames:"), 6, 0);
    m_nvencMaxBFramesSpinBox = new QSpinBox;
    m_nvencMaxBFramesSpinBox->setRange(0, 4);
    connect(m_nvencMaxBFramesSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onEncodingSettingsChanged);
    advancedLayout->addWidget(m_nvencMaxBFramesSpinBox, 6, 1);
    advancedLayout->setColumnStretch(1, 1);
    mainLayout->addWidget(m_advancedNvencGroup);

    mainLayout->addWidget(createAdvancedX264Settings());
    mainLayout->addWidget(createAdvancedQsvSettings());
    mainLayout->addWidget(createAdvancedAmfSettings());

    mainLayout->addStretch();
    onRateControlChanged();
    return tab;
}

QWidget *MainWindow::createAudioSettingsTab()
{
    QWidget *tab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(tab);
    layout->setSpacing(15);

    QGroupBox *audioGroup = new QGroupBox("Desktop Audio");
    QVBoxLayout *audioLayout = new QVBoxLayout(audioGroup);
    m_audioEnabledCheckBox = new QCheckBox("Capture Desktop Audio");
    m_audioEnabledCheckBox->setChecked(true);
    connect(m_audioEnabledCheckBox, &QCheckBox::toggled, this, &MainWindow::onAudioSettingsChanged);
    audioLayout->addWidget(m_audioEnabledCheckBox);
    QHBoxLayout *deviceLayout = new QHBoxLayout;
    deviceLayout->addWidget(new QLabel("Device:"));
    m_audioDeviceCombo = new QComboBox;
    connect(m_audioDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onAudioDeviceChanged);
    deviceLayout->addWidget(m_audioDeviceCombo);
    m_refreshAudioButton = new QPushButton("Refresh");
    connect(m_refreshAudioButton, &QPushButton::clicked, this, &MainWindow::refreshAudioDevices);
    deviceLayout->addWidget(m_refreshAudioButton);
    audioLayout->addLayout(deviceLayout);
    QHBoxLayout *volumeLayout = new QHBoxLayout;
    volumeLayout->addWidget(new QLabel("Volume:"));
    m_audioVolumeSlider = new QSlider(Qt::Horizontal);
    m_audioVolumeSlider->setRange(0, 100);
    m_audioVolumeSlider->setValue(100);
    m_volumeLabel = new QLabel("100%");
    m_volumeLabel->setMinimumWidth(40);
    connect(m_audioVolumeSlider, &QSlider::valueChanged, [this](int value)
            {
        m_volumeLabel->setText(QString("%1%").arg(value));
        onAudioSettingsChanged(); });
    volumeLayout->addWidget(m_audioVolumeSlider);
    volumeLayout->addWidget(m_volumeLabel);
    audioLayout->addLayout(volumeLayout);
    m_showAudioLevelsCheckBox = new QCheckBox("Show Audio Levels");
    connect(m_showAudioLevelsCheckBox, &QCheckBox::toggled, this, &MainWindow::onShowAudioLevelsChanged);
    audioLayout->addWidget(m_showAudioLevelsCheckBox);
    m_audioVisualizer = new AudioVisualizer;
    m_audioVisualizer->setVisible(false);
    audioLayout->addWidget(m_audioVisualizer);
    layout->addWidget(audioGroup);

    QGroupBox *micGroup = new QGroupBox("Microphone");
    QVBoxLayout *micLayout = new QVBoxLayout(micGroup);
    m_micEnabledCheckBox = new QCheckBox("Capture Microphone");
    connect(m_micEnabledCheckBox, &QCheckBox::toggled, this, &MainWindow::onMicrophoneSettingsChanged);
    micLayout->addWidget(m_micEnabledCheckBox);
    QHBoxLayout *micDeviceLayout = new QHBoxLayout;
    micDeviceLayout->addWidget(new QLabel("Device:"));
    m_micDeviceCombo = new QComboBox;
    connect(m_micDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onMicrophoneDeviceChanged);
    micDeviceLayout->addWidget(m_micDeviceCombo);
    m_refreshMicButton = new QPushButton("Refresh");
    connect(m_refreshMicButton, &QPushButton::clicked, this, &MainWindow::refreshMicrophoneDevices);
    micDeviceLayout->addWidget(m_refreshMicButton);
    micLayout->addLayout(micDeviceLayout);
    QHBoxLayout *micVolumeLayout = new QHBoxLayout;
    micVolumeLayout->addWidget(new QLabel("Volume:"));
    m_micVolumeSlider = new QSlider(Qt::Horizontal);
    m_micVolumeSlider->setRange(0, 100);
    m_micVolumeSlider->setValue(100);
    m_micVolumeLabel = new QLabel("100%");
    m_micVolumeLabel->setMinimumWidth(40);
    connect(m_micVolumeSlider, &QSlider::valueChanged, [this](int value)
            {
        m_micVolumeLabel->setText(QString("%1%").arg(value));
        onMicrophoneSettingsChanged(); });
    micVolumeLayout->addWidget(m_micVolumeSlider);
    micVolumeLayout->addWidget(m_micVolumeLabel);
    micLayout->addLayout(micVolumeLayout);
    m_showMicLevelsCheckBox = new QCheckBox("Show Microphone Levels");
    connect(m_showMicLevelsCheckBox, &QCheckBox::toggled, this, &MainWindow::onShowMicLevelsChanged);
    micLayout->addWidget(m_showMicLevelsCheckBox);
    m_microphoneVisualizer = new AudioVisualizer;
    m_microphoneVisualizer->setVisible(false);
    micLayout->addWidget(m_microphoneVisualizer);
    layout->addWidget(micGroup);

    layout->addStretch();
    return tab;
}

QWidget *MainWindow::createNotificationSettingsTab()
{
    QWidget *tab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(tab);
    layout->setSpacing(15);

    QGroupBox *soundGroup = new QGroupBox("Sound Notification");
    QVBoxLayout *soundLayout = new QVBoxLayout(soundGroup);
    m_soundEnabledCheckBox = new QCheckBox("Play sound when clip is saved");
    m_soundEnabledCheckBox->setChecked(true);
    soundLayout->addWidget(m_soundEnabledCheckBox);
    layout->addWidget(soundGroup);

    QGroupBox *trayGroup = new QGroupBox("Tray Notifications");
    QVBoxLayout *trayLayout = new QVBoxLayout(trayGroup);
    m_trayNotificationsCheckBox = new QCheckBox("Show notification when clip is saved");
    m_trayNotificationsCheckBox->setChecked(true);
    trayLayout->addWidget(m_trayNotificationsCheckBox);
    layout->addWidget(trayGroup);

    connect(m_soundEnabledCheckBox, &QCheckBox::toggled, this, &MainWindow::onNotificationSettingsChanged);
    connect(m_trayNotificationsCheckBox, &QCheckBox::toggled, this, &MainWindow::onNotificationSettingsChanged);

    layout->addStretch();
    return tab;
}

void MainWindow::setupMenuBar()
{
    QMenuBar *menuBar = this->menuBar();

    m_settingsMenu = menuBar->addMenu("Settings");
    m_keybindAction = m_settingsMenu->addAction("Keybinds...");
    connect(m_keybindAction, &QAction::triggered, this, &MainWindow::showKeybindSettings);

    m_helpMenu = menuBar->addMenu("Help");
    m_showLogsAction = m_helpMenu->addAction("Show Logs");
    connect(m_showLogsAction, &QAction::triggered, this, &MainWindow::showLogs);
}

void MainWindow::setupTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(windowIcon());
    m_trayIcon->setToolTip("OBS Replay Companion");

    QMenu *trayMenu = new QMenu(this);
    trayMenu->addAction("Show Window", this, &MainWindow::showFromTray);
    trayMenu->addAction("Toggle Clipping", this, &MainWindow::toggleClippingMode);
    trayMenu->addAction("Save Clip", this, &MainWindow::saveClip);
    trayMenu->addSeparator();
    trayMenu->addAction("Exit", this, &MainWindow::exitApplication);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason)
            {
        if (reason == QSystemTrayIcon::DoubleClick) showFromTray(); });

    m_trayIcon->setContextMenu(trayMenu);
    m_trayIcon->show();
}

void MainWindow::setupGlobalHotkeys()
{
    m_globalHotkey = new GlobalHotkey(this);
    connect(m_globalHotkey, &GlobalHotkey::hotkeyPressed, this, &MainWindow::onGlobalHotkeyPressed);
    onKeybindsChanged(m_keybindSettings);
}

void MainWindow::applyStyles()
{
    setStyleSheet(R"(
        QMainWindow { background-color: #000000; }
        QWidget { color: #e0e0e0; font-family: Inter, sans-serif; }
        QGroupBox { font-weight: bold; border: 1px solid #333333; border-radius: 6px; margin-top: 8px; padding: 10px; background-color: #121212; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }
        QLabel { background-color: transparent; }
        QPushButton { background-color: #222222; border: 1px solid #444444; border-radius: 4px; padding: 8px 16px; font-weight: bold; color: #e0e0e0; }
        QPushButton:hover { background-color: #333333; border-color: #555555; }
        QPushButton:pressed { background-color: #1a1a1a; }
        QPushButton:disabled { background-color: #1a1a1a; color: #555555; border-color: #333333; }
        QPushButton#ClippingButton[clippingActive="true"] { background-color: #ffffff; color: #000000; border: 1px solid #ffffff; }
        QPushButton#ClippingButton[clippingActive="true"]:hover { background-color: #e0e0e0; border-color: #e0e0e0; }
        QPushButton#SaveClipButton { background-color: #333333; border: 1px solid #cccccc; }
        QPushButton#SaveClipButton:hover { background-color: #444444; }
        QComboBox, QSpinBox, QLineEdit, QListWidget { background-color: #111111; border: 1px solid #444444; border-radius: 4px; padding: 5px 8px; }
        QComboBox:disabled, QSpinBox:disabled, QLineEdit:disabled, QListWidget:disabled { color: #555555; background-color: #1a1a1a; }
        QComboBox:editable { background-color: #111111; }
        QProgressBar { background-color: #111111; border: 1px solid #444444; border-radius: 4px; text-align: center; color: #e0e0e0; }
        QProgressBar::chunk { background-color: #ffffff; border-radius: 3px; }
        QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #555555; border-radius: 3px; }
        QCheckBox::indicator:checked { background-color: #ffffff; }
        QCheckBox:disabled { color: #555555; }
        QSlider::groove:horizontal { border: 1px solid #333333; height: 2px; background: #222222; margin: 2px 0; border-radius: 1px; }
        QSlider::handle:horizontal { background: #e0e0e0; border: 1px solid #e0e0e0; width: 14px; height: 14px; margin: -7px 0; border-radius: 7px; }
        QTabWidget::pane { border: 1px solid #333333; border-top: none; border-radius: 0 0 6px 6px; background-color: #121212; }
        QTabBar::tab { background: #121212; border: 1px solid #333333; padding: 8px 16px; border-top-left-radius: 6px; border-top-right-radius: 6px; }
        QTabBar::tab:selected { background: #222222; color: #ffffff; border-bottom: 1px solid #222222; }
        QTabBar::tab:!selected { color: #888888; margin-top: 2px; }
    )");
}

void MainWindow::loadSettings()
{
    qDebug() << "--- Loading settings ---";
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "GameClipRecorder", "Settings");

    // Block signals on widgets to prevent premature saves during loading
    m_resolutionCombo->blockSignals(true);
    m_fpsCombo->blockSignals(true);
    m_autoStartCheckBox->blockSignals(true);
    m_minimizeToTrayCheckBox->blockSignals(true);
    m_startClippingAutomaticallyCheckBox->blockSignals(true); // <-- ADDED
    m_clipLengthCombo->blockSignals(true);
    m_rateControlCombo->blockSignals(true);
    m_bitrateSpinBox->blockSignals(true);
    m_crfSpinBox->blockSignals(true);
    m_keyframeIntervalSpinBox->blockSignals(true);
    m_x264PresetCombo->blockSignals(true);
    m_x264ProfileCombo->blockSignals(true);
    m_x264TuneCombo->blockSignals(true);
    m_x264OptionsEdit->blockSignals(true);
    m_nvencPresetCombo->blockSignals(true);
    m_nvencTuningCombo->blockSignals(true);
    m_nvencMultipassCombo->blockSignals(true);
    m_nvencProfileCombo->blockSignals(true);
    m_nvencLookaheadCheckBox->blockSignals(true);
    m_nvencPsychoVisualTuningCheckBox->blockSignals(true);
    m_nvencGpuSpinBox->blockSignals(true);
    m_nvencMaxBFramesSpinBox->blockSignals(true);
    m_qsvPresetCombo->blockSignals(true);
    m_qsvProfileCombo->blockSignals(true);
    m_qsvLowPowerCheckBox->blockSignals(true);
    m_amfUsageCombo->blockSignals(true);
    m_amfProfileCombo->blockSignals(true);
    m_amfBramesSpinBox->blockSignals(true);
    m_amfOptionsEdit->blockSignals(true);
    m_audioVolumeSlider->blockSignals(true);
    m_audioEnabledCheckBox->blockSignals(true);
    m_showAudioLevelsCheckBox->blockSignals(true);
    m_micEnabledCheckBox->blockSignals(true);
    m_micVolumeSlider->blockSignals(true);
    m_showMicLevelsCheckBox->blockSignals(true);
    m_soundEnabledCheckBox->blockSignals(true);
    m_trayNotificationsCheckBox->blockSignals(true);

    // Video settings must be loaded and applied before OBS initialization.
    CaptureSettings captureSettings = m_capture->GetSettings();
    QString resolutionStr = settings.value("videoResolution", "1920x1080").toString();
    QStringList resParts = resolutionStr.split('x');

    if (resParts.size() == 2 && resParts[0].toInt() > 0 && resParts[1].toInt() > 0)
    {
        captureSettings.width = resParts[0].toInt();
        captureSettings.height = resParts[1].toInt();
    }
    else
    {
        qWarning() << "Invalid resolution string in settings:" << resolutionStr << ". Falling back to 1920x1080.";
        resolutionStr = "1920x1080";
        captureSettings.width = 1920;
        captureSettings.height = 1080;
    }

    captureSettings.fps = settings.value("videoFps", 60).toInt();
    m_capture->SetSettings(captureSettings);
    m_resolutionCombo->setCurrentText(resolutionStr);
    m_fpsCombo->setCurrentText(QString::number(captureSettings.fps));

    m_outputFolder = settings.value("outputFolder", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Clips").toString();
    m_outputPathEdit->setText(m_outputFolder);
    m_capture->SetOutputFolder(m_outputFolder);

    m_gameExes.clear();
    m_gameList->clear();
    int size = settings.beginReadArray("gameExes");
    for (int i = 0; i < size; ++i)
    {
        settings.setArrayIndex(i);
        QString exe = settings.value("exe").toString();
        if (!exe.isEmpty())
        {
            m_gameExes.insert(exe);
            m_gameList->addItem(exe);
        }
    }
    settings.endArray();

    // Proactively create directories for all known games.
    for (int i = 0; i < m_gameList->count(); ++i)
    {
        QString exeName = m_gameList->item(i)->text();
        QString gameName = QFileInfo(exeName).baseName();
        m_capture->EnsureDirectoryForGameName(gameName);
    }

    if (m_autoStartCheckBox)
    {
        m_autoStartCheckBox->setChecked(settings.value("autoStart", false).toBool());
    }
    if (m_minimizeToTrayCheckBox)
    {
        m_minimizeToTrayCheckBox->setChecked(settings.value("minimizeToTray", true).toBool());
    }
    if (m_startClippingAutomaticallyCheckBox)
    {
        m_startClippingAutomaticallyCheckBox->setChecked(settings.value("startClippingAutomatically", false).toBool());
    }

    m_clipLengthCombo->setCurrentText(settings.value("clipLength", "60s").toString());
    QString durationText = m_clipLengthCombo->currentText();
    int duration = durationText.left(durationText.length() - 1).toInt();
    m_capture->SetBufferDuration(duration);

    m_rateControlCombo->setCurrentIndex(settings.value("use_cbr", true).toBool() ? 0 : 1);
    m_bitrateSpinBox->setValue(settings.value("bitrate", 8000).toInt());
    m_crfSpinBox->setValue(settings.value("crf", 22).toInt());
    m_keyframeIntervalSpinBox->setValue(settings.value("keyint_sec", 0).toInt());

    m_x264PresetCombo->setCurrentText(settings.value("x264Preset", "veryfast").toString());
    m_x264ProfileCombo->setCurrentText(settings.value("x264Profile", "high").toString());
    m_x264TuneCombo->setCurrentText(settings.value("x264Tune", "zerolatency").toString());
    m_x264OptionsEdit->setText(settings.value("x264opts", "").toString());

    int presetIdx = m_nvencPresetCombo->findData(settings.value("nvencPreset", "p5").toString());
    if (presetIdx != -1)
        m_nvencPresetCombo->setCurrentIndex(presetIdx);
    int tuningIdx = m_nvencTuningCombo->findData(settings.value("nvencTuning", "hq").toString());
    if (tuningIdx != -1)
        m_nvencTuningCombo->setCurrentIndex(tuningIdx);
    int multipassIdx = m_nvencMultipassCombo->findData(settings.value("nvencMultipass", "qres").toString());
    if (multipassIdx != -1)
        m_nvencMultipassCombo->setCurrentIndex(multipassIdx);
    m_nvencProfileCombo->setCurrentText(settings.value("nvencProfile", "high").toString());
    m_nvencLookaheadCheckBox->setChecked(settings.value("nvencLookahead", false).toBool());
    m_nvencPsychoVisualTuningCheckBox->setChecked(settings.value("nvencPsychoVisualTuning", true).toBool());
    m_nvencGpuSpinBox->setValue(settings.value("nvencGpu", 0).toInt());
    m_nvencMaxBFramesSpinBox->setValue(settings.value("nvencMaxBFrames", 2).toInt());

    int qsvPresetIdx = m_qsvPresetCombo->findData(settings.value("qsvPreset", "balanced").toString());
    if (qsvPresetIdx != -1)
        m_qsvPresetCombo->setCurrentIndex(qsvPresetIdx);
    m_qsvProfileCombo->setCurrentText(settings.value("qsvProfile", "high").toString());
    m_qsvLowPowerCheckBox->setChecked(settings.value("qsvLowPower", false).toBool());

    int amfUsageIdx = m_amfUsageCombo->findData(settings.value("amfUsage", "quality").toString());
    if (amfUsageIdx != -1)
        m_amfUsageCombo->setCurrentIndex(amfUsageIdx);
    m_amfProfileCombo->setCurrentText(settings.value("amfProfile", "high").toString());
    m_amfBramesSpinBox->setValue(settings.value("amf_bframes", 2).toInt());
    m_amfOptionsEdit->setText(settings.value("amf_opts", "").toString());

    m_audioVolumeSlider->setValue(settings.value("audioVolume", 100).toInt());
    m_audioEnabledCheckBox->setChecked(settings.value("audioEnabled", true).toBool());
    m_showAudioLevelsCheckBox->setChecked(settings.value("showAudioLevels", false).toBool());

    m_micEnabledCheckBox->setChecked(settings.value("micEnabled", false).toBool());
    m_micVolumeSlider->setValue(settings.value("micVolume", 100).toInt());
    m_showMicLevelsCheckBox->setChecked(settings.value("showMicLevels", false).toBool());

    m_soundEnabledCheckBox->setChecked(settings.value("notificationSoundEnabled", true).toBool());
    m_trayNotificationsCheckBox->setChecked(settings.value("trayNotificationsEnabled", true).toBool());

    // Now that loading is complete, unblock all signals
    m_resolutionCombo->blockSignals(false);
    m_fpsCombo->blockSignals(false);
    m_autoStartCheckBox->blockSignals(false);
    m_minimizeToTrayCheckBox->blockSignals(false);
    m_startClippingAutomaticallyCheckBox->blockSignals(false); // <-- ADDED
    m_clipLengthCombo->blockSignals(false);
    m_rateControlCombo->blockSignals(false);
    m_bitrateSpinBox->blockSignals(false);
    m_crfSpinBox->blockSignals(false);
    m_keyframeIntervalSpinBox->blockSignals(false);
    m_x264PresetCombo->blockSignals(false);
    m_x264ProfileCombo->blockSignals(false);
    m_x264TuneCombo->blockSignals(false);
    m_x264OptionsEdit->blockSignals(false);
    m_nvencPresetCombo->blockSignals(false);
    m_nvencTuningCombo->blockSignals(false);
    m_nvencMultipassCombo->blockSignals(false);
    m_nvencProfileCombo->blockSignals(false);
    m_nvencLookaheadCheckBox->blockSignals(false);
    m_nvencPsychoVisualTuningCheckBox->blockSignals(false);
    m_nvencGpuSpinBox->blockSignals(false);
    m_nvencMaxBFramesSpinBox->blockSignals(false);
    m_qsvPresetCombo->blockSignals(false);
    m_qsvProfileCombo->blockSignals(false);
    m_qsvLowPowerCheckBox->blockSignals(false);
    m_amfUsageCombo->blockSignals(false);
    m_amfProfileCombo->blockSignals(false);
    m_amfBramesSpinBox->blockSignals(false);
    m_amfOptionsEdit->blockSignals(false);
    m_audioVolumeSlider->blockSignals(false);
    m_audioEnabledCheckBox->blockSignals(false);
    m_showAudioLevelsCheckBox->blockSignals(false);
    m_micEnabledCheckBox->blockSignals(false);
    m_micVolumeSlider->blockSignals(false);
    m_showMicLevelsCheckBox->blockSignals(false);
    m_soundEnabledCheckBox->blockSignals(false);
    m_trayNotificationsCheckBox->blockSignals(false);

    // After unblocking, manually call functions that need to update the UI state
    onRateControlChanged();
    onShowAudioLevelsChanged(m_showAudioLevelsCheckBox->isChecked());
    onShowMicLevelsChanged(m_showMicLevelsCheckBox->isChecked());
    m_keybindSettings.clipSave = QKeySequence(settings.value("keybind_clip", "F9").toString());
    m_keybindSettings.clippingModeToggle = QKeySequence(settings.value("keybind_clipping", "F10").toString());
    onKeybindsChanged(m_keybindSettings);

    // Re-connect signals that were disconnected in the constructor or setupUI
    connect(m_autoStartCheckBox, &QCheckBox::toggled, this, &MainWindow::onAutoStartChanged);
    connect(m_startClippingAutomaticallyCheckBox, &QCheckBox::toggled, this, &MainWindow::onStartClippingAutomaticallyChanged); // <-- ADDED
    connect(m_minimizeToTrayCheckBox, &QCheckBox::toggled, this, &MainWindow::saveSettings);
}

void MainWindow::saveSettings()
{
    qDebug() << "--- Saving settings ---";
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "GameClipRecorder", "Settings"); // Use consistent organization name

    settings.setValue("outputFolder", m_outputFolder);

    // Save games from the actual list widget, not from m_gameExes
    settings.beginWriteArray("gameExes");
    qDebug() << "Saving" << m_gameList->count() << "games.";
    for (int i = 0; i < m_gameList->count(); ++i)
    {
        settings.setArrayIndex(i);
        settings.setValue("exe", m_gameList->item(i)->text());
    }
    settings.endArray();

    if (m_autoStartCheckBox)
        settings.setValue("autoStart", m_autoStartCheckBox->isChecked());
    if (m_minimizeToTrayCheckBox)
        settings.setValue("minimizeToTray", m_minimizeToTrayCheckBox->isChecked());
    if (m_startClippingAutomaticallyCheckBox)
        settings.setValue("startClippingAutomatically", m_startClippingAutomaticallyCheckBox->isChecked()); // <-- ADDED

    settings.setValue("videoResolution", m_resolutionCombo->currentText());
    settings.setValue("videoFps", m_fpsCombo->currentText().toInt());

    settings.setValue("clipLength", m_clipLengthCombo->currentText());
    qDebug() << "Saving clipLength:" << m_clipLengthCombo->currentText();

    if (m_encoderCombo->currentIndex() >= 0)
    {
        settings.setValue("encoderType", m_encoderCombo->currentData().toMap()["type"].toInt());
    }
    settings.setValue("use_cbr", m_rateControlCombo->currentIndex() == 0);
    settings.setValue("bitrate", m_bitrateSpinBox->value());
    settings.setValue("crf", m_crfSpinBox->value());
    settings.setValue("keyint_sec", m_keyframeIntervalSpinBox->value());

    // x264
    settings.setValue("x264Preset", m_x264PresetCombo->currentText());
    settings.setValue("x264Profile", m_x264ProfileCombo->currentText());
    settings.setValue("x264Tune", m_x264TuneCombo->currentText());
    settings.setValue("x264opts", m_x264OptionsEdit->text());

    // NVENC
    if (m_nvencPresetCombo->currentIndex() >= 0)
        settings.setValue("nvencPreset", m_nvencPresetCombo->currentData().toString());
    if (m_nvencTuningCombo->currentIndex() >= 0)
        settings.setValue("nvencTuning", m_nvencTuningCombo->currentData().toString());
    if (m_nvencMultipassCombo->currentIndex() >= 0)
        settings.setValue("nvencMultipass", m_nvencMultipassCombo->currentData().toString());
    settings.setValue("nvencProfile", m_nvencProfileCombo->currentText());
    settings.setValue("nvencLookahead", m_nvencLookaheadCheckBox->isChecked());
    settings.setValue("nvencPsychoVisualTuning", m_nvencPsychoVisualTuningCheckBox->isChecked());
    settings.setValue("nvencGpu", m_nvencGpuSpinBox->value());
    settings.setValue("nvencMaxBFrames", m_nvencMaxBFramesSpinBox->value());

    // QSV
    if (m_qsvPresetCombo->currentIndex() >= 0)
        settings.setValue("qsvPreset", m_qsvPresetCombo->currentData().toString());
    settings.setValue("qsvProfile", m_qsvProfileCombo->currentText());
    settings.setValue("qsvLowPower", m_qsvLowPowerCheckBox->isChecked());

    // AMF
    if (m_amfUsageCombo->currentIndex() >= 0)
        settings.setValue("amfUsage", m_amfUsageCombo->currentData().toString());
    settings.setValue("amfProfile", m_amfProfileCombo->currentText());
    settings.setValue("amf_bframes", m_amfBramesSpinBox->value());
    settings.setValue("amf_opts", m_amfOptionsEdit->text());

    settings.setValue("audioVolume", m_audioVolumeSlider->value());
    settings.setValue("audioEnabled", m_audioEnabledCheckBox->isChecked());
    settings.setValue("showAudioLevels", m_showAudioLevelsCheckBox->isChecked());
    if (m_audioDeviceCombo)
        settings.setValue("audioDeviceID", m_audioDeviceCombo->currentData().toString());
    settings.setValue("micEnabled", m_micEnabledCheckBox->isChecked());
    settings.setValue("micVolume", m_micVolumeSlider->value());
    settings.setValue("showMicLevels", m_showMicLevelsCheckBox->isChecked());
    if (m_micDeviceCombo)
        settings.setValue("micDeviceID", m_micDeviceCombo->currentData().toString());
    settings.setValue("notificationSoundEnabled", m_soundEnabledCheckBox->isChecked());
    settings.setValue("trayNotificationsEnabled", m_trayNotificationsCheckBox->isChecked());

    // Save keybinds here too for consistency
    settings.setValue("keybind_clip", m_keybindSettings.clipSave.toString());
    settings.setValue("keybind_clipping", m_keybindSettings.clippingModeToggle.toString());

    // Force sync to disk
    settings.sync();

    qDebug() << "Settings synced to disk. Status:" << settings.status();
}

void MainWindow::startProcessMonitor()
{
    if (!m_processMonitorThread)
    {
        m_processMonitorThread = new QThread;
        m_processMonitor = new ProcessMonitor();
        m_processMonitor->moveToThread(m_processMonitorThread);
        connect(m_processMonitor, &ProcessMonitor::processStarted, this, &MainWindow::onProcessStarted);
        connect(m_processMonitor, &ProcessMonitor::processStopped, this, &MainWindow::onProcessStopped);
        connect(m_processMonitorThread, &QThread::started, m_processMonitor, &ProcessMonitor::startMonitoring);
        connect(m_processMonitorThread, &QThread::finished, m_processMonitor, &QObject::deleteLater);
        m_processMonitorThread->start();
    }
}

void MainWindow::stopProcessMonitor()
{
    if (m_processMonitorThread)
    {
        m_processMonitorThread->quit();
        m_processMonitorThread = nullptr;
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_minimizeToTrayCheckBox->isChecked() && m_trayIcon && m_trayIcon->isVisible())
    {
        hide();
        event->ignore();
    }
    else
    {
        exitApplication();
        event->accept();
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
}

void MainWindow::setSettingsLocked(bool locked)
{
    // Lock menu actions
    m_keybindAction->setDisabled(locked);

    // Main Controls
    m_clipLengthCombo->setDisabled(locked);

    // General Settings Tab
    m_outputPathEdit->setDisabled(locked);
    m_browseButton->setDisabled(locked);
    m_resolutionCombo->setDisabled(locked);
    m_fpsCombo->setDisabled(locked);
    m_addGameButton->setDisabled(locked);
    m_removeGameButton->setDisabled(locked);

    // Encoding Settings Tab
    m_encoderCombo->setDisabled(locked);
    m_rateControlCombo->setDisabled(locked);
    m_bitrateSpinBox->setDisabled(locked);
    m_crfSpinBox->setDisabled(locked);
    m_keyframeIntervalSpinBox->setDisabled(locked);
    m_advancedNvencGroup->setDisabled(locked);
    m_advancedX264Group->setDisabled(locked);
    m_advancedQsvGroup->setDisabled(locked);
    m_advancedAmfGroup->setDisabled(locked);

    // Audio Settings Tab
    m_audioEnabledCheckBox->setDisabled(locked);
    m_audioDeviceCombo->setDisabled(locked);
    m_refreshAudioButton->setDisabled(locked);
    m_micEnabledCheckBox->setDisabled(locked);
    m_micDeviceCombo->setDisabled(locked);
    m_refreshMicButton->setDisabled(locked);
}

void MainWindow::updateUiForState()
{
    switch (m_clippingState)
    {
    case DISABLED:
        m_clippingModeButton->setProperty("clippingActive", false);
        m_clippingModeButton->setChecked(false);
        m_clippingModeButton->setText("Enable Clipping");
        m_clippingModeStatus->setText("Clipping is Disabled");
        m_statusLabel->setText("Ready - Enable clipping to start");
        m_clipButton->setEnabled(false);
        setSettingsLocked(false);
        break;
    case AWAITING_GAME:
        m_clippingModeButton->setProperty("clippingActive", true);
        m_clippingModeButton->setChecked(true);
        m_clippingModeButton->setText("Disable Clipping");
        m_clippingModeStatus->setText("Clipping is Active");
        m_statusLabel->setText("Clipping is active - Waiting for a monitored game to start...");
        m_clipButton->setEnabled(false);
        setSettingsLocked(true);
        break;
    case ACTIVE:
        m_clippingModeButton->setProperty("clippingActive", true);
        m_clippingModeButton->setChecked(true);
        m_clippingModeButton->setText("Disable Clipping");
        m_clippingModeStatus->setText("Clipping is Active");
        m_statusLabel->setText(QString("Game detected: %1").arg(m_currentDetectedGame));
        m_clipButton->setEnabled(true); // Buffer is running
        setSettingsLocked(true);
        break;
    }
    // The setProperty call is sufficient for Qt to update the style.
}

void MainWindow::toggleClippingMode()
{
    if (!m_capture->IsInitialized())
    {
        QMessageBox::warning(this, "Warning", "OBS is not initialized yet. Please wait.");
        return;
    }

    if (m_clippingState == DISABLED)
    {
        m_clippingState = AWAITING_GAME;
        startProcessMonitor();
    }
    else
    {
        m_clippingState = DISABLED;
        stopProcessMonitor();
        m_capture->StopClippingMode(); // Stops buffer if active
        m_gameDetected = false;
        m_currentDetectedGame.clear();
        m_capture->ClearCapture();
    }
    updateUiForState();
}

void MainWindow::onClippingModeChanged(bool active)
{
    // This signal from GameCapture confirms the actual state of the replay buffer.
    // The main UI state is managed by the state machine, but we can call
    // updateUiForState() here to ensure everything is visually consistent with
    // the buffer's actual status.
    Q_UNUSED(active);
    updateUiForState();
}

void MainWindow::onProcessStarted(const QString &exeName)
{
    // Only act if we are waiting for a game and haven't found one yet.
    if (m_clippingState == AWAITING_GAME && m_gameExes.contains(exeName))
    {
        m_gameDetected = true;
        m_currentDetectedGame = exeName;

        m_statusLabel->setText(QString("Starting buffer for %1...").arg(exeName));
        m_capture->SetGameCapture(exeName.toStdString());

        if (m_capture->StartClippingMode())
        {
            m_clippingState = ACTIVE;
        }
        else
        {
            QMessageBox::critical(this, "Error", "Failed to start clipping buffer!");
            // Revert state if starting the buffer failed
            m_gameDetected = false;
            m_currentDetectedGame.clear();
            m_capture->ClearCapture();
        }
        updateUiForState(); // Update UI to ACTIVE or back to AWAITING_GAME on failure
    }
}

void MainWindow::onProcessStopped(const QString &exeName)
{
    // Only act if clipping is active and the correct game has closed.
    if (m_clippingState == ACTIVE && exeName.compare(m_currentDetectedGame, Qt::CaseInsensitive) == 0)
    {
        m_clippingState = AWAITING_GAME;
        m_gameDetected = false;
        m_currentDetectedGame.clear();

        m_capture->StopClippingMode();
        m_capture->ClearCapture();

        updateUiForState(); // Update UI to reflect we are waiting again.
    }
}

void MainWindow::saveClip()
{
    // Disable the button immediately to prevent spamming
    m_clipButton->setEnabled(false);

    int duration = m_clipLengthCombo->currentText().remove('s').toInt();
    m_capture->SaveInstantReplay(duration, "");
}

void MainWindow::addGameExe()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Select Game Executable", "", "*.exe");
    if (!fileName.isEmpty())
    {
        QString exeName = QFileInfo(fileName).fileName();
        if (!m_gameExes.contains(exeName))
        {
            qDebug() << "Adding game:" << exeName;
            m_gameExes.insert(exeName);
            m_gameList->addItem(exeName);

            // Proactively create directory for the new game
            QString gameName = QFileInfo(exeName).baseName();
            m_capture->EnsureDirectoryForGameName(gameName);

            saveSettings();
        }
    }
}

void MainWindow::removeGameExe()
{
    QListWidgetItem *item = m_gameList->currentItem();
    if (item)
    {
        QString itemText = item->text();
        qDebug() << "Removing game:" << itemText;
        m_gameExes.remove(itemText);
        delete m_gameList->takeItem(m_gameList->row(item));
        saveSettings();
    }
}

void MainWindow::browseOutputFolder()
{
    QString folder = QFileDialog::getExistingDirectory(this, "Select Output Folder", m_outputFolder);
    if (!folder.isEmpty())
    {
        m_outputFolder = folder;
        m_outputPathEdit->setText(folder);
        m_capture->SetOutputFolder(folder);
        saveSettings();
    }
}

void MainWindow::showKeybindSettings()
{
    if (!m_keybindDialog)
    {
        m_keybindDialog = new KeybindDialog(this);
        connect(m_keybindDialog, &KeybindDialog::keybindsChanged, this, &MainWindow::onKeybindsChanged);
    }
    m_keybindDialog->setKeybindSettings(m_keybindSettings);
    m_keybindDialog->snapshotOriginalSettings();

    if (m_globalHotkey)
        m_globalHotkey->unregisterAllHotkeys();
    m_keybindDialog->exec();
    onKeybindsChanged(m_keybindSettings);
}

void MainWindow::showFromTray()
{
    show();
    raise();
    activateWindow();
}

void MainWindow::exitApplication()
{
    if (m_trayIcon)
        m_trayIcon->hide();
    QApplication::quit();
}

void MainWindow::onClipLengthChanged()
{
    qDebug() << "onClipLengthChanged triggered.";
    QString durationText = m_clipLengthCombo->currentText();
    int duration = durationText.left(durationText.length() - 1).toInt();
    m_capture->SetBufferDuration(duration);
    saveSettings();
}

void MainWindow::onEncodingSettingsChanged()
{
    if (m_encoderCombo->currentIndex() < 0)
        return;

    QVariantMap data = m_encoderCombo->currentData().toMap();
    QString currentEncoderId = data["id"].toString();
    EncoderType currentEncoderType = static_cast<EncoderType>(data["type"].toInt());

    bool is_nvenc = currentEncoderId.contains("nvenc");
    bool is_qsv = currentEncoderId.contains("qsv");
    bool is_amf = currentEncoderId.contains("amf");
    bool is_software = currentEncoderId.contains("x264") || currentEncoderId.contains("x265");

    m_advancedNvencGroup->setVisible(is_nvenc);
    m_advancedX264Group->setVisible(is_software);
    m_advancedQsvGroup->setVisible(is_qsv);
    m_advancedAmfGroup->setVisible(is_amf);

    bool is_hevc = (currentEncoderType == EncoderType::NVENC_HEVC ||
                    currentEncoderType == EncoderType::QSV_HEVC ||
                    currentEncoderType == EncoderType::AMF_HEVC ||
                    currentEncoderType == EncoderType::X265);

    updateProfileComboBox(m_nvencProfileCombo, is_hevc, {"high", "main", "baseline"}, {"main", "main10", "rext"});
    updateProfileComboBox(m_qsvProfileCombo, is_hevc, {"high", "main", "baseline"}, {"main", "main10"});
    updateProfileComboBox(m_amfProfileCombo, is_hevc, {"high", "main", "baseline"}, {"main"});
    updateProfileComboBox(m_x264ProfileCombo, is_hevc, {"high", "main", "baseline"}, {"main", "main10", "main12"});

    if (is_software)
    {
        m_rateControlCombo->setItemText(1, "CRF (Constant Rate Factor)");
        m_crfLabel->setText("CRF Level (0-51):");
        m_crfSpinBox->setToolTip("Lower is better quality. 18-28 is a sane range. 0 is lossless.");
    }
    else
    {
        m_rateControlCombo->setItemText(1, "CQP (Constant Quality)");
        m_crfLabel->setText("CQ Level (1-30):");
        m_crfSpinBox->setToolTip("Lower is better quality. 20-25 is a sane range.");
    }

    EncodingSettings settings;
    settings.encoder = static_cast<EncoderType>(data["type"].toInt());
    settings.use_cbr = (m_rateControlCombo->currentIndex() == 0);
    settings.bitrate = m_bitrateSpinBox->value();
    settings.crf = m_crfSpinBox->value();
    settings.keyint_sec = m_keyframeIntervalSpinBox->value();

    // x264
    settings.x264Preset = m_x264PresetCombo->currentText().toStdString();
    settings.x264Profile = m_x264ProfileCombo->currentText().toStdString();
    settings.x264Tune = m_x264TuneCombo->currentText().toStdString();
    settings.x264opts = m_x264OptionsEdit->text().toStdString();

    // NVENC
    if (m_nvencPresetCombo->currentIndex() >= 0)
        settings.nvencPreset = m_nvencPresetCombo->currentData().toString().toStdString();
    if (m_nvencTuningCombo->currentIndex() >= 0)
        settings.nvencTuning = m_nvencTuningCombo->currentData().toString().toStdString();
    if (m_nvencMultipassCombo->currentIndex() >= 0)
        settings.nvencMultipass = m_nvencMultipassCombo->currentData().toString().toStdString();
    settings.nvencProfile = m_nvencProfileCombo->currentText().toStdString();
    settings.nvencLookahead = m_nvencLookaheadCheckBox->isChecked();
    settings.nvencPsychoVisualTuning = m_nvencPsychoVisualTuningCheckBox->isChecked();
    settings.nvencGpu = m_nvencGpuSpinBox->value();
    settings.nvencMaxBFrames = m_nvencMaxBFramesSpinBox->value();

    // QSV
    if (m_qsvPresetCombo->currentIndex() >= 0)
        settings.qsvPreset = m_qsvPresetCombo->currentData().toString().toStdString();
    settings.qsvProfile = m_qsvProfileCombo->currentText().toStdString();
    settings.qsvLowPower = m_qsvLowPowerCheckBox->isChecked();

    // AMF
    if (m_amfUsageCombo->currentIndex() >= 0)
        settings.amfUsage = m_amfUsageCombo->currentData().toString().toStdString();
    settings.amfProfile = m_amfProfileCombo->currentText().toStdString();
    settings.amf_bframes = m_amfBramesSpinBox->value();
    settings.amf_opts = m_amfOptionsEdit->text().toStdString();

    m_capture->UpdateEncodingSettings(settings);
    saveSettings();
}

void MainWindow::onRateControlChanged()
{
    bool isCBR = (m_rateControlCombo->currentIndex() == 0);
    m_bitrateLabel->setVisible(isCBR);
    m_bitrateSpinBox->setVisible(isCBR);
    m_crfLabel->setVisible(!isCBR);
    m_crfSpinBox->setVisible(!isCBR);
    onEncodingSettingsChanged();
}

void MainWindow::onAudioSettingsChanged()
{
    if (!m_capture || !m_capture->IsInitialized())
        return;
    AudioSettings settings;
    settings.enabled = m_audioEnabledCheckBox->isChecked();
    settings.volume = m_audioVolumeSlider->value() / 100.0f;
    settings.deviceId = m_audioDeviceCombo->currentIndex() >= 0
                            ? m_audioDeviceCombo->currentData().toString().toStdString()
                            : "default";
    m_capture->UpdateAudioSettings(settings);

    static QString lastDeviceId;
    if (QString::fromStdString(settings.deviceId) != lastDeviceId)
    {
        QTimer::singleShot(500, this, &MainWindow::setupAudioVolmeter);
        lastDeviceId = QString::fromStdString(settings.deviceId);
    }
    saveSettings();
}

void MainWindow::onMicrophoneSettingsChanged()
{
    if (!m_capture || !m_capture->IsInitialized())
        return;
    MicrophoneSettings settings;
    settings.enabled = m_micEnabledCheckBox->isChecked();
    settings.volume = m_micVolumeSlider->value() / 100.0f;
    settings.deviceId = m_micDeviceCombo->currentIndex() >= 0
                            ? m_micDeviceCombo->currentData().toString().toStdString()
                            : "default";
    m_capture->UpdateMicrophoneSettings(settings);

    static QString lastMicDeviceId;
    if (QString::fromStdString(settings.deviceId) != lastMicDeviceId)
    {
        QTimer::singleShot(500, this, &MainWindow::setupMicrophoneVolmeter);
        lastMicDeviceId = QString::fromStdString(settings.deviceId);
    }
    saveSettings();
}

void MainWindow::onVideoSettingsChanged()
{
    saveSettings();
}

void MainWindow::onNotificationSettingsChanged()
{
    saveSettings();
}

void MainWindow::onKeybindsChanged(const KeybindSettings &settings)
{
    m_keybindSettings = settings;
    if (m_globalHotkey)
    {
        m_globalHotkey->unregisterAllHotkeys();
        m_globalHotkey->registerHotkey(HOTKEY_SAVE_CLIP, settings.clipSave);
        m_globalHotkey->registerHotkey(HOTKEY_TOGGLE_CLIPPING, settings.clippingModeToggle);
    }
    // Save settings immediately when keybinds change
    saveSettings();
}

void MainWindow::onAutoStartChanged(bool checked)
{
    checked ? setupAutoStart() : removeAutoStart();

    // If "Start with Windows" is disabled, the auto-clip feature must also be disabled.
    if (!checked && m_startClippingAutomaticallyCheckBox->isChecked())
    {
        m_startClippingAutomaticallyCheckBox->setChecked(false); // This will trigger the other slot.
    }
    else
    {
        saveSettings();
    }
}

void MainWindow::onStartClippingAutomaticallyChanged(bool checked)
{
    // If "Enable clipping on start" is checked, "Start with Windows" must also be checked.
    if (checked && !m_autoStartCheckBox->isChecked())
    {
        m_autoStartCheckBox->setChecked(true); // This triggers onAutoStartChanged, which saves settings and updates the registry.
    }
    else
    {
        // If it's unchecked, or if auto-start was already enabled, we still need to update the registry
        // to either add or remove the "--autostart" argument.
        setupAutoStart();
        saveSettings();
    }
}

void MainWindow::onShowAudioLevelsChanged(bool enabled)
{
    m_audioVisualizer->setVisible(enabled);
    if (enabled)
    {
        setupAudioVolmeter();
        if (!m_visualizerUpdateTimer->isActive())
            m_visualizerUpdateTimer->start(200); // Start timer if not running
    }
    else
    {
        if (m_audioVolmeter)
        {
            obs_volmeter_attach_source(m_audioVolmeter, nullptr);
        }
        // Stop timer only if the other visualizer is also disabled
        if (!m_showMicLevelsCheckBox->isChecked() && m_visualizerUpdateTimer->isActive())
        {
            m_visualizerUpdateTimer->stop();
        }
    }
    saveSettings();
}

void MainWindow::onShowMicLevelsChanged(bool enabled)
{
    m_microphoneVisualizer->setVisible(enabled);
    if (enabled)
    {
        setupMicrophoneVolmeter();
        if (!m_visualizerUpdateTimer->isActive())
            m_visualizerUpdateTimer->start(200); // Start timer if not running
    }
    else
    {
        if (m_microphoneVolmeter)
        {
            obs_volmeter_attach_source(m_microphoneVolmeter, nullptr);
        }
        // Stop timer only if the other visualizer is also disabled
        if (!m_showAudioLevelsCheckBox->isChecked() && m_visualizerUpdateTimer->isActive())
        {
            m_visualizerUpdateTimer->stop();
        }
    }
    saveSettings();
}

void MainWindow::refreshEncoders()
{
    m_encoderCombo->blockSignals(true);
    m_encoderCombo->clear();

    auto encoders = m_capture->GetAvailableEncoders();
    for (const auto &encoder : encoders)
    {
        QVariantMap data;
        data["type"] = static_cast<int>(encoder.type);
        data["id"] = QString::fromStdString(encoder.id);
        m_encoderCombo->addItem(QString::fromStdString(encoder.name), data);
    }

    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "GameClipRecorder", "Settings"); // Use consistent organization name
    int savedEncoderType = settings.value("encoderType", -1).toInt();
    int indexToSelect = -1;

    // Try to find the user's previously saved encoder
    if (savedEncoderType != -1)
    {
        for (int i = 0; i < m_encoderCombo->count(); ++i)
        {
            if (m_encoderCombo->itemData(i).toMap()["type"].toInt() == savedEncoderType)
            {
                indexToSelect = i;
                break;
            }
        }
    }

    // If no saved encoder or saved one is not available, apply smart default
    if (indexToSelect == -1)
    {
        // 1. Prioritize NVIDIA NVENC H.264
        for (int i = 0; i < m_encoderCombo->count(); ++i)
        {
            if (m_encoderCombo->itemData(i).toMap()["type"].toInt() == static_cast<int>(EncoderType::NVENC_H264))
            {
                indexToSelect = i;
                break;
            }
        }
        // 2. Fallback to x264 if NVENC not found
        if (indexToSelect == -1)
        {
            for (int i = 0; i < m_encoderCombo->count(); ++i)
            {
                if (m_encoderCombo->itemData(i).toMap()["type"].toInt() == static_cast<int>(EncoderType::X264))
                {
                    indexToSelect = i;
                    break;
                }
            }
        }
        // 3. Last resort: select the first available encoder
        if (indexToSelect == -1 && m_encoderCombo->count() > 0)
        {
            indexToSelect = 0;
        }
    }

    if (indexToSelect != -1)
    {
        m_encoderCombo->setCurrentIndex(indexToSelect);
    }

    m_encoderCombo->blockSignals(false);
}

void MainWindow::onGlobalHotkeyPressed(int id)
{
    switch (id)
    {
    case HOTKEY_SAVE_CLIP:
        saveClip();
        break;
    case HOTKEY_TOGGLE_CLIPPING:
        m_clippingModeButton->click(); // Simulate a click to use the same logic
        break;
    }
}

void MainWindow::refreshAudioDevices()
{
    QThread *thread = new QThread;
    AudioDeviceFetcher *worker = new AudioDeviceFetcher();
    worker->moveToThread(thread);

    // Connect signals for results and automatic cleanup
    connect(thread, &QThread::started, worker, &AudioDeviceFetcher::fetchOutputDevices);
    connect(worker, &AudioDeviceFetcher::outputDevicesFetched, this, &MainWindow::onAudioDevicesReceived);
    connect(worker, &AudioDeviceFetcher::outputDevicesFetched, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
}

void MainWindow::refreshMicrophoneDevices()
{
    QThread *thread = new QThread;
    AudioDeviceFetcher *worker = new AudioDeviceFetcher();
    worker->moveToThread(thread);

    // Connect signals for results and automatic cleanup
    connect(thread, &QThread::started, worker, &AudioDeviceFetcher::fetchInputDevices);
    connect(worker, &AudioDeviceFetcher::inputDevicesFetched, this, &MainWindow::onMicrophoneDevicesReceived);
    connect(worker, &AudioDeviceFetcher::inputDevicesFetched, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
}

void MainWindow::onAudioDevicesReceived(const QList<QPair<QString, QString>> &devices)
{
    QString currentId = m_audioDeviceCombo->currentData().toString();
    m_audioDeviceCombo->blockSignals(true);
    m_audioDeviceCombo->clear();
    for (const auto &device : devices)
    {
        m_audioDeviceCombo->addItem(device.second, device.first);
    }
    int index = m_audioDeviceCombo->findData(currentId);
    if (index == -1)
    {
        QSettings settings("GameClipRecorder", "Settings");
        QString savedId = settings.value("audioDeviceID", "default").toString();
        index = m_audioDeviceCombo->findData(savedId);
    }
    if (index != -1)
        m_audioDeviceCombo->setCurrentIndex(index);
    m_audioDeviceCombo->blockSignals(false);
    if (m_audioDeviceCombo->currentData().toString() != currentId)
    {
        onAudioSettingsChanged();
    }
}

void MainWindow::onMicrophoneDevicesReceived(const QList<QPair<QString, QString>> &devices)
{
    QString currentId = m_micDeviceCombo->currentData().toString();
    m_micDeviceCombo->blockSignals(true);
    m_micDeviceCombo->clear();
    for (const auto &device : devices)
    {
        m_micDeviceCombo->addItem(device.second, device.first);
    }
    int index = m_micDeviceCombo->findData(currentId);
    if (index == -1)
    {
        QSettings settings("GameClipRecorder", "Settings");
        QString savedId = settings.value("micDeviceID", "default").toString();
        index = m_micDeviceCombo->findData(savedId);
    }
    if (index != -1)
        m_micDeviceCombo->setCurrentIndex(index);
    m_micDeviceCombo->blockSignals(false);
    if (m_micDeviceCombo->currentData().toString() != currentId)
    {
        onMicrophoneSettingsChanged();
    }
}

void MainWindow::onAudioDeviceChanged() { onAudioSettingsChanged(); }
void MainWindow::onMicrophoneDeviceChanged() { onMicrophoneSettingsChanged(); }

void MainWindow::setupAutoStart()
{
    QSettings settings("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    QString appPath = QCoreApplication::applicationFilePath().replace('/', '\\');

    // Always quote the path to handle spaces correctly.
    QString command = QString("\"%1\"").arg(appPath);

    if (m_startClippingAutomaticallyCheckBox && m_startClippingAutomaticallyCheckBox->isChecked())
    {
        command += " --autostart";
    }

    settings.setValue("GameClipRecorder", command);
}

void MainWindow::removeAutoStart()
{
    QSettings settings("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    settings.remove("GameClipRecorder");
}

void MainWindow::playNotificationSound()
{
    QString soundPath = QCoreApplication::applicationDirPath() + "/sounds/notification_sound.wav";
    QString nativePath = QDir::toNativeSeparators(soundPath);

    mciSendString(L"close notification_sound", NULL, 0, NULL);

    QString openCommand = QString("open \"%1\" type waveaudio alias notification_sound").arg(nativePath);
    if (mciSendString(openCommand.toStdWString().c_str(), NULL, 0, NULL) != 0)
    {
        return;
    }

    int volume = 1000;
    QString volumeCommand = QString("setaudio notification_sound volume to %1").arg(volume);
    mciSendString(volumeCommand.toStdWString().c_str(), NULL, 0, NULL);
    mciSendString(L"play notification_sound from 0", NULL, 0, NULL);
}

void MainWindow::setupAudioVolmeter()
{
    if (!m_capture || !m_capture->IsInitialized() || !m_showAudioLevelsCheckBox->isChecked())
        return;

    // Create volmeter only once and reuse it
    if (!m_audioVolmeter)
    {
        m_audioVolmeter = obs_volmeter_create(OBS_FADER_LOG);
        obs_volmeter_add_callback(m_audioVolmeter, [](void *data, const float *, const float *peak, const float *)
                                  {
            auto* win = static_cast<MainWindow*>(data);
            if (win && peak) win->m_currentAudioLevel = powf(10.0f, peak[0] / 20.0f); }, this);
    }

    obs_source_t *source = m_capture->GetDesktopAudioSource();
    obs_volmeter_attach_source(m_audioVolmeter, source);
}

void MainWindow::setupMicrophoneVolmeter()
{
    if (!m_capture || !m_capture->IsInitialized() || !m_showMicLevelsCheckBox->isChecked())
        return;

    // Create volmeter only once and reuse it
    if (!m_microphoneVolmeter)
    {
        m_microphoneVolmeter = obs_volmeter_create(OBS_FADER_LOG);
        obs_volmeter_add_callback(m_microphoneVolmeter, [](void *data, const float *, const float *peak, const float *)
                                  {
            auto* win = static_cast<MainWindow*>(data);
            if (win && peak) win->m_currentMicrophoneLevel = powf(10.0f, peak[0] / 20.0f); }, this);
    }

    obs_source_t *source = m_capture->GetMicrophoneSource();
    obs_volmeter_attach_source(m_microphoneVolmeter, source);
}

void MainWindow::updateVisualizers()
{
    // This single slot updates both visualizers if they are visible.
    if (m_audioVisualizer && m_showAudioLevelsCheckBox->isChecked())
    {
        m_audioVisualizer->updateAudioLevel(m_audioEnabledCheckBox->isChecked() ? getAudioLevel() : 0.0f);
    }
    if (m_microphoneVisualizer && m_showMicLevelsCheckBox->isChecked())
    {
        m_microphoneVisualizer->updateAudioLevel(m_micEnabledCheckBox->isChecked() ? getMicrophoneLevel() : 0.0f);
    }
}

float MainWindow::getAudioLevel() { return m_currentAudioLevel; }
float MainWindow::getMicrophoneLevel() { return m_currentMicrophoneLevel; }

void MainWindow::showLogs()
{
    if (!m_logDialog)
    {
        // Create the dialog on first request and parent it to the main window
        m_logDialog = new LogDialog(this);
    }
    m_logDialog->show();
    m_logDialog->raise();
    m_logDialog->activateWindow();
}