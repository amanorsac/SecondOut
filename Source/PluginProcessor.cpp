#include "PluginProcessor.h"
#include "PluginEditor.h"

SecondOutProcessor::SecondOutProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    addParameter (enabledParam = new juce::AudioParameterBool ({ "enabled", 1 }, "Enabled", true));
    enabledParam->addListener (this);
}

SecondOutProcessor::~SecondOutProcessor()
{
    enabledParam->removeListener (this);
}

void SecondOutProcessor::parameterValueChanged (int, float newValue)
{
    secondary.setEnabled (newValue >= 0.5f);
}

//==============================================================================
void SecondOutProcessor::prepareToPlay (double sampleRate, int)
{
    secondary.setDawSampleRate (sampleRate);
    secondary.setEnabled (enabledParam->get());
}

void SecondOutProcessor::releaseResources() {}

bool SecondOutProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Stereo (or mono) in/out, matching — this is a master bus utility.
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SecondOutProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // In a DAW, audio passes through untouched - we only tap a copy. In the
    // standalone there is no host to pass it back to: the feed goes to the
    // chosen second device, and routing live input straight to the default
    // output device is a feedback loop. That is the whole reason JUCE's
    // standalone mutes its input and warns about it; silencing the passthrough
    // instead lets the input stay live with nothing to feed back into.
    // Deliberately outside the license check below, so it holds whether or not
    // this device is activated.
    const bool silenceMainOutput = (wrapperType == wrapperType_Standalone);

    // The tap requires a valid license; isLicensed() is real-time safe (a
    // single atomic read, see LicenseClient.h) so this check costs nothing
    // measurable here. Unlicensed just means the second output silently
    // doesn't stream, same as any other "can't do it right now" state in
    // SecondaryDeviceManager - never a dialog, never touching the DAW's own
    // signal path.
    if (licenseClient.isLicensed())
    {
        const int numFrames = buffer.getNumSamples();
        const float* left  = buffer.getReadPointer (0);
        const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr;

        secondary.pushAudio (left, right, numFrames);
    }

    if (silenceMainOutput)
        buffer.clear();
}

//==============================================================================
juce::AudioProcessorEditor* SecondOutProcessor::createEditor()
{
    return new SecondOutEditor (*this);
}

//==============================================================================
void SecondOutProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("SecondOutState");
    xml.setAttribute ("deviceName", secondary.getSelectedDeviceName());
    xml.setAttribute ("channelPair", secondary.getChannelPairIndex());
    xml.setAttribute ("enabled", enabledParam->get());
    copyXmlToBinary (xml, destData);
}

void SecondOutProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName ("SecondOutState"))
        {
            *enabledParam = xml->getBoolAttribute ("enabled", true);

            const auto deviceName = xml->getStringAttribute ("deviceName");
            const int channelPair = xml->getIntAttribute ("channelPair", 0);
            if (deviceName.isNotEmpty())
            {
                // Device opening must happen on the message thread.
                juce::MessageManager::callAsync (
                    [self = juce::WeakReference<SecondOutProcessor> (this), deviceName, channelPair]
                    {
                        if (self != nullptr)
                            self->secondary.selectDevice (deviceName, channelPair);
                    });
            }
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SecondOutProcessor();
}
