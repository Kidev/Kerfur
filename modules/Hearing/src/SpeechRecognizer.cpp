// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "SpeechRecognizer.hpp"

#include "modeldirectory.h"
#include "voiceidentifier.h"

#include <QLoggingCategory>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

Q_LOGGING_CATEGORY(speechLog, "kerfur.hearing.speech")

// The speech entry of assets/models.json. English only, because the wake word
// and the transcript consumers are English; the manifest is what pins the
// checksum and `make models` is what puts the file on disk.
constexpr QLatin1StringView speechModelFile{"ggml-tiny.en.bin"};

// A tenth of a second of audio per callback. The previous value was two
// hundred bytes, which is six milliseconds, and turned every second of
// listening into a hundred and sixty signal emissions and buffer copies.
constexpr int captureBufferMs{100};

QString describeFormat(const QAudioFormat &format)
{
    const char *sample{"?"};
    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8:
        sample = "u8";
        break;
    case QAudioFormat::Int16:
        sample = "s16";
        break;
    case QAudioFormat::Int32:
        sample = "s32";
        break;
    case QAudioFormat::Float:
        sample = "f32";
        break;
    default:
        break;
    }
    return QStringLiteral("%1 Hz, %2 ch, %3")
        .arg(format.sampleRate())
        .arg(format.channelCount())
        .arg(QLatin1StringView{sample});
}

} // namespace

AudioProcessor::AudioProcessor(QObject *parent) : QIODevice{parent}
{
    open(QIODevice::WriteOnly);
}

qint64 AudioProcessor::readData(char *data, qint64 maxlen)
{
    Q_UNUSED(data)
    Q_UNUSED(maxlen)
    return 0;
}

qint64 AudioProcessor::writeData(const char *data, qint64 len)
{
    emit audioDataReady(QByteArray{data, static_cast<qsizetype>(len)});
    return len;
}

SpeechRecognizer::SpeechRecognizer(QObject *parent)
    : QObject{parent}
    , m_whisperEngine{std::make_unique<WhisperEngine>()}
    , m_volumeUpdateTimer{this}
    , m_kerfurResetTimer{this}
    , m_joinTimer{this}
{
    // Not parented to `this`: the engine joins a worker thread in its
    // destructor, and QObject destroys children after the derived part of the
    // parent is already gone. Owned by the unique_ptr instead, which runs
    // first.
    m_modelPath = Kerfur::ModelDirectory::locate(speechModelFile);
    applyModelPath();

    connect(m_whisperEngine.get(),
            &WhisperEngine::transcriptionReady,
            this,
            &SpeechRecognizer::handleTranscription,
            Qt::QueuedConnection);
    connect(m_whisperEngine.get(),
            &WhisperEngine::keywordDetected,
            this,
            &SpeechRecognizer::handleKeywordDetected,
            Qt::QueuedConnection);
    connect(
        m_whisperEngine.get(),
        &WhisperEngine::processingStarted,
        this,
        [this]() { setProcessing(true); },
        Qt::QueuedConnection);
    connect(
        m_whisperEngine.get(),
        &WhisperEngine::processingFinished,
        this,
        [this]() { setProcessing(false); },
        Qt::QueuedConnection);

    // Six hundred milliseconds of silence ends an utterance: long enough to
    // survive the pause in the middle of a sentence, short enough that a reply
    // does not feel late.
    m_segmenter.setSilenceTailMs(600);

    m_volumeUpdateTimer.setInterval(50);
    connect(&m_volumeUpdateTimer, &QTimer::timeout, this, &SpeechRecognizer::updateVolumeLevel);

    m_kerfurResetTimer.setSingleShot(true);
    m_kerfurResetTimer.setInterval(2000);
    connect(&m_kerfurResetTimer, &QTimer::timeout, this, &SpeechRecognizer::resetKerfurDetection);

    // The backstop for the half of an utterance that never arrives. Restarted
    // every time a half lands, so it measures the wait for the missing one
    // rather than the age of the queue.
    m_joinTimer.setSingleShot(true);
    m_joinTimer.setInterval(joinTimeoutMs);
    connect(&m_joinTimer, &QTimer::timeout, this, [this]() { drainPending(true); });

    startVoiceIdentification();
    enumerateAudioDevices();
}

SpeechRecognizer::~SpeechRecognizer()
{
    if (m_enabled) {
        uninitializeAudio();
    }
    m_voiceThread.quit();
    m_voiceThread.wait();
}

void SpeechRecognizer::startVoiceIdentification()
{
    m_voiceIdentifier = new Kerfur::VoiceIdentifier;
    m_voiceIdentifier->moveToThread(&m_voiceThread);

    connect(&m_voiceThread, &QThread::finished, m_voiceIdentifier, &QObject::deleteLater);
    connect(this,
            &SpeechRecognizer::identificationRequested,
            m_voiceIdentifier,
            &Kerfur::VoiceIdentifier::identify);
    connect(m_voiceIdentifier,
            &Kerfur::VoiceIdentifier::identified,
            this,
            &SpeechRecognizer::handleSpeaker);
    connect(m_voiceIdentifier,
            &Kerfur::VoiceIdentifier::ready,
            this,
            [this](bool loaded, const QStringList &voices, const QString &problem) {
                m_voiceIdentityReady = loaded && !voices.isEmpty();
                m_knownVoices = voices;
                emit voicesChanged();
                if (!problem.isEmpty()) {
                    // Not an error string: speaker recognition is an extra, and
                    // a face that cannot name a voice still hears it.
                    qCInfo(speechLog) << problem;
                }
            });

    m_voiceThread.setObjectName(QStringLiteral("kerfur-voices"));
    m_voiceThread.start();
    QMetaObject::invokeMethod(m_voiceIdentifier,
                              &Kerfur::VoiceIdentifier::initialize,
                              Qt::QueuedConnection);
}

void SpeechRecognizer::applyModelPath()
{
    m_modelError.clear();

    if (m_modelPath.isEmpty()) {
        m_modelError = QStringLiteral("Speech recognition model %1 not found. Searched %2. "
                                      "Run `make models` to provision it.")
                           .arg(speechModelFile, Kerfur::ModelDirectory::describeSearchPath());
    } else if (!m_whisperEngine->initialize(m_modelPath)) {
        m_modelError = QStringLiteral("Failed to load speech recognition model %1: %2")
                           .arg(m_modelPath, m_whisperEngine->getLastError());
    }

    if (m_modelError.isEmpty()) {
        qCInfo(speechLog) << "whisper ready with" << m_modelPath;
    } else {
        setErrorString(m_modelError);
        qCWarning(speechLog) << m_modelError;
    }
    emit modelPathChanged();
}

void SpeechRecognizer::enumerateAudioDevices()
{
    m_audioDevices.clear();
    QStringList names;

    const QAudioDevice defaultDevice{QMediaDevices::defaultAudioInput()};
    if (!defaultDevice.isNull()) {
        m_audioDevices.append(defaultDevice);
        names.append(defaultDevice.description().isEmpty()
                         ? QStringLiteral("Default Audio Input")
                         : defaultDevice.description());
        if (m_selectedDevice.isNull()) {
            m_selectedDevice = defaultDevice;
        }
    }

    for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
        if (device.isNull() || device.id() == defaultDevice.id()) {
            continue;
        }
        m_audioDevices.append(device);
        names.append(device.description().isEmpty()
                         ? QStringLiteral("Audio Device %1").arg(m_audioDevices.size())
                         : device.description());
    }

    if (m_audioDevices.isEmpty()) {
        qCWarning(speechLog) << "no audio input devices; is PipeWire or PulseAudio running?";
        names.append(QStringLiteral("System Audio (no devices detected)"));
    }

    setAvailableDevices(names);
    if (!names.isEmpty()) {
        const qsizetype index{m_audioDevices.indexOf(m_selectedDevice)};
        setCurrentDevice(index >= 0 ? names.at(index) : names.first());
    }
}

void SpeechRecognizer::setDevice(const QString &deviceName)
{
    const qsizetype index{m_availableDevices.indexOf(deviceName)};
    if (index < 0 || index >= m_audioDevices.size()) {
        return;
    }
    if (m_audioDevices.at(index) == m_selectedDevice) {
        return;
    }

    m_selectedDevice = m_audioDevices.at(index);
    setCurrentDevice(deviceName);

    if (m_enabled) {
        uninitializeAudio();
        initializeAudio();
    }
}

void SpeechRecognizer::refreshDevices()
{
    const bool wasEnabled{m_enabled};
    if (wasEnabled) {
        uninitializeAudio();
    }
    enumerateAudioDevices();
    if (wasEnabled) {
        initializeAudio();
    }
}

void SpeechRecognizer::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

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
        setErrorString(m_modelError.isEmpty()
                           ? QStringLiteral("Speech recognition engine not initialized")
                           : m_modelError);
        return;
    }

    if (m_selectedDevice.isNull()) {
        enumerateAudioDevices();
    }
    QAudioDevice device{m_selectedDevice};
    if (device.isNull()) {
        device = QMediaDevices::defaultAudioInput();
    }
    if (device.isNull()) {
        setErrorString(QStringLiteral("No audio input device"));
        return;
    }

    // Asked for outright, rather than derived from the device's preferred
    // format by mutation. A QAudioFormat with only its sample rate set has no
    // channel count and no sample format, and is not a format any device
    // supports; the version of this that started from one captured nothing.
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(channelCount);
    format.setSampleFormat(QAudioFormat::Int16);

    if (!device.isFormatSupported(format)) {
        // The conversion path below handles whatever comes back, so this is a
        // fallback and not a failure.
        format = device.preferredFormat();
        qCInfo(speechLog) << "device refused 16 kHz mono s16; using"
                          << describeFormat(format);
    }

    m_actualFormat = format;
    m_segmenter.reset();
    m_whisperEngine->reset();

    m_audioSource = std::make_unique<QAudioSource>(device, format, this);
    m_audioSource->setBufferSize(format.bytesForDuration(captureBufferMs * 1000));

    connect(m_audioSource.get(), &QAudioSource::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::ActiveState) {
            setListening(true);
            if (m_modelError.isEmpty()) {
                setErrorString(QString{});
            }
            m_volumeUpdateTimer.start();
        } else if (state == QAudio::StoppedState) {
            setListening(false);
            if (m_audioSource && m_audioSource->error() != QAudio::NoError) {
                setErrorString(QStringLiteral("Audio capture stopped with an error"));
            }
        }
    });

    m_audioProcessor = std::make_unique<AudioProcessor>(this);
    connect(m_audioProcessor.get(),
            &AudioProcessor::audioDataReady,
            this,
            &SpeechRecognizer::processAudioData);

    m_audioSource->start(m_audioProcessor.get());
    qCInfo(speechLog) << "capturing" << describeFormat(m_actualFormat);
    emit listeningChanged();
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
        disconnect(m_audioProcessor.get(), nullptr, this, nullptr);
        m_audioProcessor->close();
        m_audioProcessor.reset();
    }

    // Whatever was being spoken when the microphone stopped is still worth
    // transcribing; whatever was already queued behind it is not, because it
    // will surface after the user has turned the feature off.
    if (const auto utterance{m_segmenter.flush()}) {
        submitUtterance(*utterance);
    }
    m_segmenter.reset();

    setListening(false);
    setSpeechDetected(false);
    setVolumeLevel(0.0F);
}

void SpeechRecognizer::processAudioData(const QByteArray &data)
{
    if (!m_listening || data.isEmpty()) {
        return;
    }

    m_volumeSample = data;

    const std::vector<float> samples{toMono16k(data)};
    if (samples.empty()) {
        return;
    }

    for (const auto &utterance :
         m_segmenter.append(samples.data(), static_cast<qsizetype>(samples.size()))) {
        submitUtterance(utterance);
    }
    setSpeechDetected(m_segmenter.inSpeech());
}

void SpeechRecognizer::submitUtterance(const Kerfur::VoiceActivitySegmenter::Utterance &utterance)
{
    if (utterance.samples.empty()) {
        return;
    }

    const quint64 sequence{++m_utteranceSequence};
    qCDebug(speechLog) << "utterance" << sequence << utterance.durationMs << "ms of speech";

    // The same audio to both. Transcription and speaker identification want
    // exactly the same segment, and cutting it twice would be two answers to
    // the question of when somebody stopped talking.
    m_whisperEngine->submit(utterance.samples, sequence);
    emit identificationRequested(utterance.samples, sequence);
}

std::vector<float> SpeechRecognizer::toMono16k(const QByteArray &data) const
{
    const int channels{std::max(1, m_actualFormat.channelCount())};
    const int bytesPerSample{m_actualFormat.bytesPerSample()};
    if (bytesPerSample <= 0) {
        return {};
    }

    const qsizetype total{data.size() / bytesPerSample};
    const qsizetype frames{total / channels};
    if (frames <= 0) {
        return {};
    }

    std::vector<float> mono;
    mono.reserve(static_cast<size_t>(frames));

    const auto sampleAt = [&](qsizetype index) -> float {
        switch (m_actualFormat.sampleFormat()) {
        case QAudioFormat::UInt8:
            return (reinterpret_cast<const quint8 *>(data.constData())[index] - 128) / 128.0F;
        case QAudioFormat::Int16:
            return reinterpret_cast<const qint16 *>(data.constData())[index] / 32768.0F;
        case QAudioFormat::Int32:
            return static_cast<float>(
                reinterpret_cast<const qint32 *>(data.constData())[index] / 2147483648.0);
        case QAudioFormat::Float:
            return reinterpret_cast<const float *>(data.constData())[index];
        default:
            return 0.0F;
        }
    };

    // Averaged across however many channels there are, rather than assuming
    // two. A four channel array microphone is a normal thing to plug in.
    for (qsizetype frame{0}; frame < frames; ++frame) {
        float total{0.0F};
        for (int channel{0}; channel < channels; ++channel) {
            total += sampleAt((frame * channels) + channel);
        }
        mono.push_back(total / channels);
    }

    if (m_actualFormat.sampleRate() == sampleRate) {
        return mono;
    }

    // Linear resampling. The models want 16 kHz and speech has almost nothing
    // above 8 kHz, so the aliasing a better filter would remove is not
    // carrying any of the signal.
    const double ratio{static_cast<double>(sampleRate) / m_actualFormat.sampleRate()};
    const auto resampledCount{static_cast<qsizetype>(mono.size() * ratio)};
    std::vector<float> resampled;
    resampled.reserve(static_cast<size_t>(resampledCount));
    for (qsizetype index{0}; index < resampledCount; ++index) {
        const double position{index / ratio};
        const auto lower{static_cast<qsizetype>(position)};
        const double fraction{position - lower};
        if (lower + 1 < static_cast<qsizetype>(mono.size())) {
            resampled.push_back(static_cast<float>((mono[lower] * (1.0 - fraction))
                                                   + (mono[lower + 1] * fraction)));
        } else if (lower < static_cast<qsizetype>(mono.size())) {
            resampled.push_back(mono[lower]);
        }
    }
    return resampled;
}

void SpeechRecognizer::handleTranscription(const QString &text, float confidence, quint64 sequence)
{
    if (text.isEmpty()) {
        return;
    }

    m_lastText = text;
    ++m_utteranceCount;
    m_transcripts.prepend(text);
    while (m_transcripts.size() > transcriptHistory) {
        m_transcripts.removeLast();
    }

    // Emitted unconditionally, with no equality check. Two identical sentences
    // are two things that were said, and the version of this that compared
    // them showed the first one until the wording happened to change.
    emit lastTextChanged();
    setConfidence(confidence);
    emit sentenceRecognized(text, confidence);

    PendingUtterance &pending{pendingFor(sequence)};
    pending.text = text;
    pending.confidence = confidence;
    pending.transcribed = true;
    drainPending(false);
    m_joinTimer.start();
}

void SpeechRecognizer::handleKeywordDetected(const QString &keyword)
{
    Q_UNUSED(keyword)
    setKerfurDetected(true);
    m_kerfurResetTimer.start();
    emit kerfurHeard();
}

void SpeechRecognizer::handleSpeaker(const QString &name,
                                     double similarity,
                                     int priority,
                                     quint64 sequence)
{
    PendingUtterance &pending{pendingFor(sequence)};
    pending.speaker = name;
    pending.priority = priority;
    pending.identified = true;

    // The live readout updates immediately and independently of the join. It
    // answers "who is talking", which is true the moment the voice is
    // recognised; the join answers "who said this sentence", which is not.
    //
    // An utterance too short to identify leaves the previous speaker standing
    // rather than clearing it: nobody left the room, the sentence was short.
    if (similarity >= 0.0
        && (m_speaker != name || !qFuzzyCompare(m_speakerConfidence, similarity)
            || m_speakerPriority != priority)) {
        m_speaker = name;
        m_speakerConfidence = similarity;
        m_speakerPriority = priority;
        emit speakerChanged();
    }

    drainPending(false);
    m_joinTimer.start();
}

SpeechRecognizer::PendingUtterance &SpeechRecognizer::pendingFor(quint64 sequence)
{
    for (PendingUtterance &pending : m_pending) {
        if (pending.sequence == sequence) {
            return pending;
        }
    }
    if (m_pending.size() >= maximumPending) {
        // Something upstream stopped answering. Publish what the oldest has
        // rather than letting the list grow, and say so, because a silent drop
        // here looks exactly like a microphone that stopped working.
        qCWarning(speechLog) << "utterance join is behind; forcing the oldest through";
        drainPending(true);
    }
    m_pending.append(PendingUtterance{sequence, {}, 0.0F, {}, Kerfur::IdentityRegistry::unknownPriority, false, false});
    return m_pending.last();
}

void SpeechRecognizer::drainPending(bool force)
{
    QList<PendingUtterance> ready;
    for (qsizetype index{0}; index < m_pending.size();) {
        const PendingUtterance &pending{m_pending.at(index)};
        const bool complete{pending.transcribed && pending.identified};
        if (!complete && !force) {
            ++index;
            continue;
        }
        // A forced entry with no transcript is not an utterance at all: the
        // speaker network answered about audio whisper found no words in.
        if (pending.transcribed) {
            ready.append(pending);
        }
        m_pending.removeAt(index);
    }

    if (ready.isEmpty()) {
        if (m_pending.isEmpty()) {
            m_joinTimer.stop();
        }
        return;
    }

    // Rank first, arrival second. Everybody is heard; the highest ranked is
    // answered first.
    std::stable_sort(ready.begin(),
                     ready.end(),
                     [](const PendingUtterance &left, const PendingUtterance &right) {
                         if (left.priority != right.priority) {
                             return left.priority < right.priority;
                         }
                         return left.sequence < right.sequence;
                     });

    for (const PendingUtterance &pending : std::as_const(ready)) {
        const bool authorised{pending.priority <= m_commandPriorityCeiling};
        qCDebug(speechLog) << "utterance" << pending.sequence << "from"
                           << (pending.speaker.isEmpty() ? QStringLiteral("a stranger")
                                                         : pending.speaker)
                           << "rank" << pending.priority
                           << (authorised ? "may act" : "may not act");
        emit utteranceReady(pending.text,
                            pending.confidence,
                            pending.speaker,
                            pending.priority,
                            authorised);
    }

    if (m_pending.isEmpty()) {
        m_joinTimer.stop();
    }
}

void SpeechRecognizer::setCommandPriorityCeiling(int ceiling)
{
    const int clamped{std::max(0, ceiling)};
    if (m_commandPriorityCeiling == clamped) {
        return;
    }
    m_commandPriorityCeiling = clamped;
    emit commandPriorityCeilingChanged();
    emit speakerChanged();
}

void SpeechRecognizer::updateVolumeLevel()
{
    if (!m_volumeSample.isEmpty()) {
        setVolumeLevel(calculateVolume(m_volumeSample));
        m_volumeSample.clear();
    } else {
        setVolumeLevel(m_volumeLevel * 0.95F);
    }
}

float SpeechRecognizer::calculateVolume(const QByteArray &data) const
{
    const std::vector<float> samples{toMono16k(data)};
    float peak{0.0F};
    for (const float sample : samples) {
        peak = std::max(peak, std::abs(sample));
    }
    return std::clamp(peak, 0.0F, 1.0F);
}

QString SpeechRecognizer::captureFormat() const
{
    if (!m_listening) {
        return QStringLiteral("not capturing");
    }
    return describeFormat(m_actualFormat);
}

void SpeechRecognizer::resetKerfurDetection()
{
    setKerfurDetected(false);
}

void SpeechRecognizer::setListening(bool listening)
{
    if (m_listening == listening) {
        return;
    }
    m_listening = listening;
    emit listeningChanged();
}

void SpeechRecognizer::setConfidence(float confidence)
{
    if (qFuzzyCompare(m_confidence, confidence)) {
        return;
    }
    m_confidence = confidence;
    emit confidenceChanged();
}

void SpeechRecognizer::setErrorString(const QString &error)
{
    if (m_errorString == error) {
        return;
    }
    m_errorString = error;
    emit errorStringChanged();
}

void SpeechRecognizer::setKerfurDetected(bool detected)
{
    if (m_kerfurDetected == detected) {
        return;
    }
    m_kerfurDetected = detected;
    emit kerfurDetectedChanged();
}

void SpeechRecognizer::setVolumeLevel(float level)
{
    if (qFuzzyCompare(m_volumeLevel, level)) {
        return;
    }
    m_volumeLevel = level;
    emit volumeLevelChanged();
}

void SpeechRecognizer::setProcessing(bool processing)
{
    if (m_processing == processing) {
        return;
    }
    m_processing = processing;
    emit processingChanged();
}

void SpeechRecognizer::setSpeechDetected(bool detected)
{
    if (m_speechDetected == detected) {
        return;
    }
    m_speechDetected = detected;
    emit speechDetectedChanged();
}

void SpeechRecognizer::setAvailableDevices(const QStringList &devices)
{
    if (m_availableDevices == devices) {
        return;
    }
    m_availableDevices = devices;
    emit availableDevicesChanged();
}

void SpeechRecognizer::setCurrentDevice(const QString &device)
{
    if (m_currentDevice == device) {
        return;
    }
    m_currentDevice = device;
    emit currentDeviceChanged();
}
