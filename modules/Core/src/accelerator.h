// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_ACCELERATOR_H
#define KERFUR_ACCELERATOR_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/// The boundary between Kerfur and an inference accelerator plugin.
///
/// Everything above `Kerfur::Accelerator` is shared with the plugin and uses the
/// standard library only, so the plugin links its vendor runtime and nothing of
/// Kerfur's. The plugin is opened at run time: a machine without the plugin, or
/// without the runtime the plugin needs, runs every stage on the CPU.
namespace Kerfur::Accel {

/// Raised whenever an incompatible change is made to anything in this
/// namespace. The plugin refuses a version it was not built for.
inline constexpr int interfaceVersion{1};

/// One tensor of a compiled model. Height, width and channels follow the
/// model's own NHWC order; a vector output has height and width of one.
struct TensorInfo
{
    std::string name;
    int height{1};
    int width{1};
    int channels{0};
    /// The output already went through non maximum suppression on the device.
    /// Its values are then, per class: a box count, followed by that many
    /// boxes of `y_min, x_min, y_max, x_max, score`, normalized to the input.
    bool nms{false};
    int nmsClasses{0};
    int nmsMaxBoxesPerClass{0};

    std::size_t elements() const
    {
        return static_cast<std::size_t>(height) * static_cast<std::size_t>(width) *
               static_cast<std::size_t>(channels);
    }
};

/// One output of a run, dequantized to floats by the runtime.
struct Tensor
{
    TensorInfo info;
    std::vector<float> values;
};

/// A compiled model loaded on the device. Its input is one frame of 8 bit
/// interleaved RGB at the input shape; normalization is part of the model.
class Model
{
public:
    virtual ~Model() = default;

    virtual const TensorInfo &input() const = 0;
    virtual const std::vector<TensorInfo> &outputs() const = 0;

    /// Runs one frame and fills `results`, one entry per output in the order
    /// of `outputs()`. Returns an empty string on success, otherwise why not.
    /// Calls on one model must not overlap; separate models may run from
    /// separate threads.
    virtual std::string run(const std::uint8_t *rgb,
                            std::size_t bytes,
                            std::vector<Tensor> &results) = 0;
};

/// Speech recognition running on the device.
class SpeechToText
{
public:
    virtual ~SpeechToText() = default;

    /// Transcribes 16 kHz mono samples in [-1, 1), or translates them to
    /// English with `translate`. `language` is a two letter code. Returns an
    /// empty string on success, otherwise why not.
    virtual std::string transcribe(const float *samples,
                                   std::size_t count,
                                   const std::string &language,
                                   bool translate,
                                   std::string &text) = 0;
};

/// The device. Every model opened from it shares it, and it outlives them.
class Device
{
public:
    virtual ~Device() = default;

    /// One line naming the device, for the log.
    virtual std::string description() const = 0;

    /// Loads a compiled model, or returns null and says why in `error`.
    virtual std::unique_ptr<Model> openModel(const std::string &path, std::string &error) = 0;

    /// Loads a speech recognition model, or returns null and says why.
    virtual std::unique_ptr<SpeechToText> openSpeechToText(const std::string &path,
                                                           std::string &error) = 0;
};

} // namespace Kerfur::Accel

/// The one symbol a plugin exports. Returns a new device, owned by the caller,
/// or null with a reason written to `error` (at most `errorSize` bytes,
/// including the terminator).
extern "C" {
using KerfurOpenAccelerator = Kerfur::Accel::Device *(*) (int version,
                                                          char *error,
                                                          std::size_t errorSize);
}

namespace Kerfur {

/// The accelerator this process uses, opened on first use and kept until exit.
///
/// Looks for the plugin next to the executable, in `../lib` beside it, then on
/// the loader's own search path. Nothing found, or the setting
/// `Perception/accelerator` set to `cpu`, means no device: every stage then
/// runs on OpenCV, which is the normal state of every machine without one.
class Accelerator
{
public:
    static Accelerator &instance();

    /// The device, or null. Safe to call from any thread.
    Accel::Device *device();

    /// Why there is no device, or empty when there is one.
    std::string problem();

    /// The plugin's file name without platform prefix or suffix.
    static constexpr const char *pluginName{"kerfur-hailo"};

    /// The device of the first of `candidates` that loads as a plugin, or
    /// null with the reason in `problem`. A plugin that loads and finds no
    /// device ends the search: the next candidate is the same plugin.
    static std::unique_ptr<Accel::Device> openFrom(const std::vector<std::string> &candidates,
                                                   std::string &problem);

private:
    Accelerator() = default;
    void open();
    /// Closes the device when the application object goes, which is after
    /// every model has been destroyed with the engine and before the vendor
    /// runtime's own statics are torn down at exit.
    static void release();

    std::once_flag m_opened;
    std::unique_ptr<Accel::Device> m_device;
    std::string m_problem;
};

} // namespace Kerfur

#endif // KERFUR_ACCELERATOR_H
