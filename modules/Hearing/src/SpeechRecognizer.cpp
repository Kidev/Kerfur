#include "SpeechRecognizer.hpp"
#include <cmath>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QDebug>

// AudioProcessor implementation
AudioProcessor::AudioProcessor(QObject *parent) : QIODevice {parent}
{
    open(QIODevice::WriteOnly);
}

qint64 AudioProcessor::readData(char *data, qint64 maxlen)
{
    Q_UNUSED(data)
    Q_UNUSED(maxlen)
    return 0; // Write-only device
}

qint64 AudioProcessor::writeData(const char *data, qint64 len)
{
    QByteArray audioData {data, static_cast<int>(len)};
    emit audioDataReady(audioData);
    return len;
}

// SpeechRecognizer implementation
SpeechRecognizer::SpeechRecognizer(QObject *parent)
    : QObject {parent}
    , m_whisperEngine {std::make_unique<WhisperEngine>(this)}
    , m_volumeUpdateTimer {this}
    , m_kerfurResetTimer {this}
{
    // Initialize Whisper engine
    QString modelPath {"models/ggml-tiny.en.bin"};
    if (!m_whisperEngine->initialize(modelPath)) {
        setErrorString("Failed to initialize speech recognition: " +
                       m_whisperEngine->getLastError());
        qWarning() << "Whisper initialization failed:" << m_whisperEngine->getLastError();
    } else {
        qDebug() << "Whisper engine initialized successfully";
    }

    // Connect Whisper signals
    connect(m_whisperEngine.get(),
            &WhisperEngine::transcriptionReady,
            this,
            &SpeechRecognizer::handleTranscription);
    connect(m_whisperEngine.get(),
            &WhisperEngine::keywordDetected,
            this,
            &SpeechRecognizer::handleKeywordDetected);
    connect(m_whisperEngine.get(), &WhisperEngine::processingStarted, this, [this]() {
        setProcessing(true);
    });
    connect(m_whisperEngine.get(), &WhisperEngine::processingFinished, this, [this]() {
        setProcessing(false);
    });

    // Setup volume update timer
    m_volumeUpdateTimer.setInterval(50);
    connect(&m_volumeUpdateTimer, &QTimer::timeout, this, &SpeechRecognizer::updateVolumeLevel);

    // Setup kerfur detection reset timer
    m_kerfurResetTimer.setSingleShot(true);
    m_kerfurResetTimer.setInterval(2000);
    connect(&m_kerfurResetTimer, &QTimer::timeout, this, &SpeechRecognizer::resetKerfurDetection);

    // Enumerate available audio devices
    enumerateAudioDevices();
}

SpeechRecognizer::~SpeechRecognizer()
{
    if (m_enabled) {
        uninitializeAudio();
    }
}

void SpeechRecognizer::enumerateAudioDevices()
{
    m_audioDevices.clear();
    QStringList deviceNames {};

    // First, always try to get the default device
    QAudioDevice defaultDevice {QMediaDevices::defaultAudioInput()};

    // Get all audio input devices
    const QList<QAudioDevice> devices {QMediaDevices::audioInputs()};

    qDebug() << "Qt detected" << devices.size() << "audio input devices";
    qDebug() << "Default device is" << (defaultDevice.isNull() ? "NULL" : "valid");

    // If we have a valid default device, use it
    if (!defaultDevice.isNull()) {
        m_selectedDevice = defaultDevice;
        m_audioDevices.append(defaultDevice);
        QString name {defaultDevice.description()};
        if (name.isEmpty()) {
            name = "Default Audio Input";
        }
        deviceNames.append(name);
        qDebug() << "Using default device:" << name << "ID:" << defaultDevice.id();
    }

    // Add other devices if they exist and are different from default
    for (const auto &device : devices) {
        if (!device.isNull() && device.id() != defaultDevice.id()) {
            m_audioDevices.append(device);
            QString name {device.description()};
            if (name.isEmpty()) {
                name = QString {"Audio Device %1"}.arg(m_audioDevices.size());
            }
            deviceNames.append(name);
            qDebug() << "Found device:" << name << "ID:" << device.id();
        }
    }

    // If still no devices, create a dummy entry to prevent UI issues
    if (m_audioDevices.isEmpty() && defaultDevice.isNull()) {
        qWarning() << "No audio devices detected by Qt. Audio system may not be initialized.";
        qWarning() << "Make sure PulseAudio or PipeWire is running.";

        // Check if we can still create a QAudioSource with default device
        // This sometimes works even when device enumeration fails
        m_selectedDevice = QAudioDevice {}; // Empty device will use system default
        deviceNames.append("System Audio (No devices detected)");
    }

    setAvailableDevices(deviceNames);

    if (!m_selectedDevice.isNull() || !deviceNames.isEmpty()) {
        setCurrentDevice(deviceNames.isEmpty() ? "System Audio" : deviceNames.first());
    }

    qDebug() << "Audio enumeration complete:";
    qDebug() << "  Available devices:" << deviceNames.size();
    qDebug() << "  Selected device:"
             << (m_selectedDevice.isNull() ? "NULL (will try system default)"
                                           : m_selectedDevice.description());
}

void SpeechRecognizer::setDevice(const QString &deviceName)
{
    QAudioDevice selectedDevice {};

    for (const auto &device : m_audioDevices) {
        if (device.description() == deviceName ||
            (device.description().isEmpty() && deviceName == "Default Audio Input")) {
            selectedDevice = device;
            break;
        }
    }

    if (selectedDevice.isNull()) {
        qWarning() << "Device not found:" << deviceName;
        return;
    }

    if (m_selectedDevice == selectedDevice) {
        return; // No change
    }

    m_selectedDevice = selectedDevice;
    setCurrentDevice(deviceName);

    if (m_enabled) {
        uninitializeAudio();
        initializeAudio();
    }
}

void SpeechRecognizer::refreshDevices()
{
    uninitializeAudio();
    enumerateAudioDevices();
    if (m_enabled) {
        initializeAudio();
    }
}

void SpeechRecognizer::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;

    if (m_enabled) {
        initializeAudio();
    } else {
        uninitializeAudio();
    }

    emit enabledChanged();
}

void SpeechRecognizer::initializeAudio()
{
    if (!m_whisperEngine->isInitialized()) {
        setErrorString("Speech recognition engine not initialized");
        return;
    }

    // If no device selected, try to enumerate again
    if (m_selectedDevice.isNull() && m_audioDevices.isEmpty()) {
        enumerateAudioDevices();
    }

    // Setup audio format
    QAudioFormat format {};
    format.setSampleRate(48000);
    //format.setChannelCount(1);
    //format.setSampleFormat(QAudioFormat::UInt8);

    QAudioDevice deviceToUse {m_selectedDevice};

    // If still no device, try using an empty QAudioDevice which will use system default
    if (deviceToUse.isNull()) {
        qWarning() << "No audio device selected, attempting to use system default";
        deviceToUse = QAudioDevice {}; // Empty device = system default
    }

    // Check format support
    if (!deviceToUse.isNull() && !deviceToUse.isFormatSupported(format)) {
        format = deviceToUse.preferredFormat();
        if (format.channelCount() > 1) {
            format.setChannelCount(1);
        }
        if (format.sampleRate() > 16000) {
            format.setSampleRate(16000);
        }
    }

    m_actualFormat = format;

    qDebug() << "Initializing audio with format: Rate=" << format.sampleRate()
             << "Channels=" << format.channelCount() << "Format=" << format.sampleFormat();

    try {
        // Create audio source - will use system default if device is null
        m_audioSource = std::make_unique<QAudioSource>(deviceToUse, format);
        m_audioSource->setBufferSize(200);

        // Connect state monitoring
        connect(m_audioSource.get(), &QAudioSource::stateChanged, this, [this](QAudio::State state) {
            if (state == QAudio::ActiveState) {
                setListening(true);
                setErrorString("");
                m_volumeUpdateTimer.start();
            } else if (state == QAudio::StoppedState) {
                setListening(false);
                if (m_audioSource && m_audioSource->error() != QAudio::NoError) {
                    setErrorString("Audio error occurred");
                }
            }
        });

        // Create audio processor
        m_audioProcessor = std::make_unique<AudioProcessor>(this);
        connect(m_audioProcessor.get(),
                &AudioProcessor::audioDataReady,
                this,
                &SpeechRecognizer::processAudioData);

        // Start audio capture
        m_audioSource->start(m_audioProcessor.get());

        qDebug() << "Audio capture started";

    } catch (const std::exception &e) {
        setErrorString(QString {"Audio initialization failed: %1"}.arg(e.what()));
    }
}

void SpeechRecognizer::uninitializeAudio()
{
    m_volumeUpdateTimer.stop();

    if (m_audioSource) {
        disconnect(m_audioSource.get(), nullptr, this, nullptr);
        m_audioSource->stop();
        m_audioSource.reset();
    }

    if (m_audioProcessor) {
        m_audioProcessor->close();
        m_audioProcessor.reset();
    }

    if (m_whisperEngine) {
        m_whisperEngine->reset();
    }

    setListening(false);
    setVolumeLevel(0.0f);
}

void SpeechRecognizer::processAudioData(const QByteArray &data)
{
    if (!m_listening || !m_whisperEngine->isInitialized())
        return;

    // Keep buffer for volume calculation
    m_audioBuffer = data;

    // Convert to float samples for Whisper
    std::vector<float> floatSamples {};

    if (m_actualFormat.sampleFormat() == QAudioFormat::UInt8) {
        // Convert UInt8 to float [-1, 1]
        const uint8_t *samples {reinterpret_cast<const uint8_t *>(data.constData())};
        int sampleCount {static_cast<int>(data.size())};
        floatSamples.reserve(sampleCount);

        for (int i {0}; i < sampleCount; ++i) {
            floatSamples.push_back((samples[i] - 128) / 128.0f);
        }
    } else if (m_actualFormat.sampleFormat() == QAudioFormat::Int16) {
        // Convert Int16 to float [-1, 1]
        const int16_t *samples {reinterpret_cast<const int16_t *>(data.constData())};
        int sampleCount {static_cast<int>(data.size()) / static_cast<int>(sizeof(int16_t))};
        floatSamples.reserve(sampleCount);

        if (m_actualFormat.channelCount() == 1) {
            for (int i {0}; i < sampleCount; ++i) {
                floatSamples.push_back(samples[i] / 32768.0f);
            }
        } else {
            // Convert stereo to mono by averaging
            for (int i {0}; i < sampleCount - 1; i += 2) {
                floatSamples.push_back((samples[i] + samples[i + 1]) / 65536.0f);
            }
        }
    } else if (m_actualFormat.sampleFormat() == QAudioFormat::Float) {
        const float *samples {reinterpret_cast<const float *>(data.constData())};
        int sampleCount {static_cast<int>(data.size()) / static_cast<int>(sizeof(float))};

        if (m_actualFormat.channelCount() == 1) {
            floatSamples.assign(samples, samples + sampleCount);
        } else {
            // Convert stereo to mono
            floatSamples.reserve(sampleCount / 2);
            for (int i {0}; i < sampleCount - 1; i += 2) {
                floatSamples.push_back((samples[i] + samples[i + 1]) * 0.5f);
            }
        }
    }

    // Resample to 16kHz if needed
    if (m_actualFormat.sampleRate() != SAMPLE_RATE && !floatSamples.empty()) {
        std::vector<float> resampled {};
        float ratio {static_cast<float>(SAMPLE_RATE) / m_actualFormat.sampleRate()};
        int newSize {static_cast<int>(floatSamples.size() * ratio)};
        resampled.reserve(newSize);

        for (int i {0}; i < newSize; ++i) {
            float srcIdx {i / ratio};
            int idx {static_cast<int>(srcIdx)};
            float frac {srcIdx - idx};

            if (idx + 1 < static_cast<int>(floatSamples.size())) {
                resampled.push_back(floatSamples[idx] * (1.0f - frac) +
                                    floatSamples[idx + 1] * frac);
            } else if (idx < static_cast<int>(floatSamples.size())) {
                resampled.push_back(floatSamples[idx]);
            }
        }
        floatSamples = std::move(resampled);
    }

    if (!floatSamples.empty()) {
        m_whisperEngine->processAudio(floatSamples);
    }
}

void SpeechRecognizer::handleTranscription(const QString &text, float confidence)
{
    if (!text.isEmpty()) {
        setLastText(text);
        setConfidence(confidence);
        emit sentenceRecognized(text, confidence);
        qDebug() << "Transcription:" << text << "confidence:" << confidence;
    }
}

void SpeechRecognizer::handleKeywordDetected(const QString &keyword)
{
    Q_UNUSED(keyword)
    setKerfurDetected(true);
    m_kerfurResetTimer.start();
    emit kerfurHeard();
    qDebug() << "KERFUR DETECTED!";
}

void SpeechRecognizer::updateVolumeLevel()
{
    if (!m_audioBuffer.isEmpty()) {
        float volume {calculateVolume(m_audioBuffer)};
        setVolumeLevel(volume);
        m_audioBuffer.clear();
    } else {
        setVolumeLevel(m_volumeLevel * 0.95f);
    }
}

float SpeechRecognizer::calculateVolume(const QByteArray &data)
{
    if (data.isEmpty())
        return 0.0f;

    float maxAmplitude {0.0f};

    if (m_actualFormat.sampleFormat() == QAudioFormat::UInt8) {
        const uint8_t *samples {reinterpret_cast<const uint8_t *>(data.constData())};
        int sampleCount {static_cast<int>(data.size())};

        for (int i {0}; i < sampleCount; ++i) {
            float amplitude {std::abs((samples[i] - 128) / 128.0f)};
            maxAmplitude = std::max(maxAmplitude, amplitude);
        }
    } else if (m_actualFormat.sampleFormat() == QAudioFormat::Int16) {
        const int16_t *samples {reinterpret_cast<const int16_t *>(data.constData())};
        int sampleCount {static_cast<int>(data.size()) / static_cast<int>(sizeof(int16_t))};

        for (int i {0}; i < sampleCount; ++i) {
            float amplitude {std::abs(samples[i] / 32768.0f)};
            maxAmplitude = std::max(maxAmplitude, amplitude);
        }
    } else if (m_actualFormat.sampleFormat() == QAudioFormat::Float) {
        const float *samples {reinterpret_cast<const float *>(data.constData())};
        int sampleCount {static_cast<int>(data.size()) / static_cast<int>(sizeof(float))};

        for (int i {0}; i < sampleCount; ++i) {
            maxAmplitude = std::max(maxAmplitude, std::abs(samples[i]));
        }
    }

    return std::clamp(maxAmplitude, 0.0f, 1.0f);
}

void SpeechRecognizer::resetKerfurDetection()
{
    setKerfurDetected(false);
}

// Property setters
void SpeechRecognizer::setListening(bool listening)
{
    if (m_listening == listening)
        return;
    m_listening = listening;
    emit listeningChanged();
}

void SpeechRecognizer::setLastText(const QString &text)
{
    if (m_lastText == text)
        return;
    m_lastText = text;
    emit lastTextChanged();
}

void SpeechRecognizer::setConfidence(float confidence)
{
    if (qFuzzyCompare(m_confidence, confidence))
        return;
    m_confidence = confidence;
    emit confidenceChanged();
}

void SpeechRecognizer::setErrorString(const QString &error)
{
    if (m_errorString == error)
        return;
    m_errorString = error;
    emit errorStringChanged();
}

void SpeechRecognizer::setKerfurDetected(bool detected)
{
    if (m_kerfurDetected == detected)
        return;
    m_kerfurDetected = detected;
    emit kerfurDetectedChanged();
}

void SpeechRecognizer::setVolumeLevel(float level)
{
    if (qFuzzyCompare(m_volumeLevel, level))
        return;
    m_volumeLevel = level;
    emit volumeLevelChanged();
}

void SpeechRecognizer::setProcessing(bool processing)
{
    if (m_processing == processing)
        return;
    m_processing = processing;
    emit processingChanged();
}

void SpeechRecognizer::setAvailableDevices(const QStringList &devices)
{
    if (m_availableDevices == devices)
        return;
    m_availableDevices = devices;
    emit availableDevicesChanged();
}

void SpeechRecognizer::setCurrentDevice(const QString &device)
{
    if (m_currentDevice == device)
        return;
    m_currentDevice = device;
    emit currentDeviceChanged();
}
