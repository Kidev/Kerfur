// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "WhisperEngine.hpp"
#include "identityregistry.h"
#include "voiceactivity.h"

#include <memory>
#include <vector>

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QIODevice>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QThread>
#include <QTimer>

namespace Kerfur {
class VoiceIdentifier;
}

/// The sink QAudioSource writes captured audio into.
class AudioProcessor : public QIODevice
{
    Q_OBJECT

public:
    explicit AudioProcessor(QObject *parent = nullptr);

signals:
    void audioDataReady(const QByteArray &data);

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;
};

/// The microphone, and what Kerfur makes of what it hears.
///
/// One instance. It owns a capture device, a Whisper context and a speaker
/// network, and there was a period where two of these existed at once because
/// both Face.qml and ControlPanel.qml declared one: two microphones, two model
/// loads and two transcription loops competing over the same words. The panel
/// is handed the instance the face owns.
class SpeechRecognizer : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(QString lastText READ lastText NOTIFY lastTextChanged)

    /// How many utterances have been transcribed. The text alone is not enough
    /// to drive a view: saying the same word twice produces the same string,
    /// and a property that only notifies on change would show the first one for
    /// ever. That is exactly what it used to do.
    Q_PROPERTY(int utteranceCount READ utteranceCount NOTIFY lastTextChanged)

    /// The last few utterances, newest first, so the panel shows a
    /// conversation rather than a single line that keeps being overwritten.
    Q_PROPERTY(QStringList transcripts READ transcripts NOTIFY lastTextChanged)

    Q_PROPERTY(float confidence READ confidence NOTIFY confidenceChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(bool kerfurDetected READ kerfurDetected NOTIFY kerfurDetectedChanged)
    Q_PROPERTY(float volumeLevel READ volumeLevel NOTIFY volumeLevelChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)

    /// True while somebody is actually talking, as the gate sees it. The volume
    /// meter says how loud the room is; this says whether any of it is speech.
    Q_PROPERTY(bool speechDetected READ speechDetected NOTIFY speechDetectedChanged)
    Q_PROPERTY(float noiseFloor READ noiseFloor NOTIFY volumeLevelChanged)

    Q_PROPERTY(QString speaker READ speaker NOTIFY speakerChanged)
    Q_PROPERTY(qreal speakerConfidence READ speakerConfidence NOTIFY speakerChanged)

    /// The rank of whoever is speaking, zero being the top. A stranger reports
    /// `Kerfur::IdentityRegistry::unknownPriority`.
    Q_PROPERTY(int speakerPriority READ speakerPriority NOTIFY speakerChanged)

    /// What `identities/ids.json` calls whoever is speaking -- "owner",
    /// "trusted" -- or the rank spelled out for a manifest that gives them no
    /// title. Empty for a stranger. This is what the panel shows; the rank
    /// beside it is what the code decides with.
    Q_PROPERTY(QString speakerTitle READ speakerTitle NOTIFY speakerChanged)

    /// Whether the last utterance came from somebody allowed to give orders.
    ///
    /// Kerfur listens to everybody and obeys one person. Transcription,
    /// display and speaker identification run for every voice in the room;
    /// this is the single bit that decides whether what was heard is also
    /// acted on, and it is deliberately a property rather than a filter
    /// upstream so that the panel can show a refused command rather than
    /// silently discarding it.
    Q_PROPERTY(bool commandAuthority READ commandAuthority NOTIFY speakerChanged)

    /// The rank at or below which an utterance may act. Zero, and settable
    /// only from C++ and the panel: raising it is a decision about who Kerfur
    /// obeys, not a preference.
    Q_PROPERTY(int commandPriorityCeiling READ commandPriorityCeiling WRITE
                   setCommandPriorityCeiling NOTIFY commandPriorityCeilingChanged)
    Q_PROPERTY(QStringList knownVoices READ knownVoices NOTIFY voicesChanged)
    Q_PROPERTY(bool voiceIdentityReady READ voiceIdentityReady NOTIFY voicesChanged)

    /// Length of the last utterance heard, kept so that it can be enrolled as
    /// somebody's voice, and the length the speaker network needs before a
    /// recording identifies anybody.
    Q_PROPERTY(qreal lastUtteranceSeconds READ lastUtteranceSeconds NOTIFY lastUtteranceChanged)
    Q_PROPERTY(qreal minimumVoiceSeconds READ minimumVoiceSeconds CONSTANT)

    Q_PROPERTY(QStringList availableDevices READ availableDevices NOTIFY availableDevicesChanged)
    Q_PROPERTY(QString currentDevice READ currentDevice NOTIFY currentDeviceChanged)
    Q_PROPERTY(QString captureFormat READ captureFormat NOTIFY listeningChanged)
    Q_PROPERTY(QString modelPath READ modelPath NOTIFY modelPathChanged)

public:
    explicit SpeechRecognizer(QObject *parent = nullptr);
    ~SpeechRecognizer() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    bool listening() const { return m_listening; }
    QString lastText() const { return m_lastText; }
    int utteranceCount() const { return m_utteranceCount; }
    QStringList transcripts() const { return m_transcripts; }
    float confidence() const { return m_confidence; }
    QString errorString() const { return m_errorString; }
    bool kerfurDetected() const { return m_kerfurDetected; }
    float volumeLevel() const { return m_volumeLevel; }
    bool processing() const { return m_processing; }
    bool speechDetected() const { return m_speechDetected; }
    float noiseFloor() const { return m_segmenter.noiseFloor(); }
    QString speaker() const { return m_speaker; }
    qreal speakerConfidence() const { return m_speakerConfidence; }
    int speakerPriority() const { return m_speakerPriority; }
    QString speakerTitle() const { return m_speakerTitle; }
    bool commandAuthority() const { return m_speakerPriority <= m_commandPriorityCeiling; }
    int commandPriorityCeiling() const { return m_commandPriorityCeiling; }
    void setCommandPriorityCeiling(int ceiling);
    QStringList knownVoices() const { return m_knownVoices; }
    bool voiceIdentityReady() const { return m_voiceIdentityReady; }
    qreal lastUtteranceSeconds() const;
    qreal minimumVoiceSeconds() const;

    /// Stages the last utterance with `Identities` as the voice of whoever is
    /// enrolled next. Refused, with the reason on `Identities.problem`, when it
    /// is too short to identify anybody by.
    Q_INVOKABLE bool stageLastUtterance();
    QStringList availableDevices() const { return m_availableDevices; }
    QString currentDevice() const { return m_currentDevice; }
    QString captureFormat() const;
    QString modelPath() const { return m_modelPath; }

public slots:
    void setDevice(const QString &deviceName);
    void refreshDevices();

signals:
    void enabledChanged();
    void listeningChanged();
    void lastTextChanged();
    void confidenceChanged();
    void errorStringChanged();
    void kerfurDetectedChanged();
    void volumeLevelChanged();
    void processingChanged();
    void speechDetectedChanged();
    void speakerChanged();
    void voicesChanged();
    void availableDevicesChanged();
    void currentDeviceChanged();
    void modelPathChanged();
    void lastUtteranceChanged();

    void kerfurHeard();
    void sentenceRecognized(const QString &text, float confidence);
    void commandPriorityCeilingChanged();

    /// One complete utterance, with the transcript and the speaker joined.
    ///
    /// This is the signal the skill layer listens to and `sentenceRecognized`
    /// is not, because a command needs to know who gave it. The two halves are
    /// produced by two threads that finish in their own time, so they are
    /// matched by the sequence number they were submitted under rather than by
    /// the order they came back in.
    ///
    /// `authorised` is `priority <= commandPriorityCeiling`, precomputed here
    /// so that every receiver applies the same rule.
    void utteranceReady(const QString &text,
                        float confidence,
                        const QString &speaker,
                        int priority,
                        bool authorised);

    /// To the voice identification thread.
    void identificationRequested(const std::vector<float> &samples, quint64 sequence);

private slots:
    void initializeAudio();
    void uninitializeAudio();
    void processAudioData(const QByteArray &data);
    void handleTranscription(const QString &text, float confidence, quint64 sequence);
    void handleKeywordDetected(const QString &keyword);
    void handleSpeaker(const QString &name,
                       double similarity,
                       int priority,
                       const QString &title,
                       quint64 sequence);
    void updateVolumeLevel();
    void resetKerfurDetection();

private:
    void startVoiceIdentification();
    void enumerateAudioDevices();
    void applyModelPath();
    void submitUtterance(const Kerfur::VoiceActivitySegmenter::Utterance &utterance);

    /// One utterance in flight, waiting for both of its answers.
    struct PendingUtterance
    {
        quint64 sequence{0};
        QString text;
        float confidence{0.0F};
        QString speaker;
        int priority{Kerfur::IdentityRegistry::unknownPriority};
        bool transcribed{false};
        bool identified{false};
    };

    PendingUtterance &pendingFor(quint64 sequence);

    /// Publishes every utterance both threads have answered for, and every one
    /// whose second answer never came.
    ///
    /// Ordered by rank and only then by arrival. When two people talk over each
    /// other and both utterances complete in the same pass, the higher ranked
    /// one is dispatched first; a rank does not decide whether somebody is
    /// heard, only who is answered first.
    void drainPending(bool force);

    /// Turns one capture buffer into 16 kHz mono floats, whatever the device
    /// handed over. The device is asked for that format first and usually
    /// agrees; this is the path for the ones that do not.
    std::vector<float> toMono16k(const QByteArray &data) const;
    float calculateVolume(const QByteArray &data) const;

    void setListening(bool listening);
    void setConfidence(float confidence);
    void setErrorString(const QString &error);
    void setKerfurDetected(bool detected);
    void setVolumeLevel(float level);
    void setProcessing(bool processing);
    void setSpeechDetected(bool detected);
    void setAvailableDevices(const QStringList &devices);
    void setCurrentDevice(const QString &device);

    std::unique_ptr<WhisperEngine> m_whisperEngine;
    std::unique_ptr<QAudioSource> m_audioSource;
    std::unique_ptr<AudioProcessor> m_audioProcessor;

    QThread m_voiceThread;
    Kerfur::VoiceIdentifier *m_voiceIdentifier{nullptr};

    Kerfur::VoiceActivitySegmenter m_segmenter{sampleRate};

    QAudioFormat m_actualFormat;
    QList<QAudioDevice> m_audioDevices;
    QAudioDevice m_selectedDevice;

    bool m_enabled{false};
    bool m_listening{false};
    QString m_lastText;
    int m_utteranceCount{0};
    QStringList m_transcripts;
    float m_confidence{0.0F};
    QString m_errorString;
    QString m_modelError;
    bool m_kerfurDetected{false};
    float m_volumeLevel{0.0F};
    bool m_processing{false};
    bool m_speechDetected{false};
    QString m_speaker;
    qreal m_speakerConfidence{0.0};
    int m_speakerPriority{Kerfur::IdentityRegistry::unknownPriority};
    QString m_speakerTitle;
    int m_commandPriorityCeiling{0};
    QList<PendingUtterance> m_pending;
    QStringList m_knownVoices;
    bool m_voiceIdentityReady{false};
    QStringList m_availableDevices;
    QString m_currentDevice;
    QString m_modelPath;

    quint64 m_utteranceSequence{0};

    /// The samples of the last utterance, 16 kHz mono. Replaced by the next
    /// one, so only ever one sentence is held.
    std::vector<float> m_lastUtterance;

    static constexpr int sampleRate{16000};
    static constexpr int channelCount{1};

    /// How many past utterances the panel keeps. Enough to see a sentence in
    /// context, few enough that it is a status display and not a log.
    static constexpr int transcriptHistory{6};

    /// How long a completed transcript waits for its speaker before it is
    /// published as anonymous. The speaker network is tens of milliseconds and
    /// the decode is hundreds, so in practice the identity is always there
    /// first; this exists for the case where the utterance was too short to
    /// embed and no answer is coming at all.
    static constexpr int joinTimeoutMs{700};

    /// Utterances that may be in flight at once. Past this the oldest is
    /// published with whatever it has, because something upstream is stuck and
    /// a growing list of half answers is not a recovery.
    static constexpr qsizetype maximumPending{8};

    QTimer m_volumeUpdateTimer;
    QTimer m_kerfurResetTimer;
    QTimer m_joinTimer;

    QByteArray m_volumeSample;
};
