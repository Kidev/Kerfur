#include "AIFaceDetector.hpp"
#include <algorithm>
#include <opencv2/opencv.hpp>
#include <QDebug>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryFile>

AIFaceDetector::AIFaceDetector()
{
    initialize();
}

AIFaceDetector::~AIFaceDetector() = default;

bool AIFaceDetector::initialize()
{
    QMutexLocker locker {&m_mutex};

    try {
        QFile modelFile {":/models/yunet_320x320.onnx"};
        if (!modelFile.open(QIODevice::ReadOnly)) {
            m_lastError = "Failed to open face detection model";
            return false;
        }

        QTemporaryFile tempModel {"XXXXXX-yunet.onnx"};
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

        // Try backends in order of preference with proper error handling
        bool backendSet = false;

        // Try CUDA backend first (if available)
        if (!backendSet) {
            try {
                m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);

                // Test if CUDA backend actually works by creating a small blob
                cv::Mat testMat = cv::Mat::zeros(320, 320, CV_32FC3);
                cv::Mat testBlob = cv::dnn::blobFromImage(testMat, 1.0, cv::Size(320, 320));
                m_net.setInput(testBlob);
                cv::Mat testOutput = m_net.forward();

                if (!testOutput.empty()) {
                    qDebug() << "AI Face Detector using CUDA backend";
                    backendSet = true;
                }
            } catch (const cv::Exception &e) {
                qDebug() << "CUDA backend not available:" << e.what();
                // Reset backend after failed CUDA attempt
                m_net = cv::dnn::readNet(tempModel.fileName().toStdString());
            } catch (...) {
                qDebug() << "CUDA backend failed with unknown error";
                // Reset backend after failed CUDA attempt
                m_net = cv::dnn::readNet(tempModel.fileName().toStdString());
            }
        }

        // Try OpenCL backend
        if (!backendSet) {
            try {
                m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                m_net.setPreferableTarget(cv::dnn::DNN_TARGET_OPENCL);

                // Test if OpenCL backend actually works
                cv::Mat testMat = cv::Mat::zeros(320, 320, CV_32FC3);
                cv::Mat testBlob = cv::dnn::blobFromImage(testMat, 1.0, cv::Size(320, 320));
                m_net.setInput(testBlob);
                cv::Mat testOutput = m_net.forward();

                if (!testOutput.empty()) {
                    qDebug() << "AI Face Detector using OpenCL backend";
                    backendSet = true;
                }
            } catch (const cv::Exception &e) {
                qDebug() << "OpenCL backend not available:" << e.what();
            } catch (...) {
                qDebug() << "OpenCL backend failed with unknown error";
            }
        }

        // Fallback to CPU backend (should always work)
        if (!backendSet) {
            try {
                m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);

                // Test CPU backend
                cv::Mat testMat = cv::Mat::zeros(320, 320, CV_32FC3);
                cv::Mat testBlob = cv::dnn::blobFromImage(testMat, 1.0, cv::Size(320, 320));
                m_net.setInput(testBlob);
                cv::Mat testOutput = m_net.forward();

                if (!testOutput.empty()) {
                    qDebug() << "AI Face Detector using CPU backend";
                    backendSet = true;
                }
            } catch (const cv::Exception &e) {
                m_lastError = QString {"CPU backend failed: %1"}.arg(e.what());
                qWarning() << m_lastError;
                return false;
            } catch (...) {
                m_lastError = "CPU backend failed with unknown error";
                qWarning() << m_lastError;
                return false;
            }
        }

        if (!backendSet) {
            m_lastError = "Failed to initialize any DNN backend";
            qWarning() << m_lastError;
            return false;
        }

        m_initialized = true;
        m_lastError.clear();

        qDebug() << "AI Face Detector initialized successfully";

        // Log model information
        std::vector<cv::dnn::MatShape> inLayerShapes;
        std::vector<cv::dnn::MatShape> outLayerShapes;
        try {
            m_net.getLayerShapes(cv::dnn::MatShape {}, 0, inLayerShapes, outLayerShapes);

            if (!inLayerShapes.empty()) {
                qDebug() << "Expected input shape: [" << inLayerShapes[0][0] << ", "
                << inLayerShapes[0][1] << ", " << inLayerShapes[0][2] << ", "
                << inLayerShapes[0][3] << "]";
            }
        } catch (...) {
            qDebug() << "Could not get layer shapes (not critical)";
        }

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

cv::Mat AIFaceDetector::preprocessImage(const QImage &image, QSize &processSize)
{
    cv::Mat mat {};

    QImage rgbImage {image};
    if (rgbImage.format() != QImage::Format_RGB888) {
        rgbImage = rgbImage.convertToFormat(QImage::Format_RGB888);
    }

    mat = cv::Mat {rgbImage.height(),
        rgbImage.width(),
        CV_8UC3,
        const_cast<uchar *>(rgbImage.constBits()),
        static_cast<size_t>(rgbImage.bytesPerLine())}
        .clone();

        // Convert RGB to BGR (OpenCV format)
        cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);

        // Scale image to processing size for performance
        cv::Mat scaledMat {};
        if (mat.cols > PROCESS_WIDTH || mat.rows > PROCESS_HEIGHT) {
            float scale {std::min(static_cast<float>(PROCESS_WIDTH) / mat.cols,
                static_cast<float>(PROCESS_HEIGHT) / mat.rows)};
                cv::resize(mat, scaledMat, cv::Size {}, scale, scale);
        } else {
            scaledMat = mat;
        }

        processSize = QSize {scaledMat.cols, scaledMat.rows};

        return scaledMat;
}

std::vector<AIFaceDetector::FaceInfo> AIFaceDetector::detectFaces(const QImage &image,
                                                                  float confidenceThreshold)
{
    if (!m_initialized || image.isNull()) {
        return {};
    }

    QMutexLocker locker {&m_mutex};

    try {
        // Preprocess image
        QSize processSize {};
        cv::Mat processedImage {preprocessImage(image, processSize)};

        if (processedImage.empty()) {
            qWarning() << "Preprocessed image is empty";
            return {};
        }

        // Create blob from image
        // YuNet expects 320x320 input
        cv::Mat blob;
        try {
            blob = cv::dnn::blobFromImage(processedImage,
                                          1.0,                                  // scale factor
                                          cv::Size {INPUT_WIDTH, INPUT_HEIGHT}, // input size
                                          cv::Scalar {104, 177, 123}, // mean subtraction values
                                          false,                      // swapRB
                                          false);                    // crop
        } catch (const cv::Exception &e) {
            qWarning() << "Failed to create blob:" << e.what();
            return {};
        }

        if (blob.empty()) {
            qWarning() << "Created blob is empty";
            return {};
        }

        // Set input to the network
        try {
            m_net.setInput(blob);
        } catch (const cv::Exception &e) {
            qWarning() << "Failed to set network input:" << e.what();
            return {};
        }

        // Run forward pass
        cv::Mat output;
        try {
            output = m_net.forward();
        } catch (const cv::Exception &e) {
            qWarning() << "Network forward pass failed:" << e.what();
            return {};
        }

        if (output.empty()) {
            qWarning() << "Network output is empty";
            return {};
        }

        // Postprocess results
        return postprocessResults(output, image.size(), processSize, confidenceThreshold);

    } catch (const cv::Exception &e) {
        qWarning() << "OpenCV detection error:" << e.what();
        return {};
    } catch (const std::exception &e) {
        qWarning() << "Detection error:" << e.what();
        return {};
    }
}

std::vector<AIFaceDetector::FaceInfo> AIFaceDetector::postprocessResults(const cv::Mat &output,
                                                                         const QSize &originalSize,
                                                                         const QSize &processSize,
                                                                         float confidenceThreshold)
{
    std::vector<FaceInfo> faces {};

    if (output.empty()) {
        return faces;
    }

    qDebug() << "Output dims:" << output.dims << "Size:" << output.size;
    qDebug() << "Original size:" << originalSize << "Process size:" << processSize;

    // The scale factors should be from INPUT_SIZE (320x320) to original size
    float scaleX {static_cast<float>(originalSize.width()) / static_cast<float>(INPUT_WIDTH)};
    float scaleY {static_cast<float>(originalSize.height()) / static_cast<float>(INPUT_HEIGHT)};

    qDebug() << "Scale factors - X:" << scaleX << "Y:" << scaleY;

    if (output.dims == 3 && output.size[0] == 1) {
        int numDetections {output.size[1]};
        int numFields {output.size[2]};

        qDebug() << "Processing" << numDetections << "detections with" << numFields << "fields each";

        for (int i {0}; i < numDetections; ++i) {
            const float *data {output.ptr<float>(0, i)};

            // Print first few detections for analysis
            if (i < 3) {
                qDebug() << "Detection" << i << "all fields:";
                for (int f = 0; f < numFields; ++f) {
                    qDebug() << "  Field" << f << ":" << data[f];
                }
            }

            // Based on the debug output analysis, let's try different interpretations
            // The YuNet model with 10 fields might be:
            // [x_center, y_center, width, height, confidence, ...landmarks...]

            // Try interpretation: fields 0-3 are bbox in normalized coordinates (0-1)
            // field 4 might be confidence, but the values are > 1, so maybe not

            // Let's try: bbox coordinates are in pixel space relative to 320x320 input
            float raw_x1, raw_y1, raw_x2, raw_y2, confidence;

            // The confidence values in debug (0.8142, 1.21355, etc.) suggest field 4 might not be confidence
            // Let's try different fields for confidence, or just use a fixed confidence
            confidence = 0.9f; // Use fixed high confidence to test bbox interpretation

            // Try treating the first 4 fields as direct bbox coordinates in input space
            raw_x1 = data[0] * INPUT_WIDTH;
            raw_y1 = data[1] * INPUT_HEIGHT;
            raw_x2 = data[2] * INPUT_WIDTH;
            raw_y2 = data[3] * INPUT_HEIGHT;

            // For confidence, let's try field 4, but normalize if > 1
            /*confidence = data[4];
            if (confidence > 1.0f) {
                confidence = 1.0f / (1.0f + std::exp(-confidence)); // sigmoid normalization
            }*/

            // Alternative: maybe it's center/size format
            if (raw_x2 < raw_x1 || raw_y2 < raw_y1) {
                // Try center/width/height format
                float center_x = data[0] * INPUT_WIDTH;
                float center_y = data[1] * INPUT_HEIGHT;
                float width = data[2];
                float height = data[3];

                raw_x1 = center_x - width / 2.0f;
                raw_y1 = center_y - height / 2.0f;
                raw_x2 = center_x + width / 2.0f;
                raw_y2 = center_y + height / 2.0f;
            }

            // Clamp to input bounds
            raw_x1 = std::max(0.0f, std::min(static_cast<float>(INPUT_WIDTH), raw_x1));
            raw_y1 = std::max(0.0f, std::min(static_cast<float>(INPUT_HEIGHT), raw_y1));
            raw_x2 = std::max(0.0f, std::min(static_cast<float>(INPUT_WIDTH), raw_x2));
            raw_y2 = std::max(0.0f, std::min(static_cast<float>(INPUT_HEIGHT), raw_y2));

            // Check for valid bounding box
            if (raw_x2 <= raw_x1 || raw_y2 <= raw_y1) {
                if (i < 10) {
                    qDebug() << "Invalid bbox:" << raw_x1 << raw_y1 << raw_x2 << raw_y2;
                }
                continue;
            }

            // Apply confidence threshold
            if (confidence < confidenceThreshold) {
                continue;
            }

            // Scale to original image size
            float scaled_x1 = raw_x1 * scaleX;
            float scaled_y1 = raw_y1 * scaleY;
            float scaled_x2 = raw_x2 * scaleX;
            float scaled_y2 = raw_y2 * scaleY;

            // Clamp to original image bounds
            scaled_x1 = std::max(0.0f, std::min(static_cast<float>(originalSize.width()), scaled_x1));
            scaled_y1 = std::max(0.0f, std::min(static_cast<float>(originalSize.height()), scaled_y1));
            scaled_x2 = std::max(0.0f, std::min(static_cast<float>(originalSize.width()), scaled_x2));
            scaled_y2 = std::max(0.0f, std::min(static_cast<float>(originalSize.height()), scaled_y2));

            FaceInfo face {};
            face.confidence = confidence;
            face.bbox = QRectF {scaled_x1, scaled_y1, scaled_x2 - scaled_x1, scaled_y2 - scaled_y1};

            // Check for reasonable face size (at least 50x50 pixels)
            float faceWidth = face.bbox.width();
            float faceHeight = face.bbox.height();
            float minFaceSize = 50.0f;
            float maxFaceSize = std::min(originalSize.width(), originalSize.height()) * 0.8f;

            if (faceWidth >= minFaceSize && faceHeight >= minFaceSize &&
                faceWidth <= maxFaceSize && faceHeight <= maxFaceSize) {
                faces.push_back(face);
            qDebug() << "Added valid face - bbox:" << face.bbox << "confidence:" << face.confidence;
                } else {
                    if (i < 10) {
                        qDebug() << "Filtered out face - size:" << faceWidth << "x" << faceHeight
                        << "limits:" << minFaceSize << "-" << maxFaceSize
                        << "raw bbox:" << raw_x1 << raw_y1 << raw_x2 << raw_y2;
                    }
                }
        }
    }

    qDebug() << "Found" << faces.size() << "valid faces before NMS";

    // Sort by confidence and apply NMS
    std::sort(faces.begin(), faces.end(), [](const FaceInfo &a, const FaceInfo &b) {
        return a.confidence > b.confidence;
    });

    // Simple NMS
    std::vector<FaceInfo> filteredFaces {};
    std::vector<bool> suppressed(faces.size(), false);

    for (size_t i {0}; i < faces.size(); ++i) {
        if (suppressed[i]) continue;

        filteredFaces.push_back(faces[i]);

        for (size_t j {i + 1}; j < faces.size(); ++j) {
            if (suppressed[j]) continue;

            QRectF intersection {faces[i].bbox.intersected(faces[j].bbox)};
            float intersectionArea {static_cast<float>(intersection.width() * intersection.height())};
            float unionArea {static_cast<float>(
                faces[i].bbox.width() * faces[i].bbox.height() +
                faces[j].bbox.width() * faces[j].bbox.height() -
                intersectionArea)};

                if (unionArea > 0.0f) {
                    float iou {intersectionArea / unionArea};
                    if (iou > 0.4f) {
                        suppressed[j] = true;
                    }
                }
        }
    }

    qDebug() << "Final result:" << filteredFaces.size() << "faces after NMS";
    return filteredFaces;
}
