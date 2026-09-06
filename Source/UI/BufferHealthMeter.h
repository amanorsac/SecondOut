#pragma once

#include "Theme.h"

/** Buffer fill meter: single-colour fill that tracks the current operational
    state colour (green while streaming, amber while buffering), a bright
    50% target marker, and restrained 0/50/100 scale labels. 408x74 card
    per design-spec; fill position eases toward its target. */
class BufferHealthMeter : public juce::Component,
                          public juce::SettableTooltipClient,
                          private juce::Timer
{
public:
    BufferHealthMeter()
    {
        setTooltip ("SecondOut keeps this buffer near 50% by nudging its resampling "
                    "ratio a few PPM at a time. Creeping toward empty or full means "
                    "clock drift is outrunning the correction.");
        startTimerHz (30);
    }

    void setValue (float ratio0to1, bool active, juce::Colour fillColourIn)
    {
        target = juce::jlimit (0.0f, 1.0f, ratio0to1);
        isActive = active;
        fillColour = fillColourIn;
    }

    void paint (juce::Graphics& g) override
    {
        const auto card = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (theme::surface1);
        g.fillRoundedRectangle (card, 11.0f);
        g.setColour (juce::Colour (0xff2d3540));
        g.drawRoundedRectangle (card, 11.0f, 1.0f);

        g.setFont (theme::labelFont());
        g.setColour (theme::textMuted);
        g.drawText ("BUFFER HEALTH", 13, 8, 150, 12, juce::Justification::bottomLeft);

        auto micro = theme::uiFont (7.0f);
        g.setFont (micro);
        g.setColour (juce::Colour (0xff737f8e));
        g.drawText ("TARGET 50%", getWidth() - 13 - 150, 8, 150, 12, juce::Justification::bottomRight);

        // Info affordance next to the label.
        const float infoX = 13.0f + juce::GlyphArrangement::getStringWidth (theme::labelFont(), "BUFFER HEALTH") + 6.0f;
        g.setColour (juce::Colour (0xff737f8e));
        g.drawEllipse (infoX, 0.0f, 9.0f, 9.0f, 1.0f);
        g.setFont (theme::uiFont (7.5f, true));
        g.drawText ("i", juce::Rectangle<float> (infoX, 0.0f, 9.0f, 9.0f), juce::Justification::centred);

        // Track.
        const juce::Rectangle<float> track (13.0f, 34.0f, (float) getWidth() - 26.0f, 12.0f);
        g.setColour (juce::Colour (0xff242a31));
        g.fillRoundedRectangle (track, 6.0f);

        // Fill.
        if (isActive && displayed > 0.003f)
        {
            auto fill = track.withWidth (juce::jmax (track.getWidth() * displayed, 12.0f));
            g.setColour (fillColour);
            g.fillRoundedRectangle (fill, 6.0f);
        }

        // Target marker at 50%.
        const float mx = track.getX() + track.getWidth() * 0.5f;
        g.setColour (theme::textPrimary.withAlpha (0.85f));
        g.fillRoundedRectangle (juce::Rectangle<float> (mx - 1.0f, track.getY() - 3.0f, 2.0f, track.getHeight() + 6.0f), 1.0f);

        // Scale labels.
        g.setFont (micro);
        g.setColour (juce::Colour (0xff737f8e));
        g.drawText ("0%", (int) track.getX(), 52, 40, 12, juce::Justification::centredLeft);
        g.drawText ("50%", (int) (mx - 20.0f), 52, 40, 12, juce::Justification::centred);
        g.drawText ("100%", (int) (track.getRight() - 40.0f), 52, 40, 12, juce::Justification::centredRight);
    }

private:
    void timerCallback() override
    {
        const float next = displayed + 0.25f * (target - displayed);
        if (std::abs (next - displayed) > 0.0005f)
        {
            displayed = next;
            repaint();
        }
    }

    float target = 0.0f, displayed = 0.0f;
    bool isActive = false;
    juce::Colour fillColour = theme::healthy;
};
