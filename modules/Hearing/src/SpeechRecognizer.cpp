#include "SpeechRecognizer.hpp"
#include <QAudioDevice>
#include <QAudioFormat>
#include <QDebug>
#include <cmath>
#include <QProcess>

// AudioProcessor implementation
AudioProcessor::AudioProcessor(QObject *parent)
: QIODevice{parent}
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
    QByteArray audioData{data, static_cast<int>(len)};
    emit audioDataReady(audioData);
    return len;
}

// SpeechRecognizer implementation
SpeechRecognizer::SpeechRecognizer(QObject *parent)
: QObject{parent}
, m_whisperEngine{std::make_unique<WhisperEngine>(this)}
, m_volumeUpdateTimer{this}
, m_kerfurResetTimer{this}
{
    // Initialize Whisper engine
    QString modelPath = "models/ggml-tiny.en.bin";
    if (!m_whisperEngine->initialize(modelPath)) {
        setErrorString("Failed to initialize speech recognition: " + m_whisperEngine->getLastError());
    }

    // Connect Whisper signals
    connect(m_whisperEngine.get(), &WhisperEngine::transcriptionReady,
            this, &SpeechRecognizer::handleTranscription);
    connect(m_whisperEngine.get(), &WhisperEngine::keywordDetected,
            this, &SpeechRecognizer::handleKeywordDetected);
    connect(m_whisperEngine.get(), &WhisperEngine::processingStarted,
            this, [this]() { setProcessing(true); });
    connect(m_whisperEngine.get(), &WhisperEngine::processingFinished,
            this, [this]() { setProcessing(false); });

    // Setup volume update timer
    m_volumeUpdateTimer.setInterval(50);
    connect(&m_volumeUpdateTimer, &QTimer::timeout,
            this, &SpeechRecognizer::updateVolumeLevel);

    // Setup kerfur detection reset timer
    m_kerfurResetTimer.setSingleShot(true);
    m_kerfurResetTimer.setInterval(2000);
    connect(&m_kerfurResetTimer, &QTimer::timeout,
            this, &SpeechRecognizer::resetKerfurDetection);

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
    qDebug() << "=== Enumerating Audio Devices ===";

    m_audioDevices.clear();
    QStringList deviceNames;

    // Step 1: Get Qt's detected devices
    QList<QAudioDevice> qtDevices = QMediaDevices::audioInputs();
    qDebug() << "Qt detected" << qtDevices.size() << "audio input devices";

    for (const auto& device : qtDevices) {
        m_audioDevices.append(device);
        QString name = device.description();
        if (name.isEmpty()) {
            name = QString("Audio Device %1").arg(m_audioDevices.size());
        }
        deviceNames.append(name);
        qDebug() << "Qt device:" << name << "ID:" << device.id();
    }

    // Step 2: Always try to get the default device
    QAudioDevice defaultDevice = QMediaDevices::defaultAudioInput();
    bool hasDefault = !defaultDevice.isNull();

    if (hasDefault) {
        // Check if default is already in our list
        bool defaultAlreadyListed = false;
        for (const auto& existing : m_audioDevices) {
            if (existing.id() == defaultDevice.id()) {
                defaultAlreadyListed = true;
                break;
            }
        }

        if (!defaultAlreadyListed) {
            m_audioDevices.append(defaultDevice);
            QString defaultName = defaultDevice.description();
            if (defaultName.isEmpty()) {
                defaultName = "System Default";
            }
            deviceNames.append(defaultName);
            qDebug() << "Added system default:" << defaultName;
        }
    }

    // Step 3: Linux fallback - if we have no devices but PulseAudio is available
    if (m_audioDevices.isEmpty()) {
        #ifdef Q_OS_LINUX
        qDebug() << "No Qt devices found, checking PulseAudio availability...";

        QProcess pactl;
        pactl.start("pactl", {"info"});
        pactl.waitForFinished(3000);

        if (pactl.exitCode() == 0) {
            qDebug() << "PulseAudio is running, will use system default";

            // Create a synthetic "System Default" entry
            // We'll handle this special case in audio initialization
            deviceNames.append("System Default (PulseAudio)");
            // Leave m_audioDevices empty for this case - we'll handle it specially

            setAvailableDevices(deviceNames);
            setCurrentDevice("System Default (PulseAudio)");

            // Set a special flag that we're using PulseAudio fallback
            m_selectedDevice = QAudioDevice{}; // Empty device as signal

            qDebug() << "Created PulseAudio fallback entry";
            qDebug() << "=== End Audio Device Enumeration ===";
            return;
        }
        #endif

        // No devices found and no PulseAudio
        setErrorString("No audio input devices available");
        setAvailableDevices(QStringList());
        setCurrentDevice("");
        qDebug() << "No audio devices found at all";
        qDebug() << "=== End Audio Device Enumeration ===";
        return;
    }

    // Step 4: We have at least one device, set up the lists
    setAvailableDevices(deviceNames);

    // Step 5: Select the best device
    if (hasDefault) {
        // Use the default device
        m_selectedDevice = defaultDevice;
        QString defaultName = defaultDevice.description();
        if (defaultName.isEmpty()) {
            defaultName = "System Default";
        }
        setCurrentDevice(defaultName);
        qDebug() << "Selected default device:" << defaultName;
    } else if (!m_audioDevices.isEmpty()) {
        // Use the first available device
        m_selectedDevice = m_audioDevices.first();
        setCurrentDevice(deviceNames.first());
        qDebug() << "Selected first device:" << deviceNames.first();
    }

    // Clear any previous error
    setErrorString("");

    qDebug() << "Final setup:";
    qDebug() << "  Available devices:" << deviceNames.size();
    qDebug() << "  Selected device:" << (m_selectedDevice.isNull() ? "NULL" : m_selectedDevice.description());
    qDebug() << "  Current device name:" << m_currentDevice;
    qDebug() << "=== End Audio Device Enumeration ===";
}

void SpeechRecognizer::setDevice(const QString &deviceName)
{
    qDebug() << "Attempting to set audio device to:" << deviceName;

    // Find device by name
    QAudioDevice selectedDevice;
    for (const auto& device : m_audioDevices) {
        if (device.description() == deviceName) {
            selectedDevice = device;
            break;
        }
    }

    if (selectedDevice.isNull()) {
        qWarning() << "Device not found:" << deviceName;
        setErrorString("Selected audio device not found: " + deviceName);
        return;
    }

    // If device is the same, don't restart
    if (m_selectedDevice == selectedDevice) {
        qDebug() << "Device already selected:" << deviceName;
        return;
    }

    m_selectedDevice = selectedDevice;
    setCurrentDevice(deviceName);

    // If currently enabled, restart with new device
    if (m_enabled) {
        qDebug() << "Restarting audio with new device:" << deviceName;
        uninitializeAudio();
        initializeAudio();
    }
}

void SpeechRecognizer::refreshDevices()
{
    qDebug() << "Refreshing audio devices...";
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
        if (m_audioDevices.isEmpty()) {
            enumerateAudioDevices();
        }
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

    if (m_audioDevices.isEmpty() && m_selectedDevice.isNull()) {
        enumerateAudioDevices();

        // Check if we still don't have a device after enumeration
        if (m_audioDevices.isEmpty() && m_selectedDevice.isNull() && !m_currentDevice.contains("PulseAudio")) {
            setErrorString("No audio input devices available");
            return;
        }
    }

    qDebug() << "Initializing audio...";
    qDebug() << "Current device:" << m_currentDevice;
    qDebug() << "Selected device null:" << m_selectedDevice.isNull();
    qDebug() << "Audio devices count:" << m_audioDevices.size();

    // Setup audio format
    QAudioFormat format;
    format.setSampleRate(SAMPLE_RATE);
    format.setChannelCount(CHANNEL_COUNT);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice deviceToUse;

    // Determine which device to use
    if (m_currentDevice.contains("PulseAudio") || (m_selectedDevice.isNull() && m_audioDevices.isEmpty())) {
        // Use system default for PulseAudio fallback
        deviceToUse = QMediaDevices::defaultAudioInput();
        qDebug() << "Using PulseAudio fallback with system default";

        if (deviceToUse.isNull()) {
            // Create with default constructor - Qt will try to find something
            qDebug() << "System default is null, using default constructor";
        }
    } else if (!m_selectedDevice.isNull()) {
        deviceToUse = m_selectedDevice;
        qDebug() << "Using selected device:" << deviceToUse.description();
    } else if (!m_audioDevices.isEmpty()) {
        deviceToUse = m_audioDevices.first();
        qDebug() << "Using first available device:" << deviceToUse.description();
    } else {
        setErrorString("No usable audio device found");
        return;
    }

    // Check format support if we have a valid device
    if (!deviceToUse.isNull() && !deviceToUse.isFormatSupported(format)) {
        qDebug() << "Preferred format not supported, using device's preferred format";

        // Try some common formats
        QList<std::tuple<int, int, QAudioFormat::SampleFormat>> formats = {
            {16000, 1, QAudioFormat::Int16},
            {44100, 1, QAudioFormat::Int16},
            {48000, 1, QAudioFormat::Int16},
            {16000, 2, QAudioFormat::Int16},
            {44100, 2, QAudioFormat::Int16}
        };

        bool formatFound = false;
        for (const auto& [rate, channels, sampleFormat] : formats) {
            format.setSampleRate(rate);
            format.setChannelCount(channels);
            format.setSampleFormat(sampleFormat);

            if (deviceToUse.isFormatSupported(format)) {
                qDebug() << "Using format: Rate=" << rate << "Channels=" << channels;
                formatFound = true;
                break;
            }
        }

        if (!formatFound) {
            qDebug() << "No supported format found, trying with preferred format anyway";
            format.setSampleRate(SAMPLE_RATE);
            format.setChannelCount(CHANNEL_COUNT);
            format.setSampleFormat(QAudioFormat::Int16);
        }
    }

    m_actualFormat = format;

    try {
        // Create audio source
        qDebug() << "Creating QAudioSource with format:" << format.sampleRate() << "Hz," << format.channelCount() << "ch";
        m_audioSource = std::make_unique<QAudioSource>(deviceToUse, format);
        m_audioSource->setBufferSize(BUFFER_SIZE * 2);

        // Connect state monitoring
        connect(m_audioSource.get(), &QAudioSource::stateChanged,
                this, [this](QAudio::State state) {
                    qDebug() << "Audio state changed to:" << state;
                    switch (state) {
                        case QAudio::ActiveState:
                            setListening(true);
                            setErrorString("");
                            qDebug() << "Audio is now active and listening";
                            break;
                        case QAudio::IdleState:
                            setListening(true); // Still listening, just no input
                            break;
                        case QAudio::StoppedState:
                            setListening(false);
                            if (m_audioSource && m_audioSource->error() != QAudio::NoError) {
                                int errorCode = static_cast<int>(m_audioSource->error());
                                setErrorString("Audio stopped with error code: " + QString::number(errorCode));
                                qDebug() << "Audio error:" << errorCode;
                            }
                            break;
                    }
                });

        // Create audio processor
        m_audioProcessor = std::make_unique<AudioProcessor>(this);
        connect(m_audioProcessor.get(), &AudioProcessor::audioDataReady,
                this, &SpeechRecognizer::processAudioData);

        // Start audio capture
        qDebug() << "Starting audio capture...";
        m_audioSource->start(m_audioProcessor.get());

        QAudio::Error initialError = m_audioSource->error();
        if (initialError != QAudio::NoError) {
            setErrorString("Failed to start audio: error " + QString::number(static_cast<int>(initialError)));
            qDebug() << "Initial audio error:" << static_cast<int>(initialError);
            return;
        }

        // Check status after a short delay
        QTimer::singleShot(500, this, [this]() {
            if (!m_audioSource) return;

            QAudio::Error error = m_audioSource->error();
            QAudio::State state = m_audioSource->state();

            qDebug() << "Audio status check - State:" << state << "Error:" << error;

            if (error != QAudio::NoError) {
                setErrorString("Audio error: " + QString::number(static_cast<int>(error)));
            } else if (state == QAudio::ActiveState || state == QAudio::IdleState) {
                qDebug() << "Audio system working correctly";
                m_volumeUpdateTimer.start();
                setErrorString("");
            } else {
                setErrorString("Audio failed to activate properly");
            }
        });

    } catch (const std::exception& e) {
        setErrorString(QString("Audio initialization failed: %1").arg(e.what()));
        qDebug() << "Audio init exception:" << e.what();
    }

    qDebug() << "Audio initialization completed";
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

    qDebug() << "Speech recognition stopped";
}

void SpeechRecognizer::processAudioData(const QByteArray &data)
{
    if (!m_listening || !m_whisperEngine->isInitialized())
        return;

    // Keep buffer for volume calculation
    m_audioBuffer.append(data);
    if (m_audioBuffer.size() > BUFFER_SIZE * 4) {
        m_audioBuffer = m_audioBuffer.mid(m_audioBuffer.size() - BUFFER_SIZE * 2);
    }

    // Convert to float and send to Whisper
    auto floatSamples = convertAudioToFloat(data);
    if (!floatSamples.empty()) {
        m_whisperEngine->processAudio(floatSamples);
    }
}

std::vector<float> SpeechRecognizer::convertAudioToFloat(const QByteArray &data)
{
    std::vector<float> floatSamples;

    if (data.isEmpty()) {
        return floatSamples;
    }

    if (m_actualFormat.sampleFormat() == QAudioFormat::Int16) {
        const int16_t *samples = reinterpret_cast<const int16_t*>(data.constData());
        int sampleCount = data.size() / sizeof(int16_t);

        floatSamples.reserve(sampleCount);

        // Handle mono/stereo conversion
        if (m_actualFormat.channelCount() == 1) {
            // Mono - direct conversion
            for (int i = 0; i < sampleCount; ++i) {
                floatSamples.push_back(samples[i] / 32768.0f);
            }
        } else if (m_actualFormat.channelCount() == 2) {
            // Stereo - convert to mono by averaging
            floatSamples.reserve(sampleCount / 2);
            for (int i = 0; i < sampleCount - 1; i += 2) {
                float left = samples[i] / 32768.0f;
                float right = samples[i + 1] / 32768.0f;
                floatSamples.push_back((left + right) * 0.5f);
            }
        }
    } else if (m_actualFormat.sampleFormat() == QAudioFormat::Float) {
        const float *samples = reinterpret_cast<const float*>(data.constData());
        int sampleCount = data.size() / sizeof(float);

        floatSamples.reserve(sampleCount);

        if (m_actualFormat.channelCount() == 1) {
            // Mono - direct copy
            for (int i = 0; i < sampleCount; ++i) {
                floatSamples.push_back(samples[i]);
            }
        } else if (m_actualFormat.channelCount() == 2) {
            // Stereo - convert to mono by averaging
            floatSamples.reserve(sampleCount / 2);
            for (int i = 0; i < sampleCount - 1; i += 2) {
                floatSamples.push_back((samples[i] + samples[i + 1]) * 0.5f);
            }
        }
    }

    // Resample if needed (simple linear interpolation)
    if (m_actualFormat.sampleRate() != SAMPLE_RATE && !floatSamples.empty()) {
        std::vector<float> resampledSamples;
        float ratio = static_cast<float>(SAMPLE_RATE) / m_actualFormat.sampleRate();
        int newSize = static_cast<int>(floatSamples.size() * ratio);

        resampledSamples.reserve(newSize);

        for (int i = 0; i < newSize; ++i) {
            float srcIndex = i / ratio;
            int srcIdx = static_cast<int>(srcIndex);
            float frac = srcIndex - srcIdx;

            if (srcIdx + 1 < static_cast<int>(floatSamples.size())) {
                float sample = floatSamples[srcIdx] * (1.0f - frac) +
                floatSamples[srcIdx + 1] * frac;
                resampledSamples.push_back(sample);
            } else if (srcIdx < static_cast<int>(floatSamples.size())) {
                resampledSamples.push_back(floatSamples[srcIdx]);
            }
        }

        return resampledSamples;
    }

    return floatSamples;
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
        float volume = calculateVolume(m_audioBuffer);
        setVolumeLevel(volume);
    } else {
        setVolumeLevel(m_volumeLevel * 0.9f);
    }
}

float SpeechRecognizer::calculateVolume(const QByteArray &data)
{
    if (data.isEmpty())
        return 0.0f;

    if (m_actualFormat.sampleFormat() == QAudioFormat::Int16) {
        const int16_t *samples = reinterpret_cast<const int16_t*>(data.constData());
        int sampleCount = data.size() / sizeof(int16_t);

        if (sampleCount == 0)
            return 0.0f;

        // Calculate RMS
        float sum = 0.0f;
        int step = std::max(1, sampleCount / 1000);

        for (int i = 0; i < sampleCount; i += step) {
            float normalized = samples[i] / 32768.0f;
            sum += normalized * normalized;
        }

        float rms = std::sqrt(sum / (sampleCount / step));
        float db = 20.0f * std::log10(std::max(rms, 0.00001f));
        float normalized = (db + 60.0f) / 60.0f;

        return std::clamp(normalized, 0.0f, 1.0f);
    }

    return 0.0f;
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

    if (!error.isEmpty()) {
        qWarning() << "SpeechRecognizer error:" << error;
    }
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
