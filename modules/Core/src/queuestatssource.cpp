// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "queuestatssource.h"

namespace Kerfur {

// Out of line so the vtable is emitted once rather than in every translation
// unit that instantiates a BoundedQueue.
QueueStatsSource::~QueueStatsSource() = default;

} // namespace Kerfur
