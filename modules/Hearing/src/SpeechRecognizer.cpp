#include "SpeechRecognizer.hpp"
#include <QAudioDevice>
#include <QAudioFormat>
#include <QDebug>
#include <cmath>

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
    m_volumeUpdateTimer.setInterval(100);
    connect(&m_volumeUpdateTimer, &QTimer::timeout,
            this, &SpeechRecognizer::updateVolumeLevel);

    // Setup kerfur detection reset timer
    m_kerfurResetTimer.setSingleShot(true);
    m_kerfurResetTimer.setInterval(2000);
    connect(&m_kerfurResetTimer, &QTimer::timeout,
            this, &SpeechRecognizer::resetKerfurDetection);
}

SpeechRecognizer::~SpeechRecognizer()
{
    if (m_enabled) {
        uninitializeAudio();
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

    // Get default audio input device
    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        setErrorString("No audio input device available");
        return;
    }

    // Setup audio format for Whisper (16kHz, mono, 16-bit)
    QAudioFormat format;
    format.setSampleRate(SAMPLE_RATE);
    format.setChannelCount(CHANNEL_COUNT);
    format.setSampleFormat(QAudioFormat::Int16);

    // Check if format is supported
    if (!inputDevice.isFormatSupported(format)) {
        setErrorString("Audio format not supported by device");
        return;
    }

    // Create audio input
    m_audioInput = std::make_unique<QAudioInput>(inputDevice);
    m_audioInput->setVolume(1.0);

    // Create audio source
    m_audioSource = std::make_unique<QAudioSource>(inputDevice, format);
    m_audioSource->setBufferSize(BUFFER_SIZE);

    // Create audio processor
    m_audioProcessor = std::make_unique<AudioProcessor>(this);
    connect(m_audioProcessor.get(), &AudioProcessor::audioDataReady,
            this, &SpeechRecognizer::processAudioData);

    // Start audio capture
    m_audioSource->start(m_audioProcessor.get());

    if (m_audioSource->error() != QAudio::NoError) {
        setErrorString("Failed to start audio capture");
        return;
    }

    setListening(true);
    m_volumeUpdateTimer.start();
    setErrorString("");

    qDebug() << "Speech recognition started";
}

void SpeechRecognizer::uninitializeAudio()
{
    m_volumeUpdateTimer.stop();

    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource.reset();
    }

    if (m_audioProcessor) {
        m_audioProcessor->close();
        m_audioProcessor.reset();
    }

    m_audioInput.reset();

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
    if (m_audioBuffer.size() > BUFFER_SIZE * 2) {
        m_audioBuffer = m_audioBuffer.mid(m_audioBuffer.size() - BUFFER_SIZE);
    }

    // Convert to float and send to Whisper
    auto floatSamples = convertAudioToFloat(data);
    m_whisperEngine->processAudio(floatSamples);
}

std::vector<float> SpeechRecognizer::convertAudioToFloat(const QByteArray &data)
{
    const int16_t *samples = reinterpret_cast<const int16_t*>(data.constData());
    int sampleCount = data.size() / sizeof(int16_t);

    std::vector<float> floatSamples;
    floatSamples.reserve(sampleCount);

    for (int i = 0; i < sampleCount; ++i) {
        floatSamples.push_back(samples[i] / 32768.0f);
    }

    return floatSamples;
}

void SpeechRecognizer::handleTranscription(const QString &text, float confidence)
{
    if (!text.isEmpty()) {
        setLastText(text);
        setConfidence(confidence);
        emit sentenceRecognized(text, confidence);
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
        m_audioBuffer.clear();
    }
}

float SpeechRecognizer::calculateVolume(const QByteArray &data)
{
    if (data.isEmpty())
        return 0.0f;

    const int16_t *samples = reinterpret_cast<const int16_t*>(data.constData());
    int sampleCount = data.size() / sizeof(int16_t);

    if (sampleCount == 0)
        return 0.0f;

    // Calculate RMS
    float sum = 0.0f;
    for (int i = 0; i < sampleCount; ++i) {
        float normalized = samples[i] / 32768.0f;
        sum += normalized * normalized;
    }

    float rms = std::sqrt(sum / sampleCount);
    float db = 20.0f * std::log10(std::max(rms, 0.00001f));
    float normalized = (db + 60.0f) / 60.0f;

    return std::clamp(normalized, 0.0f, 1.0f);
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
