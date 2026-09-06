#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SecondaryDeviceManager.h"
#include "License/LicenseClient.h"

class SecondOutProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorParameter::Listener
{
public:
    SecondOutProcessor();
    ~SecondOutProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return "SecondOut"; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    SecondaryDeviceManager& getDeviceManager() noexcept      { return secondary; }
    juce::AudioParameterBool* getEnabledParameter() noexcept { return enabledParam; }
    amanorsacstudio::LicenseClient& getLicenseClient() noexcept { return licenseClient; }

private:
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int, bool) override {}

    SecondaryDeviceManager secondary;
    juce::AudioParameterBool* enabledParam = nullptr;
    amanorsacstudio::LicenseClient licenseClient;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SecondOutProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SecondOutProcessor)
};
