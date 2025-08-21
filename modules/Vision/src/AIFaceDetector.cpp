#include "AIFaceDetector.hpp"
#include <QDebug>
#include <QFile>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <algorithm>
#include <opencv2/opencv.hpp>

AIFaceDetector::AIFaceDetector() {
    initialize();
}

AIFaceDetector::~AIFaceDetector() = default;

bool AIFaceDetector::initialize() {
    QMutexLocker locker{&m_mutex};

    try {
        QFile modelFile{":/models/yunet_320x320.onnx"};
        if (!modelFile.open(QIODevice::ReadOnly)) {
            m_lastError = "Failed to open face detection model";
            return false;
        }

        QTemporaryFile tempModel{"XXXXXX-yunet.onnx"};
        tempModel.setAutoRemove(true);
        if (!tempModel.open()) {
            m_lastError = "Failed to create temporary model file";
            return false;
        }

        tempModel.write(modelFile.readAll());
        tempModel.close();
        modelFile.close();

        m_net = cv::dnn::readNet(tempModel.fileName().toStdString());

        if (m_net.empty()) {
            m_lastError = "Failed to load neural network model";
            return false;
        }

        try {
            m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
            m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
            qDebug() << "AI Face Detector using CUDA backend";
        } catch (...) {
            try {
                m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                m_net.setPreferableTarget(cv::dnn::DNN_TARGET_OPENCL);
                qDebug() << "AI Face Detector using OpenCL backend";
            } catch (...) {
                m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                qDebug() << "AI Face Detector using CPU backend";
            }
        }

        m_initialized = true;
        m_lastError.clear();

        qDebug() << "AI Face Detector initialized successfully";

        std::vector<cv::dnn::MatShape> inLayerShapes;
        std::vector<cv::dnn::MatShape> outLayerShapes;
        m_net.getLayerShapes(cv::dnn::MatShape{}, 0, inLayerShapes, outLayerShapes);

        if (!inLayerShapes.empty()) {
            qDebug() << "Expected input shape: ["
            << inLayerShapes[0][0] << ", "
            << inLayerShapes[0][1] << ", "
            << inLayerShapes[0][2] << ", "
            << inLayerShapes[0][3] << "]";
        }

        return true;

    } catch (const cv::Exception& e) {
        m_lastError = QString{"OpenCV error: %1"}.arg(e.what());
        qWarning() << m_lastError;
        return false;
    } catch (const std::exception& e) {
        m_lastError = QString{"Initialization error: %1"}.arg(e.what());
        qWarning() << m_lastError;
        return false;
    }
}

cv::Mat AIFaceDetector::preprocessImage(const QImage& image, QSize& processSize) {
    cv::Mat mat{};

    QImage rgbImage{image};
    if (rgbImage.format() != QImage::Format_RGB888) {
        rgbImage = rgbImage.convertToFormat(QImage::Format_RGB888);
    }

    mat = cv::Mat{rgbImage.height(),
        rgbImage.width(),
        CV_8UC3,
        const_cast<uchar*>(rgbImage.constBits()),
        static_cast<size_t>(rgbImage.bytesPerLine())}.clone();

        // Convert RGB to BGR (OpenCV format)
        cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);

        // Scale image to processing size for performance
        cv::Mat scaledMat{};
        if (mat.cols > PROCESS_WIDTH || mat.rows > PROCESS_HEIGHT) {
            float scale{std::min(static_cast<float>(PROCESS_WIDTH) / mat.cols,
                static_cast<float>(PROCESS_HEIGHT) / mat.rows)};
                cv::resize(mat, scaledMat, cv::Size{}, scale, scale);
        } else {
            scaledMat = mat;
        }

        processSize = QSize{scaledMat.cols, scaledMat.rows};

        return scaledMat;
}

std::vector<AIFaceDetector::FaceInfo> AIFaceDetector::detectFaces(const QImage& image, float confidenceThreshold) {
    if (!m_initialized || image.isNull()) {
        return {};
    }

    QMutexLocker locker{&m_mutex};

    try {
        // Preprocess image
        QSize processSize{};
        cv::Mat processedImage{preprocessImage(image, processSize)};

        // Create blob from image
        // YuNet expects 320x320 input
        cv::Mat blob{cv::dnn::blobFromImage(processedImage,
            1.0,  // scale factor
            cv::Size{INPUT_WIDTH, INPUT_HEIGHT},  // input size
            cv::Scalar{104, 177, 123},  // mean subtraction values
            false,  // swapRB
            false)}; // crop

            // Set input to the network
            m_net.setInput(blob);

            // Run forward pass
            cv::Mat output{m_net.forward()};

            // Postprocess results
            return postprocessResults(output, image.size(), processSize, confidenceThreshold);

    } catch (const cv::Exception& e) {
        qWarning() << "OpenCV detection error:" << e.what();
        return {};
    } catch (const std::exception& e) {
        qWarning() << "Detection error:" << e.what();
        return {};
    }
}

std::vector<AIFaceDetector::FaceInfo> AIFaceDetector::postprocessResults(
    const cv::Mat& output,
    const QSize& originalSize,
    const QSize& processSize,
    float confidenceThreshold) {

    std::vector<FaceInfo> faces{};

    // YuNet output format: [1, num_faces, 15]
    // Each detection: [x1, y1, x2, y2, x_le, y_le, x_re, y_re, x_n, y_n, x_lm, y_lm, x_rm, y_rm, score]
    // Where le=left_eye, re=right_eye, n=nose, lm=left_mouth, rm=right_mouth

    float scaleX{static_cast<float>(originalSize.width()) / processSize.width()};
    float scaleY{static_cast<float>(originalSize.height()) / processSize.height()};

    // Handle different output formats
    if (output.dims == 3) {
        // Format: [1, num_detections, 15]
        int numDetections{output.size[1]};
        int numFields{output.size[2]};

        for (int i{0}; i < numDetections; ++i) {
            const float* data{output.ptr<float>(0, i)};  // Fixed: use const float*

            // Get confidence score (last element)
            float confidence{data[numFields - 1]};
            if (confidence < confidenceThreshold) {
                continue;
            }

            FaceInfo face{};
            face.confidence = confidence;

            // Get bounding box (first 4 elements are x1, y1, x2, y2)
            float x1{data[0] * scaleX};
            float y1{data[1] * scaleY};
            float x2{data[2] * scaleX};
            float y2{data[3] * scaleY};

            face.bbox = QRectF{x1, y1, x2 - x1, y2 - y1};

            // Get landmarks (5 key points)
            for (int j{0}; j < 5; ++j) {
                float lx{data[4 + j * 2] * scaleX};
                float ly{data[5 + j * 2] * scaleY};
                face.landmarks.push_back(QPointF{lx, ly});
            }

            faces.push_back(face);
        }
    } else if (output.dims == 2) {
        // Format: [num_detections, 15]
        int numDetections{output.rows};

        for (int i{0}; i < numDetections; ++i) {
            const float* data{output.ptr<float>(i)};  // Already const float* here

            float confidence{data[14]};
            if (confidence < confidenceThreshold) {
                continue;
            }

            FaceInfo face{};
            face.confidence = confidence;

            float x1{data[0] * scaleX};
            float y1{data[1] * scaleY};
            float x2{data[2] * scaleX};
            float y2{data[3] * scaleY};

            face.bbox = QRectF{x1, y1, x2 - x1, y2 - y1};

            for (int j{0}; j < 5; ++j) {
                float lx{data[4 + j * 2] * scaleX};
                float ly{data[5 + j * 2] * scaleY};
                face.landmarks.push_back(QPointF{lx, ly});
            }

            faces.push_back(face);
        }
    }

    // Sort by confidence
    std::sort(faces.begin(), faces.end(),
              [](const FaceInfo& a, const FaceInfo& b) {
                  return a.confidence > b.confidence;
              });

    // Non-maximum suppression
    std::vector<FaceInfo> filteredFaces{};
    std::vector<bool> suppressed(faces.size(), false);

    for (size_t i{0}; i < faces.size(); ++i) {
        if (suppressed[i]) continue;

        filteredFaces.push_back(faces[i]);

        for (size_t j{i + 1}; j < faces.size(); ++j) {
            if (suppressed[j]) continue;

            // Calculate IoU
            QRectF intersection{faces[i].bbox.intersected(faces[j].bbox)};
            float intersectionArea{static_cast<float>(intersection.width() * intersection.height())};
            float unionArea{static_cast<float>(faces[i].bbox.width() * faces[i].bbox.height() +
                faces[j].bbox.width() * faces[j].bbox.height() -
                intersectionArea)};

                float iou{intersectionArea / unionArea};
                if (iou > 0.5f) {
                    suppressed[j] = true;
                }
        }
    }

    return filteredFaces;
    }
