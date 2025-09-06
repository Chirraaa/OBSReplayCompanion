#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <deque>
#include <mutex>
#include <QObject>
#include <QTimer>
#include <QString>

// Forward declarations
struct obs_scene;
struct obs_source;
struct obs_output;
struct obs_encoder;
struct calldata;
typedef struct obs_scene obs_scene_t;
typedef struct obs_source obs_source_t;
typedef struct obs_output obs_output_t;
typedef struct obs_encoder obs_encoder_t;
typedef struct calldata calldata_t;
typedef struct obs_data obs_data_t;

enum class EncoderType
{
    NVENC_H264,
    NVENC_HEVC,
    QSV_H264,
    QSV_HEVC,
    AMF_H264,
    AMF_HEVC,
    X264,
    X265
};

enum class PerformanceProfile
{
    Fastest,
    Balanced,
    Quality
};

struct EncoderInfo
{
    EncoderType type;
    std::string id;
    std::string name;
};

struct CaptureSettings
{
    int width = 1920;
    int height = 1080;
    int fps = 60;
    bool captureCursor = true;
};

struct AudioSettings
{
    bool enabled = true;
    int sampleRate = 48000;
    int bitrate = 192;
    int channels = 2;
    float volume = 1.0f;
    std::string deviceId = "default";
    std::string deviceName = "Default";

    bool operator==(const AudioSettings &other) const
    {
        return enabled == other.enabled &&
               bitrate == other.bitrate &&
               deviceId == other.deviceId;
    }
    bool operator!=(const AudioSettings &other) const { return !(*this == other); }
};

struct MicrophoneSettings
{
    bool enabled = false;
    int sampleRate = 48000;
    int channels = 1;
    float volume = 1.0f;
    std::string deviceId = "default";
    std::string deviceName = "Default Microphone";
    bool noiseSuppression = true;
    bool noiseGate = false;
    float noiseGateThreshold = -30.0f;
    float noiseGateCloseThreshold = -32.0f;
    float noiseGateHoldTime = 200.0f;
    float noiseGateReleaseTime = 150.0f;

    bool operator==(const MicrophoneSettings &other) const
    {
        return enabled == other.enabled &&
               deviceId == other.deviceId;
    }
    bool operator!=(const MicrophoneSettings &other) const { return !(*this == other); }
};

struct EncodingSettings
{
    EncoderType encoder = EncoderType::X264;
    int bitrate = 8000;
    bool use_cbr = true;
    int crf = 22; // Also used for CQP
    int keyint_sec = 0;

    // x264/x265 Specific
    std::string x264Preset = "veryfast";
    std::string x264Profile = "high";
    std::string x264Tune = "none";
    std::string x264opts = "";

    // NVENC Specific
    std::string nvencPreset = "p5";
    std::string nvencTuning = "hq";
    std::string nvencMultipass = "qres";
    std::string nvencProfile = "high";
    bool nvencLookahead = false;
    bool nvencPsychoVisualTuning = true;
    int nvencGpu = 0;
    int nvencMaxBFrames = 2;

    // QSV Specific (Intel)
    std::string qsvPreset = "balanced";
    std::string qsvProfile = "high";
    bool qsvLowPower = false;

    // AMF Specific (AMD)
    std::string amfUsage = "quality"; // Corresponds to "Preset" in UI
    std::string amfProfile = "high";
    int amf_bframes = 2;
    std::string amf_opts = "";

    bool operator==(const EncodingSettings &other) const
    {
        // --- CHEAP COMPARISONS FIRST ---
        if (encoder != other.encoder ||
            bitrate != other.bitrate ||
            use_cbr != other.use_cbr ||
            crf != other.crf ||
            keyint_sec != other.keyint_sec ||
            nvencLookahead != other.nvencLookahead ||
            nvencPsychoVisualTuning != other.nvencPsychoVisualTuning ||
            nvencGpu != other.nvencGpu ||
            nvencMaxBFrames != other.nvencMaxBFrames ||
            qsvLowPower != other.qsvLowPower ||
            amf_bframes != other.amf_bframes)
        {
            return false;
        }

        // --- EXPENSIVE STRING COMPARISONS LAST ---
        return x264Preset == other.x264Preset &&
               x264Profile == other.x264Profile &&
               x264Tune == other.x264Tune &&
               x264opts == other.x264opts &&
               nvencPreset == other.nvencPreset &&
               nvencTuning == other.nvencTuning &&
               nvencMultipass == other.nvencMultipass &&
               nvencProfile == other.nvencProfile &&
               qsvPreset == other.qsvPreset &&
               qsvProfile == other.qsvProfile &&
               amfUsage == other.amfUsage &&
               amfProfile == other.amfProfile &&
               amf_opts == other.amf_opts;
    }
    bool operator!=(const EncodingSettings &other) const { return !(*this == other); }
};

class GameCapture : public QObject
{
    Q_OBJECT

public:
    explicit GameCapture(QObject *parent = nullptr);
    ~GameCapture();

    // Core Lifetime
    bool Initialize();
    void Shutdown();

    // Clipping Control
    bool StartClippingMode();
    void StopClippingMode();
    bool IsClippingModeActive() const { return m_clippingModeActive.load(); }
    bool SaveInstantReplay(int durationSeconds, const std::string &filename = "");
    bool SaveClip(int durationSeconds, const std::string &filename = ""); // Legacy
    bool IsRecording() const { return m_isRecording.load(); }

    // Source & Settings Management
    bool SetGameCapture(const std::string &exe);
    void ClearCapture();
    bool SetAudioSettings(const AudioSettings &settings);
    const AudioSettings &GetAudioSettings() const { return m_audioSettings; }
    obs_source_t *GetDesktopAudioSource() const;
    bool SetMicrophoneSettings(const MicrophoneSettings &settings);
    const MicrophoneSettings &GetMicrophoneSettings() const { return m_microphoneSettings; }
    obs_source_t *GetMicrophoneSource() const;
    std::vector<EncoderInfo> GetAvailableEncoders();
    bool SetEncodingSettings(const EncodingSettings &settings);
    const EncodingSettings &GetEncodingSettings() const { return m_encodingSettings; }
    void SetBufferDuration(int seconds) { m_bufferDurationSeconds = seconds; }
    int GetBufferDuration() const { return m_bufferDurationSeconds; }
    bool IsInitialized() const { return m_obsInitialized.load(); }
    const CaptureSettings &GetSettings() const { return m_settings; }
    void SetSettings(const CaptureSettings &settings) { m_settings = settings; }
    void SetOutputFolder(const QString &folder);
    void EnsureDirectoryForGameName(const QString &gameName);

    // Public Callbacks & Updaters
    void handleReplayBufferSaved(const QString &path);
    bool UpdateEncodingSettings(const EncodingSettings &settings);
    bool UpdateAudioSettings(const AudioSettings &settings);
    bool UpdateMicrophoneSettings(const MicrophoneSettings &settings);

public slots:
    void onBufferStopped();
    void onBufferStopTimeout();
    void onSaveClipTimeout();

signals:
    void recordingStarted();
    void recordingFinished(bool success, const QString &filename);
    void clippingModeChanged(bool active);

private:
    // This struct tracks the state of the active buffer to determine
    // if expensive OBS components need to be recreated on the next start.
    struct BufferState
    {
        bool isActive = false;
        EncodingSettings lastEncodingSettings;
        AudioSettings lastAudioSettings;
        MicrophoneSettings lastMicrophoneSettings;
        int lastBufferDuration = 0;

        void reset()
        {
            isActive = false;
            lastBufferDuration = 0;
            // Clear settings to force recreation on the next start
            lastEncodingSettings = {};
            lastAudioSettings = {};
            lastMicrophoneSettings = {};
        }

        bool hasEncodingChanges(const EncodingSettings &current) const
        {
            return lastEncodingSettings != current;
        }

        bool hasAudioChanges(const AudioSettings &current) const
        {
            return lastAudioSettings != current;
        }

        bool hasMicrophoneChanges(const MicrophoneSettings &current) const
        {
            return lastMicrophoneSettings != current;
        }
    };

    // Initialization & Helper Methods
    bool InitializeOBS();
    bool InitializeAudio();
    void DetectAvailableEncoders();
    bool ValidateOBSState();
    std::string GenerateFilename(int duration);
    void StopRecording(); // This is for live recording, not buffer save
    QString GetCurrentGameFolder();
    void UpdateGameNameFromSource();
    void UpdateBufferOutputDirectory();
    void CheckForGameChange();
    void ParseGameFromLog(const QString &logMessage);

    // OBS Object Creation
    obs_data_t *GetEncoderDataSettings(const EncodingSettings &settings, const std::string &encoder_id);
    obs_encoder_t *CreateEncoder(const EncodingSettings &settings);
    obs_encoder_t *CreateAudioEncoder();
    obs_source_t *CreateAudioSource();
    obs_source_t *CreateMicrophoneSource();
    void RecreateAudioSource();
    void RecreateMicrophoneSource();

    // Buffer Management
    bool SetupCircularBuffer();
    void CleanupCircularBuffer();
    void completeBufferCleanup();
    void disconnectReplayBufferSignals();
    bool CreateBufferOutput();
    bool StartBufferOutput();
    bool UpdateBufferVideoEncoder();
    bool UpdateBufferAudioComponents();
    bool UpdateBufferSettings();
    bool FastBufferReset();

    // State & Settings
    std::atomic<bool> m_obsInitialized;
    std::atomic<bool> m_isRecording;
    std::atomic<bool> m_clippingModeActive;
    CaptureSettings m_settings;
    AudioSettings m_audioSettings;
    MicrophoneSettings m_microphoneSettings;
    EncodingSettings m_encodingSettings;
    std::vector<EncoderInfo> m_availableEncoders;
    BufferState m_bufferState;

    // OBS Components - These are now more persistent.
    // Encoders and audio sources are kept alive between clipping sessions
    // and only recreated when their settings fundamentally change.
    obs_scene_t *m_scene;
    obs_source_t *m_currentSource;
    obs_source_t *m_desktopAudioSource;   // Persistent
    obs_source_t *m_microphoneSource;     // Persistent
    obs_output_t *m_currentRecording;     // For future live recording use
    obs_encoder_t *m_currentVideoEncoder; // For future live recording use
    obs_encoder_t *m_currentAudioEncoder; // For future live recording use
    obs_output_t *m_bufferOutput;         // Recreated each time clipping is enabled
    obs_encoder_t *m_bufferVideoEncoder;  // Persistent
    obs_encoder_t *m_bufferAudioEncoder;  // Persistent

    // Timers & Async Management
    QTimer *m_bufferStopTimer;
    QTimer *m_saveClipTimeoutTimer;
    std::function<void()> m_pendingBufferCallback;
    QElapsedTimer m_saveCooldownTimer;
    const qint64 SAVE_COOLDOWN_MS = 2000;

    // File & Path Management
    QString m_currentRecordingFile;
    QString m_outputFolder;
    QString m_tempReplayPath;
    QString m_currentGameName;
    QString m_cachedGameFolder;
    int m_bufferDurationSeconds;
};