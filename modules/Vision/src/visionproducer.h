// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_VISIONPRODUCER_H
#define KERFUR_VISIONPRODUCER_H

#include "boundedqueue.h"
#include "perceptionpipe.h"

#include <QObject>
#include <QThread>

#include <memory>

namespace Kerfur {

class CameraFrameSource;
class VisionInferenceWorker;
class WorldModel;
struct VisionFrame;

/// Owns the camera and the inference graph, and is the only writer of the
/// visual half of WorldModel.
///
/// It is instantiated at the application root and is not an importable QML
/// type. Nothing may reach into it, which is the whole point of the inversion:
/// producers push into the shared model and consumers read the model.
class VisionProducer : public QObject
{
    Q_OBJECT

public:
    explicit VisionProducer(WorldModel &world, QObject *parent = nullptr);
    ~VisionProducer() override;

    bool isEnabled() const { return m_enabled; }

public slots:
    void setEnabled(bool enabled);

    /// Degrees, or negative to return to automatic. This is what the control
    /// panel's orientation control drives.
    void setOrientationOverride(int degrees);

signals:
    void enabledChanged();
    void orientationOverrideRequested(int degrees);

private:
    void startPipeline();
    void stopPipeline();

    WorldModel &m_world;
    PerceptionPipe m_pipe;

    // Behind a pointer so that this header stays free of OpenCV: it is the one
    // Vision header the application root includes, and a frame queue by value
    // would drag cv::Mat, and therefore the OpenCV include path, into every
    // consumer of the module. OpenCV stays an implementation detail here.
    std::unique_ptr<BoundedQueue<VisionFrame>> m_frames;
    QThread m_workerThread;
    CameraFrameSource *m_source{nullptr};
    VisionInferenceWorker *m_worker{nullptr};
    bool m_enabled{false};
};

} // namespace Kerfur

#endif // KERFUR_VISIONPRODUCER_H
