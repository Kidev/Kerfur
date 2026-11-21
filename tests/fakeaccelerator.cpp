// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

// A stand-in for an accelerator plugin, for tst_accelerator. It opens a device,
// or refuses with the text of KERFUR_FAKE_ACCELERATOR_REFUSES when that is set:
// the two answers a real plugin gives on a machine with and without the
// hardware.

#include "accelerator.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace {

class FakeDevice final : public Kerfur::Accel::Device
{
public:
    std::string description() const override { return "fake device"; }

    std::unique_ptr<Kerfur::Accel::Model> openModel(const std::string &path,
                                                    std::string &error) override
    {
        error = path + ": the fake device runs no models";
        return nullptr;
    }

    std::unique_ptr<Kerfur::Accel::SpeechToText> openSpeechToText(const std::string &path,
                                                                  std::string &error) override
    {
        error = path + ": the fake device runs no models";
        return nullptr;
    }
};

} // namespace

extern "C" [[gnu::visibility("default")]] Kerfur::Accel::Device *kerfurOpenAccelerator(
    int version, char *error, std::size_t errorSize)
{
    const char *const refusal{std::getenv("KERFUR_FAKE_ACCELERATOR_REFUSES")};
    if (version != Kerfur::Accel::interfaceVersion || refusal != nullptr) {
        const std::string message{refusal != nullptr ? refusal : "wrong interface version"};
        const std::size_t length{std::min(message.size(), errorSize - 1)};
        std::memcpy(error, message.data(), length);
        error[length] = '\0';
        return nullptr;
    }
    return new FakeDevice;
}
