// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

// The models compiled for the Hailo-10H. Selected by default where the Hailo
// driver has created its device node, and offered unselected everywhere else.

function Component()
{
}

Component.prototype.isDefault = function()
{
    return installer.fileExists("/dev/hailo0");
}
