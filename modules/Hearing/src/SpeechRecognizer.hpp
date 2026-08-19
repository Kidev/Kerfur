#pragma once

#include "WhisperEngine.hpp"
#include <memory>
#include <QAudioFormat>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>

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

class SpeechRecognizer : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(QString lastText READ lastText NOTIFY lastTextChanged)
    Q_PROPERTY(float confidence READ confidence NOTIFY confidenceChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(bool kerfurDetected READ kerfurDetected NOTIFY kerfurDetectedChanged)
    Q_PROPERTY(float volumeLevel READ volumeLevel NOTIFY volumeLevelChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
    Q_PROPERTY(QStringList availableDevices READ availableDevices NOTIFY availableDevicesChanged)
    Q_PROPERTY(QString currentDevice READ currentDevice NOTIFY currentDeviceChanged)
    Q_PROPERTY(QString modelPath READ modelPath WRITE setModelPath NOTIFY modelPathChanged)

public:
    explicit SpeechRecognizer(QObject *parent = nullptr);
    ~SpeechRecognizer();

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    bool listening() const { return m_listening; }
    QString lastText() const { return m_lastText; }
    float confidence() const { return m_confidence; }
    QString errorString() const { return m_errorString; }
    bool kerfurDetected() const { return m_kerfurDetected; }
    float volumeLevel() const { return m_volumeLevel; }
    bool processing() const { return m_processing; }
    QStringList availableDevices() const { return m_availableDevices; }
    QString currentDevice() const { return m_currentDevice; }
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
    void availableDevicesChanged();
    void currentDeviceChanged();

    // Action signals
    void kerfurHeard();
    void sentenceRecognized(const QString &text, float confidence);

private slots:
    void initializeAudio();
    void uninitializeAudio();
    void processAudioData(const QByteArray &data);
    void handleTranscription(const QString &text, float confidence);
    void handleKeywordDetected(const QString &keyword);
    void updateVolumeLevel();
    void resetKerfurDetection();
    void setProcessing(bool processing);

private:
    void enumerateAudioDevices();
    void setListening(bool listening);
    void setLastText(const QString &text);
    void setConfidence(float confidence);
    void setErrorString(const QString &error);
    void setKerfurDetected(bool detected);
    void setVolumeLevel(float level);
    void setAvailableDevices(const QStringList &devices);
    void setCurrentDevice(const QString &device);
    void setModelPath(const QString &modelPath);

    // Convert audio to float for Whisper
    std::vector<float> convertAudioToFloat(const QByteArray &data);
    float calculateVolume(const QByteArray &data);

    // Core components
    std::unique_ptr<WhisperEngine> m_whisperEngine;
    std::unique_ptr<QAudioSource> m_audioSource;
    std::unique_ptr<AudioProcessor> m_audioProcessor;

    // Audio format tracking
    QAudioFormat m_actualFormat;
    QList<QAudioDevice> m_audioDevices;
    QAudioDevice m_selectedDevice;

    // State
    bool m_enabled{false};
    bool m_listening{false};
    QString m_lastText;
    float m_confidence{0.0f};
    QString m_errorString;
    bool m_kerfurDetected{false};
    float m_volumeLevel{0.0f};
    bool m_processing{false};
    QStringList m_availableDevices;
    QString m_currentDevice;
    QString m_modelPath;

    // Configuration
    static constexpr int SAMPLE_RATE{16000};
    static constexpr int CHANNEL_COUNT{1};

    // Timers
    QTimer m_volumeUpdateTimer;
    QTimer m_kerfurResetTimer;

    // Audio buffer for processing
    QByteArray m_audioBuffer;
    static constexpr int BUFFER_SIZE{16384};
};
