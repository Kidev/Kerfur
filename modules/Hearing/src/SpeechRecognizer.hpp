// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "WhisperEngine.hpp"
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
    Q_PROPERTY(QStringList knownVoices READ knownVoices NOTIFY voicesChanged)
    Q_PROPERTY(bool voiceIdentityReady READ voiceIdentityReady NOTIFY voicesChanged)

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
    QStringList knownVoices() const { return m_knownVoices; }
    bool voiceIdentityReady() const { return m_voiceIdentityReady; }
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

    void kerfurHeard();
    void sentenceRecognized(const QString &text, float confidence);

    /// To the voice identification thread.
    void identificationRequested(const std::vector<float> &samples, quint64 sequence);

private slots:
    void initializeAudio();
    void uninitializeAudio();
    void processAudioData(const QByteArray &data);
    void handleTranscription(const QString &text, float confidence);
    void handleKeywordDetected(const QString &keyword);
    void handleSpeaker(const QString &name, double similarity, quint64 sequence);
    void updateVolumeLevel();
    void resetKerfurDetection();

private:
    void startVoiceIdentification();
    void enumerateAudioDevices();
    void applyModelPath();
    void submitUtterance(const Kerfur::VoiceActivitySegmenter::Utterance &utterance);

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
    QStringList m_knownVoices;
    bool m_voiceIdentityReady{false};
    QStringList m_availableDevices;
    QString m_currentDevice;
    QString m_modelPath;

    quint64 m_utteranceSequence{0};

    static constexpr int sampleRate{16000};
    static constexpr int channelCount{1};

    /// How many past utterances the panel keeps. Enough to see a sentence in
    /// context, few enough that it is a status display and not a log.
    static constexpr int transcriptHistory{6};

    QTimer m_volumeUpdateTimer;
    QTimer m_kerfurResetTimer;

    QByteArray m_volumeSample;
};
