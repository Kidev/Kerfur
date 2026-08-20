// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "faceanalysis.h"

namespace Kerfur {

void ensureFaceAnalysisMetaTypesRegistered()
{
    // Both directions of the worker connection are queued, and a queued
    // connection resolves its argument types by name at connect time. Doing it
    // here, once, is what stops the first frame after a camera start from being
    // dropped with "cannot queue arguments of type".
    static const int result{qRegisterMetaType<AnalysisResult>("Kerfur::AnalysisResult")};
    static const int capabilities{
        qRegisterMetaType<AnalysisCapabilities>("Kerfur::AnalysisCapabilities")};
    Q_UNUSED(result)
    Q_UNUSED(capabilities)
}

} // namespace Kerfur
