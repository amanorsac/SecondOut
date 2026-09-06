#pragma once

#include "Theme.h"

/** 65x38 rounded button with a circular-refresh icon + "RESCAN". */
class RescanButton : public juce::Button
{
public:
    RescanButton() : juce::Button ("Rescan") {}

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? theme::track : highlighted ? theme::surface2 : theme::control);
        g.fillRoundedRectangle (b, theme::cornerRadius);
        g.setColour (theme::hairline);
        g.drawRoundedRectangle (b, theme::cornerRadius, 1.0f);

        const float cy = b.getCentreY(), r = 6.0f;
        const float cx = b.getX() + 17.0f;
        juce::Path p;
        p.addArc (cx - r, cy - r, r * 2.0f, r * 2.0f,
                  juce::MathConstants<float>::pi * 0.15f,
                  juce::MathConstants<float>::pi * 1.85f, true);
        g.setColour (juce::Colour (0xff8995a4));
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        const float ax = cx + r * std::sin (juce::MathConstants<float>::pi * 0.15f);
        const float ay = cy - r * std::cos (juce::MathConstants<float>::pi * 0.15f);
        juce::Path head;
        head.addTriangle (ax - 2.2f, ay - 3.2f, ax + 3.2f, ay - 0.8f, ax - 0.6f, ay + 2.8f);
        g.fillPath (head);

        auto f = theme::uiFont (8.0f, true);
        f.setExtraKerningFactor (0.06f);
        g.setFont (f);
        g.setColour (theme::textPrimary);
        g.drawText ("RESCAN", getLocalBounds().withTrimmedLeft (28), juce::Justification::centred);
    }
};
