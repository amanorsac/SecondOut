#pragma once

#include "Theme.h"

/** Three-column diagnostics strip (Buffer / Drift Trim / Underruns) with a
    Details toggle that reveals an extra technical row. 408x72 collapsed,
    geometry matches design-spec (dividers at x=136/272 within the card). */
class DiagnosticsPanel : public juce::Component
{
public:
    std::function<void (bool)> onDetailsToggled;

    static constexpr int collapsedHeight = 72;
    static constexpr int expandedHeight  = 96;

    void setValues (const juce::String& bufferIn, const juce::String& trimIn,
                    const juce::String& underrunsIn, bool underrunsAreFault,
                    const juce::String& detailIn)
    {
        buffer = bufferIn;
        trim = trimIn;
        underruns = underrunsIn;
        underrunsFault = underrunsAreFault;
        detailLine = detailIn;
        repaint();
    }

    bool isExpanded() const noexcept { return expanded; }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (detailsHitArea.contains (e.getPosition()))
        {
            expanded = ! expanded;
            if (onDetailsToggled)
                onDetailsToggled (expanded);
            repaint();
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (detailsHitArea.contains (e.getPosition())
                            ? juce::MouseCursor::PointingHandCursor
                            : juce::MouseCursor::NormalCursor);
    }

    void paint (juce::Graphics& g) override
    {
        const auto card = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (theme::surface1);
        g.fillRoundedRectangle (card, 11.0f);
        g.setColour (juce::Colour (0xff2d3540));
        g.drawRoundedRectangle (card, 11.0f, 1.0f);

        // Column dividers (relative to card, from y=14 to y=58).
        g.setColour (juce::Colour (0xff29313a));
        g.fillRect (juce::Rectangle<int> (136, 14, 1, 44));
        g.fillRect (juce::Rectangle<int> (272, 14, 1, 44));

        auto metricLabel = theme::uiFont (7.0f);
        auto metricValue = theme::uiFont (14.0f, true);

        auto drawStat = [&] (int x, const char* label, const juce::String& value, juce::Colour valueColour)
        {
            g.setFont (metricLabel);
            g.setColour (juce::Colour (0xff737f8e));
            g.drawText (label, x, 15, 120, 10, juce::Justification::bottomLeft);
            g.setFont (metricValue);
            g.setColour (valueColour);
            g.drawText (value, x, 34, 130, 16, juce::Justification::bottomLeft);
        };

        drawStat (13, "BUFFER", buffer, theme::textPrimary);
        drawStat (149, "DRIFT TRIM", trim, theme::textPrimary);
        drawStat (285, "UNDERRUNS", underruns, underrunsFault ? theme::fault : theme::textPrimary);

        detailsHitArea = { getWidth() - 70, 30, 57, 20 };
        auto btn = theme::uiFont (8.0f, true);
        btn.setExtraKerningFactor (0.06f);
        g.setFont (btn);
        g.setColour (juce::Colour (0xff9aa5b3));
        g.drawText (juce::String ("DETAILS ") + (expanded ? "^" : "v"),
                    detailsHitArea, juce::Justification::centredRight);

        if (expanded)
        {
            g.setColour (juce::Colour (0xff29313a));
            g.fillRect (juce::Rectangle<int> (13, collapsedHeight - 2, getWidth() - 26, 1));
            g.setFont (theme::uiFont (9.5f));
            g.setColour (theme::textMuted);
            g.drawText (detailLine, 13, collapsedHeight + 4, getWidth() - 26, 18,
                        juce::Justification::centredLeft);
        }
    }

private:
    juce::String buffer { "--" }, trim { "--" }, underruns { "--" }, detailLine;
    bool underrunsFault = false;
    bool expanded = false;
    juce::Rectangle<int> detailsHitArea;
};
