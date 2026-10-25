#include "PluginEditor.h"

namespace {
const juce::Colour background(0xff101118), panel(0xff1a1c27), purple(0xffac8cff), muted(0xff898b9b);
}

XSTNSIONSEditor::XSTNSIONSEditor(XSTNSIONSProcessor& processor)
    : juce::AudioProcessorEditor(&processor), processor_(processor),
      extensions_(std::make_unique<xstnsions::ExtensionManager>(
          juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
              .getChildFile("XSTNSIONS").getChildFile("Extensions").getFullPathName().toStdString())) {
    setSize(900, 580);
    processor_.registerEditor(this);

    title_.setText("XSTNSIONS", juce::dontSendNotification);
    title_.setFont(juce::Font(29.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(title_);
    subtitle_.setText(XSTNSIONS_INSTRUMENT ? "INSTRUMENT     /     EXTENSION STUDIO" : "EFFECTS     /     EXTENSION STUDIO", juce::dontSendNotification);
    subtitle_.setFont(juce::Font(11.0f, juce::Font::bold));
    subtitle_.setColour(juce::Label::textColourId, purple);
    addAndMakeVisible(subtitle_);
    extensionLabel_.setFont(juce::Font(12.0f));
    extensionLabel_.setColour(juce::Label::textColourId, muted);
    extensionLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(extensionLabel_);

    const std::array<std::pair<const char*, juce::Slider*>, 5> sliders{{
        {"volume", &volume_}, {"cutoff", &tone_}, {"resonance", &resonance_},
        {"delay", &echo_}, {"chorus", &shimmer_}}};
    for (const auto& item : sliders) {
        auto* slider = item.second;
        slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 65, 22);
        slider->setColour(juce::Slider::rotarySliderFillColourId, purple);
        slider->setColour(juce::Slider::rotarySliderOutlineColourId, panel.brighter(0.25f));
        slider->setColour(juce::Slider::thumbColourId, juce::Colours::white);
        slider->setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        slider->setColour(juce::Slider::textBoxBackgroundColourId, panel);
        slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(*slider);
        attachments_.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor_.state(), item.first, *slider));
    }

    waveform_.addItem("Sine", 1);
    waveform_.addItem("Saw", 2);
    waveform_.addItem("Pulse", 3);
    waveform_.setVisible(XSTNSIONS_INSTRUMENT != 0);
    waveform_.setColour(juce::ComboBox::backgroundColourId, panel);
    waveform_.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    addAndMakeVisible(waveform_);
    waveformAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor_.state(), "waveform", waveform_);

    presetSelector_.setColour(juce::ComboBox::backgroundColourId, panel);
    presetSelector_.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    presetSelector_.onChange = [this] {
        if (suppressPresetCallback_ || presetSelector_.getSelectedId() <= 0) return;
        const int index = presetSelector_.getSelectedId() - 1;
        if (index >= 0 && index < static_cast<int>(availablePresets_.size())) {
            try {
                processor_.applyPreset(availablePresets_[static_cast<std::size_t>(index)].id);
                extensionStatus_ = "PRESET LOADED  /  " + availablePresets_[static_cast<std::size_t>(index)].name;
                repaint();
            } catch (const std::exception& error) {
                extensionStatus_ = "PRESET ERROR  /  " + juce::String(error.what());
                repaint();
            }
        }
    };
    presetSelector_.setVisible(XSTNSIONS_INSTRUMENT != 0);
    addAndMakeVisible(presetSelector_);

    importButton_.setColour(juce::TextButton::buttonColourId, purple.darker(0.52f));
    importButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    importButton_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Choose an XSTNSIONS extension folder",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& chooser) {
                const auto folder = chooser.getResult();
                if (folder == juce::File{}) return;
                try {
                    extensionStatus_ = "INSTALLED  /  " +
                        juce::String(extensions_->importFolder(folder.getFullPathName().toStdString()));
                    refreshExtensions();
                } catch (const std::exception& error) {
                    extensionStatus_ = "IMPORT FAILED  /  " + juce::String(error.what());
                    extensionLabel_.setText(extensionStatus_, juce::dontSendNotification);
                }
            });
    };
    addAndMakeVisible(importButton_);

    refreshButton_.setColour(juce::TextButton::buttonColourId, panel.brighter(0.18f));
    refreshButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    refreshButton_.setButtonText("REFRESH");
    refreshButton_.onClick = [this] { refreshExtensions(); };
    addAndMakeVisible(refreshButton_);

    refreshExtensions();
}

XSTNSIONSEditor::~XSTNSIONSEditor() {
    processor_.registerEditor(nullptr);
}

void XSTNSIONSEditor::refreshExtensions() {
    try {
        const auto packages = extensions_->scan();
        availablePresets_ = processor_.availablePresets();
        suppressPresetCallback_ = true;
        presetSelector_.clear(juce::dontSendNotification);
        presetSelector_.setTextWhenNothingSelected("SELECT A PRESET");
        for (std::size_t index = 0; index < availablePresets_.size(); ++index)
            presetSelector_.addItem(availablePresets_[index].name, static_cast<int>(index) + 1);
        suppressPresetCallback_ = false;

        juce::String status = juce::String(packages.size()) + " EXTENSION" +
            (packages.size() == 1 ? "" : "S") + " INSTALLED";
        if (!packages.empty()) {
            status += "    /    ";
            for (std::size_t i = 0; i < packages.size() && i < 4; ++i) {
                if (i != 0) status += "   ·   ";
                status += juce::String(packages[i].name);
            }
            if (packages.size() > 4)
                status += "   ·   +" + juce::String(static_cast<int>(packages.size() - 4)) + " MORE";
        } else if (extensionStatus_.isEmpty()) {
            status += "    /    IMPORT PRESETS OR SAMPLE PACKS TO GET STARTED";
        }
        extensionLabel_.setText(extensionStatus_.isNotEmpty()
                                     ? extensionStatus_ + "     /     " + status
                                     : status,
                                 juce::dontSendNotification);
    } catch (const std::exception& error) {
        suppressPresetCallback_ = false;
        extensionLabel_.setText("EXTENSION LIBRARY ERROR  /  " + juce::String(error.what()),
                                juce::dontSendNotification);
    }
}

void XSTNSIONSEditor::paint(juce::Graphics& g) {
    g.fillAll(background);
    g.setColour(juce::Colour(0xff171923));
    g.fillRoundedRectangle(24.0f, 111.0f, static_cast<float>(getWidth() - 48), 300.0f, 18.0f);
    g.setColour(juce::Colour(0xff272333));
    g.drawRoundedRectangle(24.0f, 111.0f, static_cast<float>(getWidth() - 48), 300.0f, 18.0f, 1.0f);
    g.setColour(purple.withAlpha(0.18f));
    g.fillEllipse(static_cast<float>(getWidth() - 161), 19.0f, 105.0f, 105.0f);
    g.setColour(juce::Colour(0xff21222d));
    g.fillRoundedRectangle(33.0f, 430.0f, static_cast<float>(getWidth() - 66), 112.0f, 14.0f);
    g.setColour(juce::Colour(0xff2d2d39));
    g.drawRoundedRectangle(33.0f, 430.0f, static_cast<float>(getWidth() - 66), 112.0f, 14.0f, 1.0f);
    g.setColour(purple);
    g.setFont(11.0f);
    g.drawText("EXTENSION LIBRARY", 54, 446, 190, 18, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(10.0f);
    g.drawText("LOCAL PACKAGES  /  SAFE IMPORT  /  PRIVATE BY DEFAULT",
               54, 466, getWidth() - 105, 15, juce::Justification::centredLeft);
    if (extensionStatus_.isNotEmpty()) {
        g.setColour(extensionStatus_.startsWith("IMPORT FAILED")
                        ? juce::Colour(0xffff8888) : juce::Colour(0xffbdb1db));
        g.setFont(9.0f);
        g.drawText(extensionStatus_, 54, 511, getWidth() - 108, 17, juce::Justification::centredLeft);
    }
}

void XSTNSIONSEditor::resized() {
    title_.setBounds(34, 20, 350, 39);
    subtitle_.setBounds(37, 61, 390, 20);
    constexpr int left = 48;
    constexpr int gap = 28;
    const int size = juce::jlimit(92, 124, (getWidth() - 2 * left - 4 * gap) / 5);
    static constexpr std::array<const char*, 5> labels{{
        "LEVEL", "TONE", "RESONANCE", "ECHO", "SHIMMER"}};
    std::array<juce::Slider*, 5> sliders{{
        &volume_, &tone_, &resonance_, &echo_, &shimmer_}};
    for (std::size_t i = 0; i < sliders.size(); ++i) {
        const int x = left + static_cast<int>(i) * (size + gap);
        sliders[i]->setBounds(x, 184, size, 142);
        controlLabels_[i].setText(labels[i], juce::dontSendNotification);
        controlLabels_[i].setFont(juce::Font(10.0f, juce::Font::bold));
        controlLabels_[i].setColour(juce::Label::textColourId, muted);
        controlLabels_[i].setJustificationType(juce::Justification::centred);
        controlLabels_[i].setInterceptsMouseClicks(false, false);
        addAndMakeVisible(controlLabels_[i]);
        controlLabels_[i].setBounds(x, 174, size, 16);
    }
    if (XSTNSIONS_INSTRUMENT) {
        waveform_.setBounds(54, 351, 138, 35);
        presetSelector_.setBounds(207, 351, 203, 35);
    } else {
        waveform_.setBounds(54, 351, 138, 35);
        presetSelector_.setBounds(207, 351, 203, 35);
    }
    importButton_.setBounds(getWidth() - 246, 446, 190, 39);
    refreshButton_.setBounds(getWidth() - 246, 491, 190, 32);
    extensionLabel_.setBounds(54, 494, getWidth() - 108, 27);
}
