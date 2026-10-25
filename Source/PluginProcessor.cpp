#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>

namespace {
class ActiveVoiceScope {
public:
    explicit ActiveVoiceScope(juce::AudioProcessor& processor) : processor_(processor) {
        processor_.suspendProcessing(true);
    }
    ~ActiveVoiceScope() { processor_.suspendProcessing(false); }
private:
    juce::AudioProcessor& processor_;
};

const juce::StringArray kParameterIds{
    "volume", "cutoff", "resonance", "delay", "chorus", "waveform"};

std::vector<xstnsions::ExtensionManifest> readBundledPresets() {
    const std::array<std::pair<const char*, const char*>, 2> packages{{
        {"aurora-keys", BinaryData::extensions_aurora_002dkeys_extension_json},
        {"velvet-pulse", BinaryData::extensions_velvet_002dpulse_extension_json}}};
    std::vector<xstnsions::ExtensionManifest> presets;
    for (const auto& package : packages) {
        const auto json = juce::String::fromUTF8(
            package.second, static_cast<int>(std::strlen(package.second)));
        auto temporary = std::filesystem::temp_directory_path() /
                         ("xstnsions-extension-" + std::string(package.first) + "-" +
                          std::to_string(reinterpret_cast<std::uintptr_t>(package.second)) + ".json");
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << json.toStdString();
        }
        try {
            presets.push_back(xstnsions::parseManifest(temporary));
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
    }
    return presets;
}

std::filesystem::path userExtensionsDirectory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("XSTNSIONS").getChildFile("Extensions").getFullPathName().toStdString();
}
}

XSTNSIONSProcessor::XSTNSIONSProcessor()
    : juce::AudioProcessor(BusesProperties()
#if XSTNSIONS_INSTRUMENT
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#else
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
      ),
      parameters_(*this, nullptr, "PARAMETERS", createParameterLayout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout XSTNSIONSProcessor::createParameterLayout() {
    using Float = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    parameters.push_back(std::make_unique<Float>(
        "volume", "Volume", juce::NormalisableRange<float>(0.0f, 1.0f), 0.78f));
    parameters.push_back(std::make_unique<Float>(
        "cutoff", "Tone", juce::NormalisableRange<float>(0.0f, 1.0f), 0.72f));
    parameters.push_back(std::make_unique<Float>(
        "resonance", "Resonance", juce::NormalisableRange<float>(0.0f, 0.95f), 0.12f));
    parameters.push_back(std::make_unique<Float>(
        "delay", "Echo", juce::NormalisableRange<float>(0.0f, 1.0f), 0.12f));
    parameters.push_back(std::make_unique<Float>(
        "chorus", "Shimmer", juce::NormalisableRange<float>(0.0f, 1.0f), 0.08f));
    parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
        "waveform", "Wave", juce::StringArray{"Sine", "Saw", "Pulse"}, 0));
    return {parameters.begin(), parameters.end()};
}

void XSTNSIONSProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) {
    engine_.prepare(sampleRate, static_cast<std::size_t>(std::max(1, maximumExpectedSamplesPerBlock)));
}

void XSTNSIONSProcessor::releaseResources() {}
const juce::String XSTNSIONSProcessor::getName() const { return JucePlugin_Name; }
bool XSTNSIONSProcessor::acceptsMidi() const { return XSTNSIONS_INSTRUMENT != 0; }

void XSTNSIONSProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    const juce::ScopedLock parameterLock(parameterMutex_);
    ActiveVoiceScope safeParameterChange(*this);
    std::size_t changeCount = 0;
    const auto changes = parameterChanges_.drain(changeCount);
    for (std::size_t index = 0; index < changeCount; ++index) {
        const auto change = changes[index];
        if (change.parameter < 0 || change.parameter >= kParameterIds.size()) continue;
        const auto& parameterId = kParameterIds[change.parameter];
        if (auto* value = parameters_.getRawParameterValue(parameterId)) {
            const float updated = juce::jlimit(
                0.0f, 1.0f, value->load(std::memory_order_relaxed) + change.delta);
            value->store(updated, std::memory_order_relaxed);
            if (auto* parameter = parameters_.getParameter(parameterId))
                parameter->setValueNotifyingHost(updated);
        }
    }
    for (const auto& identifier : kParameterIds)
        if (auto* value = parameters_.getRawParameterValue(identifier))
            engine_.setParameter(identifier.toStdString(), value->load());

    for (const auto metadata : midiMessages) {
        const auto message = metadata.getMessage();
        if (message.isNoteOn()) engine_.noteOn(message.getNoteNumber(), message.getFloatVelocity());
        else if (message.isNoteOff()) engine_.noteOff(message.getNoteNumber());
        else if (message.isAllNotesOff() || message.isAllSoundOff()) engine_.reset();
    }

    std::array<float*, 2> channels{{
        buffer.getNumChannels() > 0 ? buffer.getWritePointer(0) : nullptr,
        buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr}};
    engine_.process(
        channels.data(), static_cast<std::size_t>(juce::jmin(buffer.getNumChannels(), 2)),
        static_cast<std::size_t>(buffer.getNumSamples()));
}

juce::AudioProcessorEditor* XSTNSIONSProcessor::createEditor() {
    return new XSTNSIONSEditor(*this);
}

void XSTNSIONSProcessor::getStateInformation(juce::MemoryBlock& destination) {
    juce::ScopedLock lock(parameterMutex_);
    std::unique_ptr<juce::XmlElement> xml(parameters_.copyState().createXml());
    if (xml) copyXmlToBinary(*xml, destination);
}

void XSTNSIONSProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml || !xml->hasTagName(parameters_.state.getType())) return;
    juce::ScopedLock lock(parameterMutex_);
    ActiveVoiceScope safeStateChange(*this);
    parameters_.replaceState(juce::ValueTree::fromXml(*xml));
}

void XSTNSIONSProcessor::setParameterFromUI(const juce::String& parameter, float value) {
    const auto found = std::find(kParameterIds.begin(), kParameterIds.end(), parameter);
    if (found == kParameterIds.end()) return;
    const auto index = static_cast<int>(std::distance(kParameterIds.begin(), found));
    juce::ScopedLock lock(parameterMutex_);
    if (auto* audioParameter = parameters_.getParameter(parameter)) {
        const float normalized = audioParameter->convertTo0to1(value);
        const float delta = normalized - audioParameter->getValue();
        audioParameter->beginChangeGesture();
        audioParameter->setValueNotifyingHost(normalized);
        audioParameter->endChangeGesture();
        parameterChanges_.add(index, delta);
    }
}

std::vector<xstnsions::ExtensionManifest> XSTNSIONSProcessor::availablePresets() const {
    auto presets = readBundledPresets();
    const auto directory = userExtensionsDirectory();
    if (std::filesystem::exists(directory)) {
        for (const auto& preset : xstnsions::scanExtensions(directory)) {
            if (preset.kind != "preset") continue;
            const bool duplicate = std::any_of(presets.begin(), presets.end(), [&](const auto& bundled) {
                return bundled.id == preset.id;
            });
            if (!duplicate) presets.push_back(preset);
        }
    }
    std::sort(presets.begin(), presets.end(), [](const auto& first, const auto& second) {
        return first.name < second.name;
    });
    return presets;
}

void XSTNSIONSProcessor::applyPreset(const juce::String& id) {
    const auto presets = availablePresets();
    const auto found = std::find_if(presets.begin(), presets.end(), [&](const auto& preset) {
        return juce::String(preset.id) == id;
    });
    if (found == presets.end()) throw std::runtime_error("The selected preset is no longer available");
    const auto apply = [this](const char* parameter, const std::optional<float>& value) {
        if (value) setParameterFromUI(parameter, *value);
    };
    apply("volume", found->volume);
    apply("cutoff", found->cutoff);
    apply("resonance", found->resonance);
    apply("delay", found->delay);
    apply("chorus", found->chorus);
    if (found->waveform)
        setParameterFromUI("waveform", *found->waveform == "saw" ? 1.0f :
                                                 (*found->waveform == "pulse" ? 2.0f : 0.0f));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new XSTNSIONSProcessor();
}
