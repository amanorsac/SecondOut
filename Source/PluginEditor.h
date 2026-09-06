#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "UI/Theme.h"
#include "UI/Header.h"
#include "UI/RescanButton.h"
#include "UI/EnableToggle.h"
#include "UI/StatusCard.h"
#include "UI/BufferHealthMeter.h"
#include "UI/DiagnosticsPanel.h"
#include "UI/LicenseGate.h"

class SecondOutEditor : public juce::AudioProcessorEditor,
                        private juce::Timer,
                        private juce::ChangeListener
{
public:
    explicit SecondOutEditor (SecondOutProcessor&);
    ~SecondOutEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshDeviceList (bool rescan);
    void deviceChosen();
    void channelPairChosen();
    void retryDevice();
    void updateStatusCard();
    void refreshChannelPairBox();
    int requiredHeight() const;
    bool channelBoxVisible() const noexcept;
    bool asioWarningVisible() const noexcept;
    int extraRowsHeight() const noexcept;

    SecondOutProcessor& processor;

    SecondOutLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    Header header;
    juce::ComboBox deviceBox;
    RescanButton rescanButton;
    EnableToggle enableToggle;
    juce::ComboBox channelPairBox;
    StatusCard statusCard;
    BufferHealthMeter bufferMeter;
    DiagnosticsPanel diagnostics;

    juce::StringArray knownDevices;
    int lastKnownChannelCount = 0;   // used to detect when to repopulate channelPairBox

    LicenseGate licenseGate;
    bool wasLicensed = false;   // detects the transition so we can resize/relayout once, not every tick
    void updateLicenseGateVisibility();
    static constexpr int gateHeight = 280;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SecondOutEditor)
};
