#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Design tokens + shared LookAndFeel for the SecondOut interface.
// Source: SecondOut-UX-Design.zip design-spec.md (v0.2.0 evolution).
namespace theme
{
    // Base
    const juce::Colour canvas0     { 0xff0c0f13 };   // window background (bottom of gradient)
    const juce::Colour canvas1     { 0xff101318 };   // window background (top of gradient)
    const juce::Colour surface1    { 0xff151a20 };   // cards / diagnostics
    const juce::Colour surface2    { 0xff1a1f26 };   // hero card highlight
    const juce::Colour control     { 0xff171b21 };   // buttons and selector
    const juce::Colour hairline    { 0xff303843 };   // component edge

    // Text
    const juce::Colour textPrimary   { 0xfff4f7fa };
    const juce::Colour textSecondary { 0xffc5cdd7 };
    const juce::Colour textMuted     { 0xff8e99a7 };
    const juce::Colour textDim       { 0xff56616e };

    // Status
    const juce::Colour healthy { 0xff3fe083 };
    const juce::Colour caution { 0xfff2b84b };
    const juce::Colour fault   { 0xffff5c68 };
    const juce::Colour neutral { 0xff84909f };

    // Aliases used by earlier code paths.
    const juce::Colour background = canvas0;
    const juce::Colour card       = surface1;
    const juce::Colour cardHover  = surface2;
    const juce::Colour border     = hairline;
    const juce::Colour track      = juce::Colour (0xff242a31);
    const juce::Colour text       = textPrimary;
    const juce::Colour textFaint  = textDim;
    const juce::Colour green      = healthy;
    const juce::Colour amber      = caution;
    const juce::Colour red        = fault;
    const juce::Colour grey       = neutral;
    const juce::Colour blue       { 0xff4f9dde };

    const float cornerRadius = 9.0f;

    inline juce::Font uiFont (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions ("Segoe UI", height,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    // Small uppercase section label (e.g. OUTPUT DEVICE, BUFFER HEALTH)
    inline juce::Font labelFont()
    {
        auto f = uiFont (11.0f, true);
        f.setExtraKerningFactor (0.09f);
        return f;
    }
}

/** Styles the stock JUCE ComboBox / PopupMenu to match the design. */
class SecondOutLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SecondOutLookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, theme::control);
        setColour (juce::ComboBox::textColourId, theme::textPrimary);
        setColour (juce::ComboBox::outlineColourId, theme::hairline);
        setColour (juce::ComboBox::arrowColourId, theme::textMuted);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1a1f26));
        setColour (juce::PopupMenu::textColourId, theme::textPrimary);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff262d36));
        setColour (juce::PopupMenu::highlightedTextColourId, theme::textPrimary);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff1c222a));
        setColour (juce::TooltipWindow::textColourId, theme::textPrimary);
        setColour (juce::TooltipWindow::outlineColourId, theme::hairline);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

        g.setColour (theme::control);
        g.fillRoundedRectangle (bounds.reduced (0.5f), theme::cornerRadius);
        g.setColour (box.isPopupActive() ? theme::blue.withAlpha (0.6f) : theme::hairline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), theme::cornerRadius, 1.0f);

        // Speaker icon on the left.
        const auto ic = bounds.removeFromLeft (28.0f).reduced (2.0f, 11.0f);
        g.setColour (juce::Colour (0xff8995a4));
        juce::Path spk;
        const float cx = ic.getX(), cy = ic.getCentreY(), s = ic.getHeight();
        spk.addRoundedRectangle (cx, cy - s * 0.16f, s * 0.28f, s * 0.32f, 1.0f);
        spk.addTriangle (cx + s * 0.12f, cy, cx + s * 0.52f, cy - s * 0.34f, cx + s * 0.52f, cy + s * 0.34f);
        g.fillPath (spk);
        g.drawLine (cx + s * 0.68f, cy - s * 0.18f, cx + s * 0.68f, cy + s * 0.18f, 1.4f);
        g.drawLine (cx + s * 0.86f, cy - s * 0.32f, cx + s * 0.86f, cy + s * 0.32f, 1.4f);

        // Chevron on the right.
        const float ax = (float) width - 20.0f, ay = (float) height * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath (ax - 4.5f, ay - 2.5f);
        arrow.lineTo (ax, ay + 2.5f);
        arrow.lineTo (ax + 4.5f, ay - 2.5f);
        g.setColour (juce::Colour (0xff8995a4));
        g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (28, 1, box.getWidth() - 28 - 22, box.getHeight() - 2);
        label.setFont (theme::uiFont (13.5f));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override      { return theme::uiFont (13.5f); }
    juce::Font getPopupMenuFont() override                     { return theme::uiFont (13.5f); }
};
