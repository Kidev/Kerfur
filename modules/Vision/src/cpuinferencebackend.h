// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_CPUINFERENCEBACKEND_H
#define KERFUR_CPUINFERENCEBACKEND_H

#include "facepipeline.h"
#include "identitystore.h"
#include "modelcatalog.h"
#include "objectdetector.h"
#include "orientationresolver.h"
#include "persontracker.h"
#include "posedetector.h"
#include "visionconfig.h"
#include "visiontypes.h"

#include "perceptiontypes.h"

#include <QString>
#include <QStringList>

namespace Kerfur {

/// The five stages of the CPU perception graph, in one object that lives on the
/// inference worker thread and is touched by nothing else.
///
/// It replaces the Hailo graph one stage at a time rather than as a whole: the
/// stage boundaries, the cadences and the value it produces are the same, and
/// only the thing running the arithmetic differs. That is what lets the
/// tracker, the orientation recovery and the identity vote be shared code
/// rather than two implementations.
class CpuInferenceBackend
{
public:
    explicit CpuInferenceBackend(VisionConfig config = VisionConfig::forPlatform());

    /// Loads every stage the catalog has a file for. Returns false only when
    /// the pose stage is missing, because nothing else in the graph has an
    /// input without it.
    bool initialize(const ModelCatalog &catalog,
                    const QStringList &galleries,
                    const QString &identityCachePath);

    /// Enrols every face the galleries declare, merged as
    /// `IdentityRegistry::load` merges them. Called again whenever the
    /// galleries change; an unchanged file is answered from the cache.
    void enrolGallery(const QStringList &galleries, const QString &identityCachePath);

    bool isReady() const { return m_pose.isLoaded(); }

    /// Runs the graph over one captured frame. Returns a frame with no people
    /// rather than nothing when there is nobody there, because "I looked and
    /// the room is empty" and "I did not look" are different facts to everyone
    /// downstream.
    PerceptionFrame process(const VisionFrame &frame);

    /// Negative returns to automatic. Mirrors the control panel's override.
    void setOrientationOverride(int degrees);
    int orientationDegrees() const { return m_orientation.degrees(); }

    void reset();

private:
    cv::Mat applyOrientation(const cv::Mat &source) const;
    cv::Rect headRegion(const PersonTrack &track, const cv::Size &frameSize) const;
    void runFaceStages(const cv::Mat &image, qint64 timestampMs);
    void attributeObjects(const QList<ObjectDetection> &objects);

    VisionConfig m_config;
    PoseDetector m_pose;
    ObjectDetector m_objects;
    FacePipeline m_faces;
    IdentityStore m_identities;
    PersonTracker m_tracker;
    OrientationResolver m_orientation;
    quint64 m_processedFrames{0};
};

} // namespace Kerfur

#endif // KERFUR_CPUINFERENCEBACKEND_H
