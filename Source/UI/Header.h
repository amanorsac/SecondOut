#pragma once

#include "Theme.h"

/** Top bar: product name + "SECOND OUTPUT MONITOR" brand tag, version on the
    right, hairline divider below. Matches design-spec header geometry. */
class Header : public juce::Component
{
public:
    explicit Header (juce::String versionIn) : version (std::move (versionIn)) {}

    void paint (juce::Graphics& g) override
    {
        auto row = getLocalBounds().reduced (16, 0);
        const int baseline = 31;   // y=14 within a group translated by 17 -> 31 in editor space

        g.setFont (theme::uiFont (13.0f, false).withStyle (juce::Font::plain).withHeight (13.0f));
        g.setColour (theme::textPrimary);
        auto pf = theme::uiFont (13.0f);
        pf.setBold (true);
        g.setFont (pf);
        g.drawText ("SecondOut", row.getX(), baseline - 14, 76, 18, juce::Justification::bottomLeft);

        auto brand = theme::labelFont().withHeight (9.0f);
        brand.setExtraKerningFactor (0.135f);
        g.setFont (brand);
        g.setColour (juce::Colour (0xff9aa5b3));
        g.drawText ("SECOND OUTPUT MONITOR", row.getX() + 76, baseline - 12, 220, 16,
                    juce::Justification::bottomLeft);

        g.setFont (theme::uiFont (8.0f));
        g.setColour (juce::Colour (0xff697481));
        g.drawText (version, row, juce::Justification::bottomRight);

        g.setColour (juce::Colour (0xff252c34));
        g.fillRect (juce::Rectangle<int> (16, getHeight() - 1, getWidth() - 32, 1));
    }

private:
    juce::String version;
};
