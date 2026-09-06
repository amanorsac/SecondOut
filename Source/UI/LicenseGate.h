#pragma once

#include "Theme.h"
#include "../License/LicenseClient.h"

/**
    Full-panel "enter your license key" screen, shown instead of the normal
    editor content until this device is licensed (see
    SecondOutProcessor::processBlock() for the actual enforcement - this is
    only the UI half). Matches the rest of SecondOut's dark, calm visual
    language rather than looking like a bolted-on dialog.
*/
class LicenseGate : public juce::Component
{
public:
    explicit LicenseGate (amanorsacstudio::LicenseClient& clientIn)
        : client (clientIn)
    {
        keyBox.setMultiLine (false);
        keyBox.setTextToShowWhenEmpty ("SOUT-XXXX-XXXX-XXXX-XXXX", theme::textFaint);
        keyBox.setFont (theme::uiFont (15.0f));
        keyBox.setColour (juce::TextEditor::backgroundColourId, theme::control);
        keyBox.setColour (juce::TextEditor::outlineColourId, theme::hairline);
        keyBox.setColour (juce::TextEditor::focusedOutlineColourId, theme::blue);
        keyBox.setColour (juce::TextEditor::textColourId, theme::textPrimary);
        keyBox.setJustification (juce::Justification::centred);
        keyBox.onReturnKey = [this] { activate(); };
        addAndMakeVisible (keyBox);

        activateButton.onClick = [this] { activate(); };
        addAndMakeVisible (activateButton);

        addAndMakeVisible (statusLabel);
    }

    void paint (juce::Graphics& g) override
    {
        juce::ColourGradient bg (theme::canvas1, 0, 0, theme::canvas0, 0, (float) getHeight(), false);
        g.setGradientFill (bg);
        g.fillAll();
        g.setColour (theme::hairline.withAlpha (0.6f));
        g.drawRect (getLocalBounds());

        auto area = getLocalBounds().reduced (32, 0);

        auto title = theme::uiFont (22.0f, true);
        g.setFont (title);
        g.setColour (theme::textPrimary);
        g.drawText ("Activate SecondOut", area.withY (48).withHeight (30), juce::Justification::centred);

        g.setFont (theme::uiFont (12.5f));
        g.setColour (theme::textSecondary);
        g.drawText ("Enter the license key from your purchase email to unlock this device.",
                    area.withY (82).withHeight (36), juce::Justification::centred);

        g.setFont (theme::uiFont (10.0f));
        g.setColour (theme::textDim);
        g.drawText ("One license activates a limited number of devices at once - "
                    "deactivate an old machine from within the plugin if you need to move it.",
                    area.withY (getHeight() - 44).withHeight (28), juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (32, 0);
        keyBox.setBounds (area.withY (128).withHeight (38));
        activateButton.setBounds (area.withY (176).withHeight (36));
        statusLabel.setBounds (area.withY (220).withHeight (40));
    }

private:
    void activate()
    {
        const auto key = keyBox.getText().trim();
        if (key.isEmpty())
            return;

        activateButton.setEnabled (false);
        statusLabel.setColour (juce::Label::textColourId, theme::textMuted);
        statusLabel.setText ("Checking...", juce::dontSendNotification);

        client.activate (key, [safe = juce::Component::SafePointer<LicenseGate> (this)] (amanorsacstudio::LicenseResult r)
        {
            auto* self = safe.getComponent();
            if (self == nullptr)
                return;

            self->activateButton.setEnabled (true);
            if (r.ok)
            {
                self->statusLabel.setColour (juce::Label::textColourId, theme::healthy);
                self->statusLabel.setText ("Activated.", juce::dontSendNotification);
            }
            else
            {
                self->statusLabel.setColour (juce::Label::textColourId, theme::fault);
                self->statusLabel.setText (r.message, juce::dontSendNotification);
            }
        });
    }

    amanorsacstudio::LicenseClient& client;

    juce::TextEditor keyBox;
    juce::TextButton activateButton { "Activate" };
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseGate)
};
