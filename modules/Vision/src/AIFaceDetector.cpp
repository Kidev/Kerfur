#include "AIFaceDetector.hpp"
#include <algorithm>
#include <opencv2/opencv.hpp>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QStandardPaths>

AIFaceDetector::AIFaceDetector()
{
    initialize();
}

AIFaceDetector::~AIFaceDetector() = default;

void AIFaceDetector::setModelPath(const QString &modelPath)
{
    m_modelPath = modelPath;
    if (!modelPath.isEmpty())
        initialize();
}

QString AIFaceDetector::getModelPath()
{
    return m_modelPath;
}

bool AIFaceDetector::initialize()
{
    QMutexLocker locker {&m_mutex};

    try {
        // Find the YuNet model file
        if (m_modelPath.isEmpty()) {
            m_lastError = "YuNet model file not found";
            return false;
        }

        qDebug() << "Loading YuNet model from:" << m_modelPath;

        // Create FaceDetectorYN using the same parameters as the demo
        m_detector = cv::FaceDetectorYN::create(m_modelPath.toStdString(),
                                                "", // config (empty for ONNX)
                                                m_inputSize,
                                                DEFAULT_CONF_THRESHOLD,
                                                DEFAULT_NMS_THRESHOLD,
                                                DEFAULT_TOP_K,
                                                DEFAULT_BACKEND_ID,
                                                DEFAULT_TARGET_ID);

        if (m_detector.empty()) {
            m_lastError = "Failed to create FaceDetectorYN";
            return false;
        }

        // Test the detector with a small image to ensure it works
        try {
            cv::Mat testImage {cv::Mat::zeros(320, 320, CV_8UC3)};
            cv::Mat faces;
            m_detector->detect(testImage, faces);
            qDebug() << "YuNet detector test successful";
        } catch (const cv::Exception &e) {
            m_lastError = QString {"Detector test failed: %1"}.arg(e.what());
            qWarning() << m_lastError;
            return false;
        }

        m_initialized = true;
        m_lastError.clear();
        qDebug() << "YuNet Face Detector initialized successfully";
        return true;

    } catch (const cv::Exception &e) {
        m_lastError = QString {"OpenCV error during initialization: %1"}.arg(e.what());
        qWarning() << m_lastError;
        return false;
    } catch (const std::exception &e) {
        m_lastError = QString {"Initialization error: %1"}.arg(e.what());
        qWarning() << m_lastError;
        return false;
    }
}

cv::Mat AIFaceDetector::qImageToCvMat(const QImage &image)
{
    QImage rgbImage {image};
    if (rgbImage.format() != QImage::Format_RGB888) {
        rgbImage = rgbImage.convertToFormat(QImage::Format_RGB888);
    }

    cv::Mat mat {rgbImage.height(),
                 rgbImage.width(),
                 CV_8UC3,
                 const_cast<uchar *>(rgbImage.constBits()),
                 static_cast<size_t>(rgbImage.bytesPerLine())};

    // Convert RGB to BGR (OpenCV format) and clone to ensure data ownership
    cv::Mat bgrMat;
    cv::cvtColor(mat, bgrMat, cv::COLOR_RGB2BGR);
    return bgrMat.clone();
}

void AIFaceDetector::setInputSize(const cv::Size &inputSize)
{
    if (m_inputSize != inputSize && m_detector) {
        m_inputSize = inputSize;
        m_detector->setInputSize(inputSize);
    }
}

std::vector<AIFaceDetector::FaceInfo> AIFaceDetector::detectFaces(const QImage &image,
                                                                  float confidenceThreshold)
{
    if (!m_initialized || image.isNull() || !m_detector) {
        return {};
    }

    QMutexLocker locker {&m_mutex};

    try {
        // Convert QImage to OpenCV Mat
        cv::Mat inputImage {qImageToCvMat(image)};
        if (inputImage.empty()) {
            qWarning() << "Failed to convert image";
            return {};
        }

        // Set the input size to match the image dimensions
        // This allows YuNet to work with any input size
        setInputSize(cv::Size {inputImage.cols, inputImage.rows});

        // Detect faces using YuNet
        cv::Mat faces;
        m_detector->detect(inputImage, faces);

        // Convert results to our FaceInfo format
        std::vector<FaceInfo> faceInfos;

        if (faces.rows > 0) {
            for (int i {0}; i < faces.rows; ++i) {
                const float *data {faces.ptr<float>(i)};

                // YuNet output format: [x1, y1, w, h, score, x_re, y_re, x_le, y_le, x_nt, y_nt, x_rcm, y_rcm, x_lcm, y_lcm]
                // Where: re=right eye, le=left eye, nt=nose tip, rcm=right corner mouth, lcm=left corner mouth
                float x1 {data[0]};
                float y1 {data[1]};
                float w {data[2]};
                float h {data[3]};
                float confidence {data[4]};

                // Skip low confidence detections
                if (confidence < confidenceThreshold) {
                    continue;
                }

                // Validate bounding box
                if (w <= 0 || h <= 0 || x1 < 0 || y1 < 0 || x1 + w > inputImage.cols ||
                    y1 + h > inputImage.rows) {
                    continue;
                }

                FaceInfo faceInfo;
                faceInfo.confidence = confidence;
                faceInfo.bbox = QRectF {x1, y1, w, h};

                // Extract landmarks (5 points: right eye, left eye, nose tip, right mouth corner, left mouth corner)
                if (faces.cols >= 15) { // Ensure we have all landmark data
                    faceInfo.landmarks.reserve(5);
                    faceInfo.landmarks.emplace_back(data[5], data[6]);   // right eye
                    faceInfo.landmarks.emplace_back(data[7], data[8]);   // left eye
                    faceInfo.landmarks.emplace_back(data[9], data[10]);  // nose tip
                    faceInfo.landmarks.emplace_back(data[11], data[12]); // right mouth corner
                    faceInfo.landmarks.emplace_back(data[13], data[14]); // left mouth corner
                }

                faceInfos.push_back(faceInfo);
            }
        }

        // Sort by confidence (highest first)
        std::sort(faceInfos.begin(), faceInfos.end(), [](const FaceInfo &a, const FaceInfo &b) {
            return a.confidence > b.confidence;
        });

        return faceInfos;

    } catch (const cv::Exception &e) {
        qWarning() << "OpenCV detection error:" << e.what();
        return {};
    } catch (const std::exception &e) {
        qWarning() << "Detection error:" << e.what();
        return {};
    }
}
