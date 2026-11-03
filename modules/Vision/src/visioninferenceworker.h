// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_VISIONINFERENCEWORKER_H
#define KERFUR_VISIONINFERENCEWORKER_H

#include "boundedqueue.h"
#include "cpuinferencebackend.h"
#include "visiontypes.h"

#include <QObject>
#include <QString>

namespace Kerfur {

class PerceptionPipe;

/// Owns the inference graph and runs it, on its own thread.
///
/// Nothing else touches the backend. Model loading happens here too rather than
/// in the constructor, because reading thirteen megabytes of ONNX and building
/// its layer graph takes long enough to be visible as a stalled face if it
/// happens on the GUI thread.
class VisionInferenceWorker : public QObject
{
    Q_OBJECT

public:
    VisionInferenceWorker(BoundedQueue<VisionFrame> &queue,
                          PerceptionPipe &pipe,
                          QObject *parent = nullptr);

    bool isReady() const { return m_ready; }

public slots:
    /// Loads every provisioned stage. Runs on the worker thread.
    void initialize();

    /// Drains the frame queue, processes the newest frame and submits the
    /// result. Connect the source's signal to this queued.
    void processPending();

    void setOrientationOverride(int degrees);

    /// Re-enrols from the galleries. Connected to
    /// `IdentityLibrary::galleryChanged`.
    void reloadGallery();

signals:
    void readyChanged(bool ready);

private:
    BoundedQueue<VisionFrame> &m_queue;
    PerceptionPipe &m_pipe;
    CpuInferenceBackend m_backend;
    bool m_ready{false};
    bool m_initialized{false};
};

} // namespace Kerfur

#endif // KERFUR_VISIONINFERENCEWORKER_H
