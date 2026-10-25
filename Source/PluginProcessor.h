#pragma once
#include <JuceHeader.h>
#include "AudioEngine.h"
#include <array>
#include <atomic>
#include <mutex>

struct ParameterChange {
    int parameter = -1;
    float delta = 0.0f;
};

class ParameterChangeQueue {
public:
    void add(int parameter, float delta) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& change : changes_)
            if (change.parameter == parameter) { change.delta += delta; return; }
        if (size_ < changes_.size()) changes_[size_++] = {parameter, delta};
    }
    std::array<ParameterChange, 6> drain(std::size_t& count) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        count = size_;
        const auto result = changes_;
        size_ = 0;
        return result;
    }
private:
    std::mutex mutex_;
    std::array<ParameterChange, 6> changes_{};
    std::size_t size_ = 0;
};

class XSTNSIONSProcessor final : public juce::AudioProcessor {
public:
    XSTNSIONSProcessor();
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    void setParameterFromUI(const juce::String& parameter, float value);
    std::vector<xstnsions::ExtensionManifest> availablePresets() const;
    void applyPreset(const juce::String& id);
    void registerEditor(class XSTNSIONSEditor* editor) noexcept { activeEditor_.store(editor); }
    juce::AudioProcessorValueTreeState& state() noexcept { return parameters_; }
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
private:
    xstnsions::AudioEngine engine_;
    juce::AudioProcessorValueTreeState parameters_;
    ParameterChangeQueue parameterChanges_;
    juce::CriticalSection parameterMutex_;
    std::atomic<class XSTNSIONSEditor*> activeEditor_{nullptr};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XSTNSIONSProcessor)
};
