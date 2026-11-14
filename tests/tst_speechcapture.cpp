// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "SpeechRecognizer.hpp"

#include <QCoreApplication>
#include <QTest>

/// The microphone and the speech model are two separate things.
///
/// A build without models still captures: the level meter, the voice segmenter,
/// speaker recognition and the enrolment of a voice all work on audio alone.
/// These cases run in a process that has no model directory, which is the
/// state of a fresh install, and pin that the missing model is reported as
/// such and never as a capture failure.
class TestSpeechCapture : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void missingModelIsNotACaptureError();
    void captureStartsWithoutAModel();
};

void TestSpeechCapture::initTestCase()
{
    // Named so that the model search finds nothing provisioned for another
    // Kerfur build in the application data location.
    QCoreApplication::setOrganizationName(QStringLiteral("kidev"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_speechcapture"));
}

void TestSpeechCapture::missingModelIsNotACaptureError()
{
    const SpeechRecognizer recognizer;

    QVERIFY(!recognizer.modelProblem().isEmpty());
    QVERIFY(recognizer.errorString().isEmpty());
}

void TestSpeechCapture::captureStartsWithoutAModel()
{
    SpeechRecognizer recognizer;
    recognizer.setEnabled(true);

    // Whatever the host has (a microphone, none, a pending permission), the
    // answer is about the device. The model problem stays where it was.
    QVERIFY(!recognizer.modelProblem().isEmpty());
    QVERIFY(recognizer.errorString() != recognizer.modelProblem());

    recognizer.setEnabled(false);
}

QTEST_GUILESS_MAIN(TestSpeechCapture)

#include "tst_speechcapture.moc"
