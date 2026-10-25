#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ExtensionManager.h"
#include <array>
#include <memory>
#include <vector>

class XSTNSIONSEditor final : public juce::AudioProcessorEditor {
public:
    explicit XSTNSIONSEditor(XSTNSIONSProcessor& processor);
    ~XSTNSIONSEditor() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;
private:
    XSTNSIONSProcessor& processor_;
    juce::Slider volume_, tone_, resonance_, echo_, shimmer_;
    juce::ComboBox waveform_, presetSelector_;
    juce::Label title_, subtitle_, extensionLabel_;
    std::array<juce::Label, 5> controlLabels_{};
    juce::TextButton importButton_{"IMPORT EXTENSION"};
    juce::TextButton refreshButton_{"REFRESH"};
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveformAttachment_;
    std::vector<xstnsions::ExtensionManifest> availablePresets_;
    bool suppressPresetCallback_ = false;
    std::unique_ptr<xstnsions::ExtensionManager> extensions_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String extensionStatus_;
    void refreshExtensions();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XSTNSIONSEditor)
};
