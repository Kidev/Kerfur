#pragma once

#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <vector>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QMutex>
#include <QRect>
#include <QStandardPaths>

class AIFaceDetector
{
public:
    struct FaceInfo
    {
        QRectF bbox;
        float confidence;
        std::vector<QPointF>
            landmarks; // 5 key points: right eye, left eye, nose tip, right mouth, left mouth
    };

    explicit AIFaceDetector();
    ~AIFaceDetector();

    bool initialize();
    std::vector<FaceInfo> detectFaces(const QImage &image, float confidenceThreshold = 0.6f);
    bool isInitialized() const { return m_initialized; }
    QString getLastError() const { return m_lastError; }
    QString getModelPath();
    void setModelPath(const QString &modelPath);

private:
    cv::Mat qImageToCvMat(const QImage &image);
    void setInputSize(const cv::Size &inputSize);

    cv::Ptr<cv::FaceDetectorYN> m_detector;
    bool m_initialized {false};
    QString m_lastError;
    QMutex m_mutex;
    cv::Size m_inputSize {320, 320};
    QString m_modelPath;

    // YuNet model parameters - lowered confidence for better tilted face detection
    static constexpr float DEFAULT_CONF_THRESHOLD {0.6f};
    static constexpr float DEFAULT_NMS_THRESHOLD {0.3f};
    static constexpr int DEFAULT_TOP_K {5000};
    static constexpr int DEFAULT_BACKEND_ID {0}; // DNN_BACKEND_OPENCV
    static constexpr int DEFAULT_TARGET_ID {0};  // DNN_TARGET_CPU
};
