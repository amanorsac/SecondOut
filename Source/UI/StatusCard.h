#pragma once

#include "Theme.h"

/** The primary "is my feed OK?" hero card: state rail, glowing LED, large
    state word, subtitle, an operational badge chip, and - depending on
    state - either a hint line, a Retry button, or a low-amplitude activity
    waveform (streaming only). 408x96 per design-spec. */
class StatusCard : public juce::Component,
                   private juce::Timer
{
public:
    std::function<void()> onRetry;

    StatusCard()
    {
        for (auto& b : bars)
            b = 0.3f;

        retryButton.onClick = [this] { if (onRetry) onRetry(); };
        addChildComponent (retryButton);
    }

    void setStatus (const juce::String& titleIn, const juce::String& subtitleIn,
                    const juce::String& badgeIn, const juce::String& hintIn,
                    juce::Colour colourIn, bool animateWaveform, bool showRetry)
    {
        title = titleIn;
        subtitle = subtitleIn;
        badge = badgeIn;
        hint = hintIn;
        colour = colourIn;
        waveformActive = animateWaveform;
        retryButton.setVisible (showRetry);

        if (waveformActive && ! isTimerRunning())
            startTimerHz (24);
        else if (! waveformActive && isTimerRunning())
            stopTimer();

        resized();
        repaint();
    }

    /** Empty string hides the chip. */
    void setWarning (const juce::String& warnTitle, const juce::String& warnDetail)
    {
        warningTitle = warnTitle;
        warningDetail = warnDetail;
        repaint();
    }

    void resized() override
    {
        retryButton.setBounds (getWidth() - 92, 58, 76, 28);
    }

    void paint (juce::Graphics& g) override
    {
        const auto card = getLocalBounds().toFloat().reduced (0.5f);
        juce::ColourGradient grad (theme::surface2, 0, 0, theme::surface1, 0, (float) getHeight(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (card, 11.0f);
        g.setColour (theme::hairline);
        g.drawRoundedRectangle (card, 11.0f, 1.0f);

        // 4px state rail, full height, left edge.
        g.setColour (colour);
        g.fillRoundedRectangle (juce::Rectangle<float> (0, 0, 4.0f, (float) getHeight()), 2.0f);

        // LED: 3-layer glow (soft halo, blurred mid, solid core).
        const float lx = 22.0f, ly = 31.0f;
        g.setColour (colour.withAlpha (0.10f));
        g.fillEllipse (lx - 14.0f, ly - 14.0f, 28.0f, 28.0f);
        {
            juce::Image glowImg (juce::Image::ARGB, 32, 32, true);
            juce::Graphics gg (glowImg);
            gg.setColour (colour.withAlpha (0.55f));
            gg.fillEllipse (7.0f, 7.0f, 18.0f, 18.0f);
            juce::ImageConvolutionKernel kernel (5);
            kernel.createGaussianBlur (2.6f);
            kernel.applyToImage (glowImg, glowImg, glowImg.getBounds());
            g.setOpacity (1.0f);
            g.drawImageAt (glowImg, (int) lx - 16, (int) ly - 16);
        }
        g.setColour (colour);
        g.fillEllipse (lx - 6.0f, ly - 6.0f, 12.0f, 12.0f);

        // State word + subtitle.
        auto stateFont = theme::uiFont (21.0f, true);
        stateFont.setExtraKerningFactor (0.019f);
        g.setFont (stateFont);
        g.setColour (colour);
        g.drawText (title, 46, 22, 240, 22, juce::Justification::bottomLeft);

        g.setFont (theme::uiFont (10.0f));
        g.setColour (theme::textSecondary);
        g.drawText (subtitle, 46, 41, 250, 16, juce::Justification::bottomLeft);

        // Badge chip, top-right.
        if (badge.isNotEmpty())
        {
            juce::Rectangle<float> chip (314.0f, 14.0f, 78.0f, 20.0f);
            g.setColour (colour.withAlpha (0.09f));
            g.fillRoundedRectangle (chip, 6.0f);
            g.setColour (colour.withAlpha (0.30f));
            g.drawRoundedRectangle (chip.reduced (0.5f), 6.0f, 1.0f);
            auto tag = theme::uiFont (7.0f, true);
            tag.setExtraKerningFactor (0.157f);
            g.setFont (tag);
            g.setColour (colour);
            g.drawText (badge, chip, juce::Justification::centred);
        }

        // Content below the subtitle: waveform (streaming) OR hint text
        // (everything else). Retry button, if visible, sits to its right.
        if (waveformActive)
            paintWaveform (g, juce::Rectangle<float> (246.0f, 63.0f, 152.0f, 22.0f));
        else if (hint.isNotEmpty())
        {
            g.setFont (theme::uiFont (8.5f));
            g.setColour (theme::textMuted);
            const int hintWidth = retryButton.isVisible() ? 260 : 360;
            g.drawText (hint, 31, 71, hintWidth, 14, juce::Justification::centredLeft);
        }

        // Resampling chip: bottom-left, only when there's room (no hint/retry
        // showing, i.e. the streaming state).
        if (warningTitle.isNotEmpty() && waveformActive)
            paintWarningChip (g, juce::Rectangle<float> (31.0f, 68.0f, 190.0f, 20.0f));
    }

private:
    void paintWaveform (juce::Graphics& g, juce::Rectangle<float> a)
    {
        const int n = (int) bars.size();
        const float bw = 2.4f;
        const float gap = (a.getWidth() - bw * (float) n) / (float) (n - 1);
        float x = a.getX();
        for (int i = 0; i < n; ++i)
        {
            const float h = juce::jmax (3.0f, bars[(size_t) i] * a.getHeight());
            g.setColour (colour.withAlpha (0.55f + 0.4f * bars[(size_t) i]));
            g.fillRoundedRectangle (x, a.getCentreY() - h * 0.5f, bw, h, 1.2f);
            x += bw + gap;
        }
    }

    void paintWarningChip (juce::Graphics& g, juce::Rectangle<float> a)
    {
        g.setColour (theme::caution.withAlpha (0.12f));
        g.fillRoundedRectangle (a, 6.0f);
        g.setColour (theme::caution.withAlpha (0.4f));
        g.drawRoundedRectangle (a.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (theme::caution);
        g.setFont (theme::uiFont (8.0f, true));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\xa0 ")) + warningTitle + "  " + warningDetail,
                    a.reduced (8.0f, 0.0f), juce::Justification::centredLeft);
    }

    struct RetryButton : public juce::Button
    {
        RetryButton() : juce::Button ("Retry") {}

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (down ? juce::Colour (0xff1c2129) : highlighted ? juce::Colour (0xff2a3038)
                                                                        : juce::Colour (0xff242a32));
            g.fillRoundedRectangle (b, 8.0f);
            g.setColour (juce::Colour (0xff3b4552));
            g.drawRoundedRectangle (b, 8.0f, 1.0f);

            auto f = theme::uiFont (8.0f, true);
            f.setExtraKerningFactor (0.06f);
            g.setFont (f);
            g.setColour (theme::textPrimary);
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\xbb")) + "  RETRY", b, juce::Justification::centred);
        }
    };

    juce::String title { "NO DEVICE" }, subtitle, badge, hint;
    juce::String warningTitle, warningDetail;
    juce::Colour colour { theme::neutral };
    bool waveformActive = false;

    RetryButton retryButton;
    std::array<float, 25> bars;
    float phase = 0.0f;
    juce::Random random;

    void timerCallback() override
    {
        phase += 0.35f;
        for (int i = 0; i < (int) bars.size(); ++i)
        {
            const float target = 0.25f
                + 0.35f * (0.5f + 0.5f * std::sin (phase * 0.6f + (float) i * 0.85f))
                + 0.25f * random.nextFloat();
            auto& b = bars[(size_t) i];
            b += 0.25f * (target - b);
        }
        repaint();
    }
};
