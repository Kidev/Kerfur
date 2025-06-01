#include "FaceTracker.hpp"
#include <QDebug>

FaceTracker::FaceTracker(QObject *parent) : QObject(parent)
{
    qDebug() << "FaceTracker created (minimal implementation)";
}

void FaceTracker::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;

    if (m_enabled) {
        // TODO: Initialize camera and face detection
        qDebug() << "FaceTracker enabled - camera initialization not implemented yet";
        setErrorString("Face tracking not yet implemented");
    } else {
        // TODO: Cleanup camera
        qDebug() << "FaceTracker disabled";
        setFaceDetected(false);
        setErrorString("");
    }

    emit enabledChanged();
}

void FaceTracker::setFaceDetected(bool detected)
{
    if (m_faceDetected == detected)
        return;
    m_faceDetected = detected;
    emit faceDetectedChanged();
}

void FaceTracker::setFaceCenter(const QPointF &center)
{
    if (m_faceCenter == center)
        return;
    m_faceCenter = center;
    emit faceCenterChanged();
}

void FaceTracker::setErrorString(const QString &error)
{
    if (m_errorString == error)
        return;
    m_errorString = error;
    emit errorStringChanged();
}
