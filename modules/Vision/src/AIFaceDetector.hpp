#pragma once

#include <memory>
#include <vector>
#include <QImage>
#include <QRect>
#include <QMutex>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

class AIFaceDetector {
public:
    struct FaceInfo {
        QRectF bbox;
        float confidence;
        std::vector<QPointF> landmarks; // 5 key points: eyes, nose, mouth corners
    };

    explicit AIFaceDetector();
    ~AIFaceDetector();

    bool initialize();
    std::vector<FaceInfo> detectFaces(const QImage& image, float confidenceThreshold = 0.7f);
    bool isInitialized() const { return m_initialized; }
    QString getLastError() const { return m_lastError; }

private:
    cv::Mat preprocessImage(const QImage& image, QSize& processSize);
    std::vector<FaceInfo> postprocessResults(const cv::Mat& output,
                                             const QSize& originalSize,
                                             const QSize& processSize,
                                             float confidenceThreshold);

    cv::dnn::Net m_net;
    bool m_initialized{false};
    QString m_lastError;
    QMutex m_mutex;

    // Processing size for performance (scaled down from original)
    static constexpr int PROCESS_WIDTH{640};
    static constexpr int PROCESS_HEIGHT{480};
    static constexpr int INPUT_WIDTH{320};
    static constexpr int INPUT_HEIGHT{320};
};
