// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "accelerator.h"

#include <hailo/genai/speech2text/speech2text.hpp>
#include <hailo/hailort.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using Kerfur::Accel::Device;
using Kerfur::Accel::Model;
using Kerfur::Accel::SpeechToText;
using Kerfur::Accel::Tensor;
using Kerfur::Accel::TensorInfo;

/// Lets another process on the same machine (a language model server, for
/// one) schedule its networks on the device alongside these.
constexpr const char *sharedGroup{"SHARED"};

constexpr std::chrono::milliseconds runTimeout{2000};
constexpr std::chrono::milliseconds speechTimeout{30000};

std::string describe(hailo_status status)
{
    const char *const message{hailo_get_status_message(status)};
    return message != nullptr ? std::string{message} : "status " + std::to_string(status);
}

class HailoModel final : public Model
{
public:
    HailoModel(std::shared_ptr<hailort::VDevice> device,
               std::shared_ptr<hailort::InferModel> model,
               hailort::ConfiguredInferModel configured,
               hailort::ConfiguredInferModel::Bindings bindings,
               TensorInfo input,
               std::vector<TensorInfo> outputs)
        : m_device{std::move(device)}
        , m_model{std::move(model)}
        , m_configured{std::move(configured)}
        , m_bindings{std::move(bindings)}
        , m_input{std::move(input)}
        , m_outputs{std::move(outputs)}
    {}

    ~HailoModel() override { m_configured.shutdown(); }

    const TensorInfo &input() const override { return m_input; }
    const std::vector<TensorInfo> &outputs() const override { return m_outputs; }

    std::string run(const std::uint8_t *rgb,
                    std::size_t bytes,
                    std::vector<Tensor> &results) override
    {
        if (rgb == nullptr || bytes != m_input.elements()) {
            return "input of " + std::to_string(bytes) + " bytes, the model takes " +
                   std::to_string(m_input.elements());
        }

        auto input{m_bindings.input()};
        if (!input) {
            return describe(input.status());
        }
        hailo_status status{input->set_buffer(hailort::MemoryView::create_const(rgb, bytes))};
        if (status != HAILO_SUCCESS) {
            return describe(status);
        }

        // Bound to the caller's vectors, so the runtime writes the floats where
        // they are read and nothing is copied afterwards.
        results.resize(m_outputs.size());
        for (std::size_t index{0}; index < m_outputs.size(); ++index) {
            Tensor &result{results[index]};
            result.info = m_outputs[index];
            result.values.resize(m_outputs[index].elements());
            auto output{m_bindings.output(m_outputs[index].name)};
            if (!output) {
                return describe(output.status());
            }
            status = output->set_buffer(
                hailort::MemoryView{result.values.data(), result.values.size() * sizeof(float)});
            if (status != HAILO_SUCCESS) {
                return describe(status);
            }
        }

        status = m_configured.run(m_bindings, runTimeout);
        return status == HAILO_SUCCESS ? std::string{} : describe(status);
    }

private:
    /// Held so that the device outlives every model opened on it, whatever
    /// order their owners are destroyed in.
    std::shared_ptr<hailort::VDevice> m_device;
    std::shared_ptr<hailort::InferModel> m_model;
    hailort::ConfiguredInferModel m_configured;
    hailort::ConfiguredInferModel::Bindings m_bindings;
    TensorInfo m_input;
    std::vector<TensorInfo> m_outputs;
};

class HailoSpeech final : public SpeechToText
{
public:
    HailoSpeech(std::shared_ptr<hailort::VDevice> device,
                std::unique_ptr<hailort::genai::Speech2Text> speech)
        : m_device{std::move(device)}, m_speech{std::move(speech)}
    {}

    std::string transcribe(const float *samples,
                           std::size_t count,
                           const std::string &language,
                           bool translate,
                           std::string &text) override
    {
        text.clear();
        auto parameters{m_speech->create_generator_params()};
        if (!parameters) {
            return describe(parameters.status());
        }
        hailo_status status{
            parameters->set_task(translate ? hailort::genai::Speech2TextTask::TRANSLATE
                                           : hailort::genai::Speech2TextTask::TRANSCRIBE)};
        if (status == HAILO_SUCCESS && !language.empty()) {
            status = parameters->set_language(language);
        }
        if (status != HAILO_SUCCESS) {
            return describe(status);
        }

        const hailort::MemoryView audio{
            hailort::MemoryView::create_const(samples, count * sizeof(float))};
        auto segments{m_speech->generate_all_segments(audio, parameters.value(), speechTimeout)};
        if (!segments) {
            return describe(segments.status());
        }
        for (const auto &segment : segments.value()) {
            if (!text.empty() && !segment.text.empty()) {
                text.push_back(' ');
            }
            text.append(segment.text);
        }
        return {};
    }

private:
    std::shared_ptr<hailort::VDevice> m_device;
    std::unique_ptr<hailort::genai::Speech2Text> m_speech;
};

class HailoDevice final : public Device
{
public:
    HailoDevice(std::shared_ptr<hailort::VDevice> device, std::string description)
        : m_device{std::move(device)}, m_description{std::move(description)}
    {}

    std::string description() const override { return m_description; }

    std::unique_ptr<Model> openModel(const std::string &path, std::string &error) override
    {
        auto created{m_device->create_infer_model(path)};
        if (!created) {
            error = path + ": " + describe(created.status());
            return nullptr;
        }
        std::shared_ptr<hailort::InferModel> model{created.release()};
        model->set_batch_size(1);

        if (model->inputs().size() != 1) {
            error = path + ": " + std::to_string(model->inputs().size()) +
                    " inputs, one image input expected";
            return nullptr;
        }
        auto inputStream{model->input()};
        if (!inputStream) {
            error = path + ": " + describe(inputStream.status());
            return nullptr;
        }
        inputStream->set_format_type(HAILO_FORMAT_TYPE_UINT8);
        const hailo_3d_image_shape_t inputShape{inputStream->shape()};
        TensorInfo input;
        input.name = inputStream->name();
        input.height = static_cast<int>(inputShape.height);
        input.width = static_cast<int>(inputShape.width);
        input.channels = static_cast<int>(inputShape.features);
        if (input.channels != 3) {
            error = path + ": input has " + std::to_string(input.channels) +
                    " channels, an RGB image expected";
            return nullptr;
        }

        std::vector<TensorInfo> outputs;
        for (const std::string &name : model->get_output_names()) {
            auto stream{model->output(name)};
            if (!stream) {
                error = path + ": " + describe(stream.status());
                return nullptr;
            }
            TensorInfo info;
            info.name = name;
            if (stream->is_nms()) {
                // Already floats; the device did the dequantizing as part of the
                // suppression, and a box list has no shape beyond its size.
                const auto shape{stream->get_nms_shape()};
                if (!shape) {
                    error = path + ": " + describe(shape.status());
                    return nullptr;
                }
                info.nms = true;
                info.nmsClasses = static_cast<int>(shape->number_of_classes);
                info.nmsMaxBoxesPerClass = static_cast<int>(shape->max_bboxes_per_class);
                info.channels = static_cast<int>(stream->get_frame_size() / sizeof(float));
            } else {
                stream->set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
                const hailo_3d_image_shape_t shape{stream->shape()};
                info.height = static_cast<int>(shape.height);
                info.width = static_cast<int>(shape.width);
                info.channels = static_cast<int>(shape.features);
            }
            outputs.push_back(std::move(info));
        }

        auto configured{model->configure()};
        if (!configured) {
            error = path + ": " + describe(configured.status());
            return nullptr;
        }
        auto bindings{configured->create_bindings()};
        if (!bindings) {
            error = path + ": " + describe(bindings.status());
            return nullptr;
        }
        return std::make_unique<HailoModel>(m_device,
                                            std::move(model),
                                            configured.release(),
                                            bindings.release(),
                                            std::move(input),
                                            std::move(outputs));
    }

    std::unique_ptr<SpeechToText> openSpeechToText(const std::string &path,
                                                   std::string &error) override
    {
        const hailort::genai::Speech2TextParams parameters{path};
        auto created{hailort::genai::Speech2Text::create(m_device, parameters)};
        if (!created) {
            error = path + ": " + describe(created.status());
            return nullptr;
        }
        return std::make_unique<HailoSpeech>(m_device,
                                             std::make_unique<hailort::genai::Speech2Text>(
                                                 created.release()));
    }

private:
    std::shared_ptr<hailort::VDevice> m_device;
    std::string m_description;
};

void writeError(char *error, std::size_t errorSize, const std::string &message)
{
    if (error == nullptr || errorSize == 0) {
        return;
    }
    const std::size_t length{std::min(message.size(), errorSize - 1)};
    std::memcpy(error, message.data(), length);
    error[length] = '\0';
}

} // namespace

extern "C" [[gnu::visibility("default")]] Kerfur::Accel::Device *kerfurOpenAccelerator(
    int version, char *error, std::size_t errorSize)
{
    if (version != Kerfur::Accel::interfaceVersion) {
        writeError(error,
                   errorSize,
                   "plugin built for interface " + std::to_string(Kerfur::Accel::interfaceVersion) +
                       ", Kerfur asked for " + std::to_string(version));
        return nullptr;
    }

    hailo_vdevice_params_t parameters{};
    hailo_status status{hailo_init_vdevice_params(&parameters)};
    if (status != HAILO_SUCCESS) {
        writeError(error, errorSize, describe(status));
        return nullptr;
    }
    parameters.scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN;
    parameters.group_id = sharedGroup;

    auto created{hailort::VDevice::create_shared(parameters)};
    if (!created) {
        writeError(error, errorSize, describe(created.status()));
        return nullptr;
    }
    std::shared_ptr<hailort::VDevice> device{created.release()};

    hailo_version_t library{};
    std::string description{"HailoRT"};
    if (hailo_get_library_version(&library) == HAILO_SUCCESS) {
        description += " " + std::to_string(library.major) + "." + std::to_string(library.minor) +
                       "." + std::to_string(library.revision);
    }
    auto identifiers{device->get_physical_devices_ids()};
    if (identifiers) {
        for (const std::string &identifier : identifiers.value()) {
            description += " " + identifier;
        }
    }
    return new HailoDevice{std::move(device), std::move(description)};
}
