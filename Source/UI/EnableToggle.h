#pragma once

#include "Theme.h"

/** 53x28 pill toggle. Track colour reflects the CURRENT OPERATIONAL state
    (green while streaming, amber while buffering, dark grey otherwise) rather
    than just the raw enabled flag - the toggle doubles as a status glance.
    ON/OFF caption sits centred below the pill. Can be marked non-interactive
    (e.g. no device selected yet) per design-spec's "disable it until a valid
    device is selected". */
class EnableToggle : public juce::Component,
                     private juce::Timer
{
public:
    std::function<void (bool)> onToggle;

    void setState (bool shouldBeOn, bool animate = true)
    {
        on = shouldBeOn;
        if (! animate)
            pos = on ? 1.0f : 0.0f;
        if (! isTimerRunning())
            startTimerHz (60);
    }

    /** Colour used for the track while ON; ignored while OFF. */
    void setActiveColour (juce::Colour c)
    {
        if (activeColour != c) { activeColour = c; repaint(); }
    }

    void setInteractable (bool shouldBeInteractable)
    {
        if (interactable != shouldBeInteractable)
        {
            interactable = shouldBeInteractable;
            setMouseCursor (interactable ? juce::MouseCursor::PointingHandCursor
                                         : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    bool getState() const noexcept { return on; }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (interactable && getLocalBounds().contains (e.getPosition()))
        {
            setState (! on);
            if (onToggle)
                onToggle (on);
        }
    }

    void paint (juce::Graphics& g) override
    {
        const float h = 28.0f, w = 53.0f;
        const float x0 = (getWidth() - w) * 0.5f;
        auto pill = juce::Rectangle<float> (x0, 0.0f, w, h);

        const auto offTrack = juce::Colour (0xff2a3038);
        const auto onTrack = on ? activeColour : offTrack;
        const auto trackCol = offTrack.interpolatedWith (onTrack, pos);
        const float alpha = interactable ? 1.0f : 0.45f;

        g.setColour (trackCol.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (pill, h * 0.5f);

        const float tr = 10.0f;
        const float tx = pill.getX() + 4.0f + tr + pos * (w - 8.0f - tr * 2.0f);
        g.setColour (juce::Colour (0xfff7fafc).withMultipliedAlpha (alpha));
        g.fillEllipse (tx - tr, pill.getCentreY() - tr, tr * 2.0f, tr * 2.0f);

        auto tag = theme::uiFont (7.0f, true);
        tag.setExtraKerningFactor (0.1f);
        g.setFont (tag);
        g.setColour (juce::Colour (0xff7f8a97).withMultipliedAlpha (interactable ? 1.0f : 0.7f));
        g.drawText (on ? "ON" : "OFF", getLocalBounds().withTrimmedTop ((int) h + 6),
                    juce::Justification::centredTop);
    }

private:
    void timerCallback() override
    {
        const float target = on ? 1.0f : 0.0f;
        const float step = 0.18f;
        if (std::abs (pos - target) < 0.01f)
        {
            pos = target;
            stopTimer();
        }
        else
        {
            pos += juce::jlimit (-step, step, target - pos);
        }
        repaint();
    }

    bool on = true;
    bool interactable = true;
    float pos = 1.0f;
    juce::Colour activeColour = theme::healthy;
};
