// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "modelcatalog.h"

#include "modeldirectory.h"

#include <QDir>
#include <QFileInfo>

namespace Kerfur {

namespace {

constexpr ModelId allModels[]{ModelId::Pose,
                              ModelId::Objects,
                              ModelId::Face,
                              ModelId::Identity,
                              ModelId::Emotion,
                              ModelId::Palm,
                              ModelId::HandLandmarks,
                              ModelId::PoseHailo,
                              ModelId::ClipHailo,
                              ModelId::FaceHailo,
                              ModelId::IdentityHailo,
                              ModelId::HandLandmarksHailo};

} // namespace

QString ModelCatalog::fileName(ModelId id)
{
    switch (id) {
    case ModelId::Pose:
        return QStringLiteral("yolov8n-pose.onnx");
    case ModelId::Objects:
        return QStringLiteral("object_detection_nanodet_2022nov.onnx");
    case ModelId::Face:
        return QStringLiteral("face_detection_yunet_2023mar.onnx");
    case ModelId::Identity:
        return QStringLiteral("face_recognition_sface_2021dec.onnx");
    case ModelId::Emotion:
        return QStringLiteral("facial_expression_recognition_mobilefacenet_2022july.onnx");
    case ModelId::Palm:
        return QStringLiteral("palm_detection_mediapipe_2023feb.onnx");
    case ModelId::HandLandmarks:
        return QStringLiteral("handpose_estimation_mediapipe_2023feb.onnx");
    case ModelId::PoseHailo:
        return QStringLiteral("yolov8m_pose.hef");
    case ModelId::ClipHailo:
        return QStringLiteral("clip_vit_l_14_laion2B_image_encoder.hef");
    case ModelId::FaceHailo:
        return QStringLiteral("scrfd_10g.hef");
    case ModelId::IdentityHailo:
        return QStringLiteral("arcface_r50.hef");
    case ModelId::HandLandmarksHailo:
        return QStringLiteral("hand_landmark_lite.hef");
    }
    return {};
}

QString ModelCatalog::stageName(ModelId id)
{
    switch (id) {
    case ModelId::Pose:
        return QStringLiteral("pose");
    case ModelId::Objects:
        return QStringLiteral("objects");
    case ModelId::Face:
        return QStringLiteral("face");
    case ModelId::Identity:
        return QStringLiteral("identity");
    case ModelId::Emotion:
        return QStringLiteral("emotion");
    case ModelId::Palm:
        return QStringLiteral("palm");
    case ModelId::HandLandmarks:
        return QStringLiteral("handlandmarks");
    case ModelId::PoseHailo:
        return QStringLiteral("pose-hailo");
    case ModelId::ClipHailo:
        return QStringLiteral("clip-hailo");
    case ModelId::FaceHailo:
        return QStringLiteral("face-hailo");
    case ModelId::IdentityHailo:
        return QStringLiteral("identity-hailo");
    case ModelId::HandLandmarksHailo:
        return QStringLiteral("handlandmarks-hailo");
    }
    return {};
}

ModelCatalog ModelCatalog::resolve()
{
    ModelCatalog catalog;
    catalog.m_directory = ModelDirectory::resolve();

    if (catalog.m_directory.isEmpty()) {
        return catalog;
    }

    const QDir directory{catalog.m_directory};
    for (const ModelId id : allModels) {
        const QString absolute{directory.absoluteFilePath(fileName(id))};
        if (QFileInfo::exists(absolute)) {
            catalog.m_paths.insert(id, absolute);
        }
    }
    return catalog;
}

bool ModelCatalog::isAccelerated(ModelId id)
{
    switch (id) {
    case ModelId::PoseHailo:
    case ModelId::ClipHailo:
    case ModelId::FaceHailo:
    case ModelId::IdentityHailo:
    case ModelId::HandLandmarksHailo:
        return true;
    case ModelId::Pose:
    case ModelId::Objects:
    case ModelId::Face:
    case ModelId::Identity:
    case ModelId::Emotion:
    case ModelId::Palm:
    case ModelId::HandLandmarks:
        return false;
    }
    return false;
}

QStringList ModelCatalog::missingStages(bool includeAccelerated) const
{
    QStringList missing;
    for (const ModelId id : allModels) {
        if (!includeAccelerated && isAccelerated(id)) {
            continue;
        }
        if (!has(id)) {
            missing.append(stageName(id));
        }
    }
    return missing;
}

} // namespace Kerfur
