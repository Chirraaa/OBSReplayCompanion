#include "GameCapture.h"
#include <obs.hpp>
#include <obs-module.h>
#include <obs-encoder.h>
#include <callback/signal.h>
#include <filesystem>
#include <windows.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <QThread>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QFileInfo>
#include <QFile>
#include <tuple>
#include <unordered_map>

// --- Static Helper Function ---
// Generates a sanitized, game-specific folder path.
static QString GetPathForGameName(const QString &outputFolder, const QString &gameName)
{
    // Compile the regex once and reuse it for efficiency.
    static const QRegularExpression invalidCharsRegex(QStringLiteral("[<>:\"/\\\\|?*]"));

    QString baseFolder = outputFolder;
    if (gameName.isEmpty() || gameName == "Unknown")
    {
        return baseFolder + "/General";
    }

    QString cleanGameName = gameName;
    cleanGameName.replace(invalidCharsRegex, "_");
    cleanGameName = cleanGameName.trimmed();

    return cleanGameName.isEmpty() ? baseFolder + "/General" : baseFolder + "/" + cleanGameName;
}

// Static callback functions for OBS signals
static void onBufferStopSignal(void *data, calldata_t *cd);

static void replay_buffer_saved_callback(void *data, calldata_t *cd)
{
    if (!data || !cd)
    {
        qDebug() << "Invalid callback data";
        return;
    }

    GameCapture *capture = static_cast<GameCapture *>(data);

    // OBS can use different keys for the path parameter
    const char *path = calldata_string(cd, "path");
    if (!path || strlen(path) == 0)
        path = calldata_string(cd, "file");
    if (!path || strlen(path) == 0)
        path = calldata_string(cd, "filename");
    if (!path || strlen(path) == 0)
        path = calldata_string(cd, "output_path");

    qDebug() << "Replay buffer saved callback - path:" << (path ? path : "null");

    QString pathStr = path ? QString::fromUtf8(path) : QString();

    // Use queued connection to avoid threading issues
    QMetaObject::invokeMethod(capture, [capture, pathStr]()
                              { capture->handleReplayBufferSaved(pathStr); }, Qt::QueuedConnection);
}

static void onBufferStopSignal(void *data, calldata_t *cd)
{
    Q_UNUSED(cd)
    if (!data)
        return;

    GameCapture *capture = static_cast<GameCapture *>(data);
    QMetaObject::invokeMethod(capture, [capture]()
                              { capture->onBufferStopped(); }, Qt::QueuedConnection);
}

GameCapture::GameCapture(QObject *parent)
    : QObject(parent),
      m_obsInitialized(false),
      m_isRecording(false),
      m_clippingModeActive(false),
      m_scene(nullptr),
      m_currentSource(nullptr),
      m_desktopAudioSource(nullptr),
      m_microphoneSource(nullptr),
      m_currentRecording(nullptr),
      m_currentVideoEncoder(nullptr),
      m_currentAudioEncoder(nullptr),
      m_bufferOutput(nullptr),
      m_bufferVideoEncoder(nullptr),
      m_bufferAudioEncoder(nullptr),
      m_bufferDurationSeconds(60),
      m_bufferStopTimer(new QTimer(this)),
      m_saveClipTimeoutTimer(new QTimer(this))
{
    m_bufferState.reset();
    m_bufferStopTimer->setSingleShot(true);
    m_bufferStopTimer->setInterval(3000); // 3 second timeout
    connect(m_bufferStopTimer, &QTimer::timeout, this, &GameCapture::onBufferStopTimeout);

    m_saveClipTimeoutTimer->setSingleShot(true);
    m_saveClipTimeoutTimer->setInterval(30000); // 30 second timeout
    connect(m_saveClipTimeoutTimer, &QTimer::timeout, this, &GameCapture::onSaveClipTimeout);

    // Start the cooldown timer
    m_saveCooldownTimer.start();
}

GameCapture::~GameCapture()
{
    Shutdown();
}

bool GameCapture::Initialize()
{
    return InitializeOBS();
}

void GameCapture::Shutdown()
{
    StopClippingMode();
    ClearCapture();

    // Explicitly release all persistent OBS components that are not tied
    // to the replay buffer output's lifecycle.
    if (m_bufferVideoEncoder)
    {
        obs_encoder_release(m_bufferVideoEncoder);
        m_bufferVideoEncoder = nullptr;
    }
    if (m_bufferAudioEncoder)
    {
        obs_encoder_release(m_bufferAudioEncoder);
        m_bufferAudioEncoder = nullptr;
    }
    if (m_desktopAudioSource)
    {
        obs_source_release(m_desktopAudioSource);
        m_desktopAudioSource = nullptr;
    }
    if (m_microphoneSource)
    {
        obs_source_release(m_microphoneSource);
        m_microphoneSource = nullptr;
    }
    if (m_scene)
    {
        obs_scene_release(m_scene);
        m_scene = nullptr;
    }
    if (m_obsInitialized)
    {
        obs_shutdown();
        m_obsInitialized = false;
    }
}

bool GameCapture::StartClippingMode()
{
    if (!m_obsInitialized.load() || m_clippingModeActive.load())
    {
        qDebug() << "Cannot start clipping mode - OBS not initialized or already active";
        return false;
    }
    if (!SetupCircularBuffer())
    {
        qDebug() << "Failed to setup circular buffer";
        // Ensure partial setup is cleaned up
        CleanupCircularBuffer();
        return false;
    }

    m_clippingModeActive = true;
    emit clippingModeChanged(true);
    qDebug() << "Clipping mode started successfully";
    return true;
}

void GameCapture::StopClippingMode()
{
    if (!m_clippingModeActive.load())
    {
        return;
    }

    qDebug() << "Stopping clipping mode";

    if (m_isRecording.load())
    {
        m_isRecording = false;
        disconnectReplayBufferSignals();
    }

    CleanupCircularBuffer();
    m_clippingModeActive = false;
    emit clippingModeChanged(false);
    qDebug() << "Clipping mode stopped";
}

void GameCapture::onSaveClipTimeout()
{
    if (m_isRecording.load())
    {
        qDebug() << "Save operation timed out";
        m_isRecording = false;
        disconnectReplayBufferSignals();
        emit recordingFinished(false, m_currentRecordingFile);
    }
}

bool GameCapture::SaveInstantReplay(int durationSeconds, const std::string &filename)
{
    qDebug() << "SaveInstantReplay called with duration:" << durationSeconds << "filename:" << filename.c_str();

    if (m_saveCooldownTimer.elapsed() < SAVE_COOLDOWN_MS)
    {
        qDebug() << "Cannot save replay: Save button is on cooldown.";
        return false;
    }

    if (!m_clippingModeActive.load() || m_isRecording.load() || !m_bufferOutput || !obs_output_active(m_bufferOutput))
    {
        qDebug() << "Cannot save replay: clipping not active, already saving, or buffer is inactive.";
        return false;
    }

    // Reset the cooldown timer
    m_saveCooldownTimer.restart();

    m_isRecording = true;
    emit recordingStarted();
    m_currentRecordingFile = QString::fromStdString(filename);

    disconnectReplayBufferSignals();

    signal_handler_t *handler = obs_output_get_signal_handler(m_bufferOutput);
    if (!handler)
    {
        qDebug() << "No signal handler found";
        m_isRecording = false;
        return false;
    }

    signal_handler_connect(handler, "saved", replay_buffer_saved_callback, this);

    qDebug() << "Triggering replay buffer save using procedure call";
    proc_handler_t *proc_handler = obs_output_get_proc_handler(m_bufferOutput);
    if (!proc_handler)
    {
        qDebug() << "No procedure handler found";
        m_isRecording = false;
        disconnectReplayBufferSignals();
        return false;
    }

    struct calldata params = {0};
    calldata_init(&params);
    bool success = proc_handler_call(proc_handler, "save", &params);
    calldata_free(&params);

    if (!success)
    {
        qDebug() << "Failed to call save procedure";
        m_isRecording = false;
        disconnectReplayBufferSignals();
        return false;
    }

    m_saveClipTimeoutTimer->start();

    qDebug() << "Save operation initiated successfully";
    return true;
}

bool GameCapture::SaveClip(int durationSeconds, const std::string &filename)
{
    return m_clippingModeActive.load() ? SaveInstantReplay(durationSeconds, filename) : false;
}

bool GameCapture::SetGameCapture(const std::string &exe)
{
    std::filesystem::path exePath(exe);
    QString newGameName = QString::fromStdString(exePath.stem().string());

    if (newGameName == m_currentGameName)
    {
        qDebug() << "Game capture source is already set for:" << m_currentGameName;
        return true;
    }

    ClearCapture();

    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "executable", exe.c_str());
    obs_data_set_bool(settings, "capture_cursor", m_settings.captureCursor);
    obs_data_set_bool(settings, "capture_overlays", true);
    obs_data_set_bool(settings, "anti_cheat_hook", true);
    obs_data_set_string(settings, "hook_rate", "normal");
    obs_data_set_string(settings, "mode", "any_fullscreen");

    m_currentSource = obs_source_create("game_capture", "Game Capture", settings, nullptr);
    obs_data_release(settings);

    if (m_currentSource)
    {
        obs_sceneitem_t *scene_item = obs_scene_add(m_scene, m_currentSource);
        if (scene_item)
        {
            obs_sceneitem_set_bounds_type(scene_item, OBS_BOUNDS_STRETCH);
            struct vec2 bounds;
            bounds.x = static_cast<float>(m_settings.width);
            bounds.y = static_cast<float>(m_settings.height);
            obs_sceneitem_set_bounds(scene_item, &bounds);
        }

        m_currentGameName = newGameName;
        m_cachedGameFolder.clear(); // Invalidate cache
        qDebug() << "Game capture set for:" << m_currentGameName;
        if (m_clippingModeActive.load())
        {
            UpdateBufferOutputDirectory();
        }
        return true;
    }
    return false;
}

void GameCapture::ClearCapture()
{
    if (m_currentSource)
    {
        obs_sceneitem_t *item = obs_scene_find_source(m_scene, "Game Capture");
        if (item)
        {
            obs_sceneitem_remove(item);
        }
        obs_source_release(m_currentSource);
        m_currentSource = nullptr;
    }
}

bool GameCapture::SetAudioSettings(const AudioSettings &settings)
{
    return UpdateAudioSettings(settings);
}

obs_source_t *GameCapture::GetDesktopAudioSource() const
{
    return m_desktopAudioSource;
}

bool GameCapture::SetMicrophoneSettings(const MicrophoneSettings &settings)
{
    return UpdateMicrophoneSettings(settings);
}

obs_source_t *GameCapture::GetMicrophoneSource() const
{
    return m_microphoneSource;
}

std::vector<EncoderInfo> GameCapture::GetAvailableEncoders()
{
    return m_availableEncoders;
}

bool GameCapture::SetEncodingSettings(const EncodingSettings &settings)
{
    return UpdateEncodingSettings(settings);
}

void GameCapture::SetOutputFolder(const QString &folder)
{
    m_outputFolder = folder;
    // Proactively create base folder and the "General" sub-folder
    EnsureDirectoryForGameName("Unknown"); // "Unknown" maps to "General"
}

void GameCapture::EnsureDirectoryForGameName(const QString &gameName)
{
    QString path = GetPathForGameName(m_outputFolder, gameName);
    QDir dir(path);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }
}

void GameCapture::handleReplayBufferSaved(const QString &path)
{
    m_saveClipTimeoutTimer->stop();
    qDebug() << "handleReplayBufferSaved called with path:" << path;
    m_isRecording = false;
    disconnectReplayBufferSignals();

    QString savedPath = path;
    if (!savedPath.isEmpty())
    {
        QString expectedGameFolder = GetCurrentGameFolder();
        QFileInfo fileInfo(savedPath);

        if (fileInfo.absolutePath() != expectedGameFolder)
        {
            qDebug() << "File saved to unexpected location, moving...";
            QString correctPath = expectedGameFolder + "/" + fileInfo.fileName();
            if (QFile::rename(savedPath, correctPath))
            {
                savedPath = correctPath;
            }
        }
    }
    else
    {
        // Fallback: search for the most recent file in the game folder
        QDir gameDir(GetCurrentGameFolder());
        QFileInfoList files = gameDir.entryInfoList({"*.mp4", "*.mkv"}, QDir::Files, QDir::Time);
        if (!files.isEmpty())
        {
            savedPath = files.first().absoluteFilePath();
        }
    }

    if (!savedPath.isEmpty() && QFile::exists(savedPath))
    {
        emit recordingFinished(true, savedPath);
    }
    else
    {
        emit recordingFinished(false, "");
    }

    QTimer::singleShot(200, this, [this]()
                       {
        if (m_clippingModeActive.load() && !FastBufferReset()) {
            qDebug() << "Fast reset failed, falling back to full restart";
            StopClippingMode();
            QTimer::singleShot(500, this, [this]() { StartClippingMode(); });
        } });
}

// These methods now just update the internal settings objects.
// The changes are detected and applied efficiently when StartClippingMode is called.
// Live-updatable settings (like volume) are still applied immediately.
bool GameCapture::UpdateEncodingSettings(const EncodingSettings &settings)
{
    if (m_encodingSettings == settings)
        return true;

    m_encodingSettings = settings;
    return true;
}

bool GameCapture::UpdateAudioSettings(const AudioSettings &settings)
{
    // Volume can be updated live without a restart, so we apply it directly.
    bool volumeChanged = (settings.volume != m_audioSettings.volume);
    if (m_desktopAudioSource && volumeChanged)
    {
        obs_source_set_volume(m_desktopAudioSource, settings.volume);
    }

    m_audioSettings = settings;
    return true;
}

bool GameCapture::UpdateMicrophoneSettings(const MicrophoneSettings &settings)
{
    // Volume and filters can be updated live.
    bool volumeChanged = (settings.volume != m_microphoneSettings.volume);
    bool noiseSuppressionChanged = (settings.noiseSuppression != m_microphoneSettings.noiseSuppression);

    if (m_microphoneSource)
    {
        if (volumeChanged)
        {
            obs_source_set_volume(m_microphoneSource, settings.volume);
        }
        if (noiseSuppressionChanged)
        {
            obs_source_t *filter = obs_source_get_filter_by_name(m_microphoneSource, "Noise Suppression");
            if (filter)
            {
                obs_source_set_enabled(filter, settings.noiseSuppression);
                obs_source_release(filter);
            }
        }
    }

    m_microphoneSettings = settings;
    return true;
}

bool GameCapture::UpdateBufferSettings()
{
    if (!m_bufferOutput)
        return false;

    if (m_bufferDurationSeconds != m_bufferState.lastBufferDuration)
    {
        obs_data_t *settings = obs_data_create();
        obs_data_set_int(settings, "max_time_sec", m_bufferDurationSeconds);
        // Directory is updated separately when the game changes
        obs_output_update(m_bufferOutput, settings);
        obs_data_release(settings);
        m_bufferState.lastBufferDuration = m_bufferDurationSeconds;
    }
    return true;
}

void GameCapture::onBufferStopped()
{
    qDebug() << "Buffer stop signal received";
    m_bufferStopTimer->stop();

    if (m_bufferOutput)
    {
        signal_handler_t *handler = obs_output_get_signal_handler(m_bufferOutput);
        if (handler)
        {
            signal_handler_disconnect(handler, "stop", onBufferStopSignal, this);
        }
    }
    if (m_pendingBufferCallback)
    {
        m_pendingBufferCallback();
    }
}

void GameCapture::onBufferStopTimeout()
{
    qDebug() << "Buffer stop timeout - forcing completion";
    if (m_bufferOutput)
    {
        signal_handler_t *handler = obs_output_get_signal_handler(m_bufferOutput);
        if (handler)
        {
            signal_handler_disconnect(handler, "stop", onBufferStopSignal, this);
        }
        if (obs_output_active(m_bufferOutput))
        {
            obs_output_force_stop(m_bufferOutput);
        }
    }
    if (m_pendingBufferCallback)
    {
        m_pendingBufferCallback();
    }
}

void GameCapture::StopRecording()
{
    // This method is now only for live recording, not replay buffer saving.
    if (m_isRecording.load() && m_currentRecording)
    {
        obs_output_stop(m_currentRecording);
    }
}

bool GameCapture::InitializeOBS()
{
    qDebug() << "Initializing OBS";
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    std::filesystem::path base_path = std::filesystem::path(exe_path).parent_path();
    std::vector<std::filesystem::path> possible_data_paths = {
        base_path / "data", base_path / ".." / "data",
        "C:/Program Files/obs-studio/data"};

    std::filesystem::path data_path;
    bool found_data = false;
    for (const auto &path : possible_data_paths)
    {
        if (std::filesystem::exists(path / "libobs"))
        {
            data_path = path;
            found_data = true;
            break;
        }
    }
    if (!found_data)
        return false;
    if (!obs_startup("en-US", data_path.string().c_str(), nullptr))
        return false;

    qDebug() << "GameCapture::InitializeOBS - Applying video settings:"
             << m_settings.width << "x" << m_settings.height
             << "@" << m_settings.fps << "FPS";

    struct obs_video_info ovi = {};
    ovi.graphics_module = "libobs-d3d11.dll";
    ovi.fps_num = m_settings.fps;
    ovi.fps_den = 1;
    ovi.output_format = VIDEO_FORMAT_NV12;
    ovi.base_width = m_settings.width;
    ovi.base_height = m_settings.height;
    ovi.output_width = m_settings.width;
    ovi.output_height = m_settings.height;
    ovi.adapter = 0;
    ovi.gpu_conversion = true;
    ovi.colorspace = VIDEO_CS_709;
    ovi.range = VIDEO_RANGE_PARTIAL;

    int video_result = obs_reset_video(&ovi);
    if (video_result != OBS_VIDEO_SUCCESS)
    {
        qDebug() << "D3D11 failed, trying OpenGL";
        ovi.graphics_module = "libobs-opengl.dll";
        video_result = obs_reset_video(&ovi);
        if (video_result != OBS_VIDEO_SUCCESS)
        {
            obs_shutdown();
            return false;
        }
    }

    obs_load_all_modules();
    obs_post_load_modules();

    if (!InitializeAudio())
    {
        obs_shutdown();
        return false;
    }

    m_scene = obs_scene_create("capture_scene");
    if (!m_scene)
    {
        obs_shutdown();
        return false;
    }

    obs_set_output_source(0, obs_scene_get_source(m_scene));
    DetectAvailableEncoders();
    m_desktopAudioSource = CreateAudioSource();
    m_obsInitialized = true;
    qDebug() << "OBS initialized successfully";
    return true;
}

bool GameCapture::InitializeAudio()
{
    struct obs_audio_info oai = {};
    oai.samples_per_sec = 48000;
    oai.speakers = (m_audioSettings.channels == 2) ? SPEAKERS_STEREO : SPEAKERS_MONO;
    if (!obs_reset_audio(&oai))
    {
        oai.samples_per_sec = 44100;
        if (!obs_reset_audio(&oai))
        {
            return false;
        }
    }
    return true;
}

void GameCapture::DetectAvailableEncoders()
{
    m_availableEncoders.clear();
    qDebug() << "Detecting available encoders...";

    // This map provides user-friendly names and our internal EncoderType for each OBS encoder ID.
    // The IDs are based on modern OBS Studio versions.
    const std::unordered_map<std::string, std::pair<EncoderType, std::string>> encoder_map = {
        {"ffmpeg_nvenc", {EncoderType::NVENC_H264, "NVIDIA NVENC H.264"}},
        {"ffmpeg_hevc_nvenc", {EncoderType::NVENC_HEVC, "NVIDIA NVENC HEVC"}},
        {"obs_qsv11", {EncoderType::QSV_H264, "Intel Quick Sync (QSV) H.264"}},
        {"h264_texture_amf", {EncoderType::AMF_H264, "AMD AMF H.264 (AVC)"}},
        {"h265_texture_amf", {EncoderType::AMF_HEVC, "AMD AMF HEVC"}},
        {"obs_x264", {EncoderType::X264, "Software (x264)"}},
        {"obs_x265", {EncoderType::X265, "Software (x265)"}}};

    const char *id;
    int i = 0;
    // This function enumerates all registered video encoder types in the loaded OBS modules.
    while (obs_enum_encoder_types(i++, &id))
    {
        std::string current_id_str(id);
        auto it = encoder_map.find(current_id_str);

        if (it != encoder_map.end())
        {
            // We found a known encoder that is available on the system.
            qDebug() << "Detected available encoder:" << it->second.second.c_str() << "(" << id << ")";
            m_availableEncoders.push_back({it->second.first,    // type (EncoderType enum)
                                           it->first,           // id (std::string)
                                           it->second.second}); // name (std::string)
        }
    }
    qDebug() << "Encoder detection finished. Found" << m_availableEncoders.size() << "encoders.";
}

obs_data_t *GameCapture::GetEncoderDataSettings(const EncodingSettings &settings, const std::string &encoder_id)
{
    obs_data_t *encoder_settings = obs_data_create();

    bool is_nvenc = (encoder_id.find("nvenc") != std::string::npos);
    bool is_qsv = (encoder_id.find("qsv") != std::string::npos);
    bool is_amf = (encoder_id.find("amf") != std::string::npos);
    bool is_x264 = (encoder_id.find("x264") != std::string::npos || encoder_id.find("x265") != std::string::npos);

    obs_data_set_int(encoder_settings, "keyint_sec", settings.keyint_sec);

    if (is_nvenc)
    {
        obs_data_set_string(encoder_settings, "preset2", settings.nvencPreset.c_str());
        obs_data_set_string(encoder_settings, "tune", settings.nvencTuning.c_str());
        obs_data_set_string(encoder_settings, "multipass", settings.nvencMultipass.c_str());
        obs_data_set_string(encoder_settings, "profile", settings.nvencProfile.c_str());
        obs_data_set_bool(encoder_settings, "lookahead", settings.nvencLookahead);
        obs_data_set_bool(encoder_settings, "psycho_aq", settings.nvencPsychoVisualTuning);
        obs_data_set_int(encoder_settings, "gpu", settings.nvencGpu);
        obs_data_set_int(encoder_settings, "bf", settings.nvencMaxBFrames);
    }
    else if (is_qsv)
    {
        obs_data_set_string(encoder_settings, "preset", settings.qsvPreset.c_str());
        obs_data_set_string(encoder_settings, "profile", settings.qsvProfile.c_str());
        obs_data_set_bool(encoder_settings, "low_power", settings.qsvLowPower);
    }
    else if (is_amf)
    {
        obs_data_set_string(encoder_settings, "usage", settings.amfUsage.c_str());
        obs_data_set_string(encoder_settings, "profile", settings.amfProfile.c_str());
        obs_data_set_int(encoder_settings, "bf", settings.amf_bframes);
        if (!settings.amf_opts.empty())
        {
            obs_data_set_string(encoder_settings, "amf_opts", settings.amf_opts.c_str());
        }
    }
    else if (is_x264)
    {
        obs_data_set_string(encoder_settings, "preset", settings.x264Preset.c_str());
        if (settings.x264Tune != "none")
        {
            obs_data_set_string(encoder_settings, "tune", settings.x264Tune.c_str());
        }
        obs_data_set_string(encoder_settings, "profile", settings.x264Profile.c_str());
        if (!settings.x264opts.empty())
        {
            obs_data_set_string(encoder_settings, "x264opts", settings.x264opts.c_str());
        }
    }

    if (settings.use_cbr)
    {
        obs_data_set_string(encoder_settings, "rate_control", "CBR");
        obs_data_set_int(encoder_settings, "bitrate", settings.bitrate);
    }
    else
    {
        if (is_nvenc || is_qsv || is_amf)
        {
            obs_data_set_string(encoder_settings, "rate_control", "CQP");
            obs_data_set_int(encoder_settings, "cqp", settings.crf);
        }
        else
        {
            obs_data_set_string(encoder_settings, "rate_control", "CRF");
            obs_data_set_int(encoder_settings, "crf", settings.crf);
        }
    }
    return encoder_settings;
}

obs_encoder_t *GameCapture::CreateEncoder(const EncodingSettings &settings)
{
    std::string encoder_id;
    for (const auto &encoder : m_availableEncoders)
    {
        if (encoder.type == settings.encoder)
        {
            encoder_id = encoder.id;
            break;
        }
    }
    if (encoder_id.empty())
        encoder_id = "obs_x264";

    obs_data_t *encoder_settings = GetEncoderDataSettings(settings, encoder_id);
    obs_encoder_t *encoder = obs_video_encoder_create(encoder_id.c_str(), "video_encoder", encoder_settings, nullptr);

    if (!encoder)
    {
        qDebug() << "Failed to create specified encoder, falling back to x264";
        encoder_id = "obs_x264";
        obs_data_release(encoder_settings);
        encoder_settings = GetEncoderDataSettings(settings, encoder_id);
        encoder = obs_video_encoder_create(encoder_id.c_str(), "video_encoder", encoder_settings, nullptr);
    }
    obs_data_release(encoder_settings);
    return encoder;
}

obs_encoder_t *GameCapture::CreateAudioEncoder()
{
    obs_data_t *audio_settings = obs_data_create();
    obs_data_set_int(audio_settings, "bitrate", m_audioSettings.bitrate);
    obs_data_set_string(audio_settings, "rate_control", "CBR");
    obs_data_set_int(audio_settings, "samplerate", 48000);
    obs_encoder_t *encoder = obs_audio_encoder_create("ffmpeg_aac", "audio_encoder", audio_settings, 0, nullptr);
    obs_data_release(audio_settings);
    return encoder;
}

obs_source_t *GameCapture::CreateAudioSource()
{
    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "device_id", m_audioSettings.deviceId.empty() ? "default" : m_audioSettings.deviceId.c_str());
    obs_source_t *source = obs_source_create("wasapi_output_capture", "Desktop Audio", settings, nullptr);
    obs_data_release(settings);

    if (source)
    {
        obs_source_set_volume(source, m_audioSettings.volume);
        obs_source_set_enabled(source, m_audioSettings.enabled);
        obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);
    }
    return source;
}

obs_source_t *GameCapture::CreateMicrophoneSource()
{
    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "device_id", m_microphoneSettings.deviceId.empty() ? "default" : m_microphoneSettings.deviceId.c_str());
    obs_data_set_bool(settings, "use_device_timing", true);
    obs_source_t *source = obs_source_create("wasapi_input_capture", "Microphone", settings, nullptr);
    obs_data_release(settings);

    if (source)
    {
        obs_source_set_volume(source, m_microphoneSettings.volume);
        obs_source_set_enabled(source, m_microphoneSettings.enabled);
        obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_NONE);

        if (m_microphoneSettings.noiseSuppression)
        {
            obs_source_t *filter = obs_source_create("noise_suppress_filter", "Noise Suppression", nullptr, nullptr);
            if (filter)
            {
                obs_source_filter_add(source, filter);
                obs_source_release(filter);
            }
        }
    }
    return source;
}

void GameCapture::RecreateAudioSource()
{
    bool wasActive = m_clippingModeActive.load();
    if (wasActive)
        StopClippingMode();
    if (m_desktopAudioSource)
    {
        obs_source_release(m_desktopAudioSource);
        m_desktopAudioSource = nullptr;
    }
    m_desktopAudioSource = CreateAudioSource();
    if (wasActive)
        QTimer::singleShot(500, [this]()
                           { StartClippingMode(); });
}

void GameCapture::RecreateMicrophoneSource()
{
    bool wasActive = m_clippingModeActive.load();
    if (wasActive)
        StopClippingMode();
    if (m_microphoneSource)
    {
        obs_source_release(m_microphoneSource);
        m_microphoneSource = nullptr;
    }
    if (m_microphoneSettings.enabled)
    {
        m_microphoneSource = CreateMicrophoneSource();
    }
    if (wasActive)
        QTimer::singleShot(500, [this]()
                           { StartClippingMode(); });
}

std::string GameCapture::GenerateFilename(int duration)
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "Clip_" << std::put_time(std::localtime(&time_t), "%Y-%m-%d_%H-%M-%S");
    ss << "_" << duration << "s.mp4";
    return ss.str();
}

void GameCapture::CheckForGameChange()
{
    if (m_clippingModeActive.load())
    {
        UpdateGameNameFromSource();
    }
}

void GameCapture::UpdateGameNameFromSource()
{
    if (!m_currentSource)
        return;

    obs_data_t *settings = obs_source_get_settings(m_currentSource);
    if (!settings)
        return;

    const char *executable = obs_data_get_string(settings, "executable");
    if (executable && strlen(executable) > 0)
    {
        std::filesystem::path exePath(executable);
        QString newGameName = QString::fromStdString(exePath.stem().string());
        if (newGameName != m_currentGameName)
        {
            m_currentGameName = newGameName;
            m_cachedGameFolder.clear(); // Invalidate cache
            if (m_clippingModeActive.load())
            {
                UpdateBufferOutputDirectory();
            }
        }
    }
    obs_data_release(settings);
}

void GameCapture::UpdateBufferOutputDirectory()
{
    if (!m_bufferOutput)
        return;

    QString newGameFolder = GetCurrentGameFolder();

    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "directory", newGameFolder.toUtf8().constData());
    obs_output_update(m_bufferOutput, settings);
    obs_data_release(settings);
}

QString GameCapture::GetCurrentGameFolder()
{
    // Return the cached path if the game name hasn't changed
    if (!m_cachedGameFolder.isEmpty())
    {
        return m_cachedGameFolder;
    }

    m_cachedGameFolder = GetPathForGameName(m_outputFolder, m_currentGameName);
    return m_cachedGameFolder;
}

bool GameCapture::ValidateOBSState()
{
    if (!m_obsInitialized.load())
        return false;
    video_t *video = obs_get_video();
    audio_t *audio = obs_get_audio();
    if (!video || !audio)
        return false;

    const struct video_output_info *voi = video_output_get_info(video);
    if (!voi || voi->width == 0 || voi->height == 0)
        return false;

    const struct audio_output_info *aoi = audio_output_get_info(audio);
    return aoi != nullptr;
}

bool GameCapture::SetupCircularBuffer()
{
    if (!ValidateOBSState())
        return false;

    if (m_bufferState.isActive)
    {
        qDebug() << "Buffer is already active, applying settings updates if any.";
        return UpdateBufferSettings(); // Update duration if changed
    }

    qDebug() << "Setting up circular buffer...";

    // This sequence now ensures components are created/updated only when needed
    // before the buffer output itself is created and started.
    if (!UpdateBufferVideoEncoder() || !UpdateBufferAudioComponents() || !CreateBufferOutput() || !StartBufferOutput())
    {
        qDebug() << "A step in circular buffer setup failed. Cleaning up.";
        CleanupCircularBuffer();
        return false;
    }

    // After a successful start, update the state trackers so we know what
    // settings the current components were created with.
    m_bufferState.isActive = true;
    m_bufferState.lastEncodingSettings = m_encodingSettings;
    m_bufferState.lastAudioSettings = m_audioSettings;
    m_bufferState.lastMicrophoneSettings = m_microphoneSettings;
    m_bufferState.lastBufferDuration = m_bufferDurationSeconds;

    qDebug() << "Circular buffer setup successful and is now active.";
    return true;
}

void GameCapture::CleanupCircularBuffer()
{
    if (!m_bufferOutput && !m_bufferState.isActive)
        return;

    qDebug() << "Cleaning up circular buffer (stopping and releasing output).";
    disconnectReplayBufferSignals();

    // The cleanup process now only targets the replay_buffer output.
    // Encoders and audio sources remain alive for the next session.
    if (m_bufferOutput && obs_output_active(m_bufferOutput))
    {
        // Stop the buffer asynchronously to prevent blocking the main thread.
        m_pendingBufferCallback = [this]()
        {
            completeBufferCleanup();
            m_pendingBufferCallback = nullptr;
        };
        signal_handler_t *handler = obs_output_get_signal_handler(m_bufferOutput);
        if (handler)
        {
            signal_handler_connect(handler, "stop", onBufferStopSignal, this);
        }
        m_bufferStopTimer->start();
        obs_output_stop(m_bufferOutput);
    }
    else
    {
        completeBufferCleanup();
    }
}

void GameCapture::completeBufferCleanup()
{
    if (m_bufferOutput)
    {
        obs_output_release(m_bufferOutput);
        m_bufferOutput = nullptr;
    }
    m_bufferState.isActive = false;
}

void GameCapture::disconnectReplayBufferSignals()
{
    if (m_bufferOutput)
    {
        signal_handler_t *handler = obs_output_get_signal_handler(m_bufferOutput);
        if (handler)
        {
            signal_handler_disconnect(handler, "saved", replay_buffer_saved_callback, this);
        }
    }
}

bool GameCapture::CreateBufferOutput()
{
    if (m_bufferOutput) // Already exists, no need to create.
        return true;

    if (!m_bufferVideoEncoder || !m_bufferAudioEncoder)
    {
        qWarning() << "Cannot create buffer output without valid encoders.";
        return false;
    }

    obs_data_t *output_settings = obs_data_create();
    obs_data_set_int(output_settings, "max_time_sec", m_bufferDurationSeconds);
    QString outputPath = GetCurrentGameFolder();
    obs_data_set_string(output_settings, "directory", outputPath.toUtf8().constData());
    obs_data_set_string(output_settings, "format", "Replay_%CCYY%MM%DD_%hh%mm%ss");
    obs_data_set_string(output_settings, "extension", "mp4");
    m_bufferOutput = obs_output_create("replay_buffer", "buffer_output", output_settings, nullptr);
    obs_data_release(output_settings);

    if (!m_bufferOutput)
    {
        qWarning() << "Failed to create replay_buffer output object.";
        return false;
    }

    // Attach the persistent encoders to the new output object.
    obs_output_set_video_encoder(m_bufferOutput, m_bufferVideoEncoder);
    obs_output_set_audio_encoder(m_bufferOutput, m_bufferAudioEncoder, 0);

    return true;
}

bool GameCapture::StartBufferOutput()
{
    if (!m_bufferOutput || !m_bufferVideoEncoder)
        return false;
    if (obs_output_active(m_bufferOutput))
        return true;

    if (!obs_output_start(m_bufferOutput))
    {
        qDebug() << "Failed to start buffer output:" << obs_output_get_last_error(m_bufferOutput);
        return false;
    }

    // Reapply encoder settings after start; some drivers override them.
    QTimer::singleShot(1000, [this]()
                       {
        if (m_bufferOutput && obs_output_active(m_bufferOutput) && m_bufferVideoEncoder) {
            std::string encoder_id = obs_encoder_get_id(m_bufferVideoEncoder);
            obs_data_t *settings = GetEncoderDataSettings(m_encodingSettings, encoder_id);
            obs_encoder_update(m_bufferVideoEncoder, settings);
            obs_data_release(settings);
        } });
    return true;
}

bool GameCapture::UpdateBufferVideoEncoder()
{
    // Determine if the encoder needs to be recreated. This happens if it doesn't
    // exist yet, or if the encoding settings have changed since it was created.
    bool needsRecreation = !m_bufferVideoEncoder || m_bufferState.hasEncodingChanges(m_encodingSettings);
    if (!needsRecreation)
    {
        qDebug() << "Video encoder is up-to-date. No recreation needed.";
        return true;
    }

    qDebug() << "Recreating video encoder due to settings change or first-time setup.";
    if (m_bufferVideoEncoder)
    {
        obs_encoder_release(m_bufferVideoEncoder);
        m_bufferVideoEncoder = nullptr;
    }

    m_bufferVideoEncoder = CreateEncoder(m_encodingSettings);
    if (!m_bufferVideoEncoder)
    {
        qWarning() << "Failed to create video encoder!";
        return false;
    }

    obs_encoder_set_video(m_bufferVideoEncoder, obs_get_video());
    return true;
}

bool GameCapture::UpdateBufferAudioComponents()
{
    // Determine what needs to be changed based on specific properties
    bool desktopDeviceChanged = !m_desktopAudioSource || (m_bufferState.lastAudioSettings.deviceId != m_audioSettings.deviceId);
    bool micDeviceChanged = !m_microphoneSource || (m_bufferState.lastMicrophoneSettings.deviceId != m_microphoneSettings.deviceId);
    bool encoderSettingsChanged = !m_bufferAudioEncoder || (m_bufferState.lastAudioSettings.bitrate != m_audioSettings.bitrate);

    // --- Desktop Audio Source ---
    if (desktopDeviceChanged)
    {
        qDebug() << "Recreating desktop audio source due to device change or first-time setup.";
        if (m_desktopAudioSource)
            obs_source_release(m_desktopAudioSource);
        m_desktopAudioSource = CreateAudioSource();
    }

    // Always apply current volume and enabled state to the source.
    if (m_desktopAudioSource)
    {
        obs_source_set_volume(m_desktopAudioSource, m_audioSettings.volume);
        obs_source_set_enabled(m_desktopAudioSource, m_audioSettings.enabled);
        obs_set_output_source(1, m_audioSettings.enabled ? m_desktopAudioSource : nullptr);
    }

    // --- Microphone Source ---
    if (micDeviceChanged || (!m_microphoneSource && m_microphoneSettings.enabled))
    {
        qDebug() << "Recreating microphone source due to device change or being enabled.";
        if (m_microphoneSource)
            obs_source_release(m_microphoneSource);
        m_microphoneSource = m_microphoneSettings.enabled ? CreateMicrophoneSource() : nullptr;
    }

    // Always apply current settings to the microphone source.
    if (m_microphoneSource)
    {
        obs_source_set_volume(m_microphoneSource, m_microphoneSettings.volume);
        obs_source_set_enabled(m_microphoneSource, m_microphoneSettings.enabled);

        obs_source_t *filter = obs_source_get_filter_by_name(m_microphoneSource, "Noise Suppression");
        if (m_microphoneSettings.noiseSuppression)
        {
            if (!filter)
            {
                obs_source_t *new_filter = obs_source_create("noise_suppress_filter", "Noise Suppression", nullptr, nullptr);
                if (new_filter)
                {
                    obs_source_filter_add(m_microphoneSource, new_filter);
                    obs_source_release(new_filter);
                }
            }
            else
            {
                obs_source_set_enabled(filter, true);
            }
        }
        else
        {
            if (filter)
            {
                obs_source_set_enabled(filter, false);
            }
        }
        if (filter)
            obs_source_release(filter);

        obs_set_output_source(2, m_microphoneSettings.enabled ? m_microphoneSource : nullptr);
    }
    else
    {
        obs_set_output_source(2, nullptr);
    }

    // --- Audio Encoder ---
    if (encoderSettingsChanged)
    {
        qDebug() << "Recreating audio encoder due to settings change.";
        if (m_bufferAudioEncoder)
            obs_encoder_release(m_bufferAudioEncoder);
        m_bufferAudioEncoder = CreateAudioEncoder();
        if (!m_bufferAudioEncoder)
        {
            qWarning() << "Failed to create audio encoder!";
            return false;
        }
        obs_encoder_set_audio(m_bufferAudioEncoder, obs_get_audio());
    }

    return true;
}

bool GameCapture::FastBufferReset()
{
    if (!m_bufferOutput || !obs_output_active(m_bufferOutput) || m_pendingBufferCallback)
    {
        return false;
    }

    // Asynchronously stop and then restart the buffer output. This is much
    // faster than destroying and recreating the entire object.
    m_pendingBufferCallback = [this]()
    {
        m_pendingBufferCallback = nullptr;
        if (!m_bufferVideoEncoder || !m_bufferAudioEncoder)
        {
            StopClippingMode();
            QTimer::singleShot(500, this, &GameCapture::StartClippingMode);
            return;
        }
        if (!obs_output_start(m_bufferOutput))
        {
            StopClippingMode();
            QTimer::singleShot(500, this, &GameCapture::StartClippingMode);
            return;
        }
        QTimer::singleShot(500, [this]()
                           {
            if (m_clippingModeActive.load() && (!m_bufferOutput || !obs_output_active(m_bufferOutput))) {
                 StopClippingMode();
                 QTimer::singleShot(500, this, &GameCapture::StartClippingMode);
            } });
    };

    signal_handler_t *handler = obs_output_get_signal_handler(m_bufferOutput);
    if (handler)
    {
        signal_handler_connect(handler, "stop", onBufferStopSignal, this);
    }
    m_bufferStopTimer->start();
    obs_output_stop(m_bufferOutput);
    return true;
}