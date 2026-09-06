#include "PluginEditor.h"

namespace
{
    constexpr int editorWidth    = 440;
    constexpr int headerHeight   = 52;
    constexpr int controlsY      = 86;
    constexpr int controlsH      = 38;
    constexpr int heroY          = 142;
    constexpr int heroH          = 96;
    constexpr int healthY        = 250;
    constexpr int healthH        = 74;
    constexpr int diagY          = 336;
    constexpr int channelRowExtra = 46;   // vertical space reserved when the channel-pair picker is shown
    constexpr int asioRowExtra    = 32;   // vertical space reserved for the ASIO exclusivity warning
}

SecondOutEditor::SecondOutEditor (SecondOutProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      header ("v" JucePlugin_VersionString),
      licenseGate (p.getLicenseClient())
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (header);
    addChildComponent (licenseGate);

    deviceBox.setTextWhenNothingSelected ("Choose output device...");
    deviceBox.onChange = [this] { deviceChosen(); };
    addAndMakeVisible (deviceBox);

    rescanButton.onClick = [this] { refreshDeviceList (true); };
    addAndMakeVisible (rescanButton);

    channelPairBox.onChange = [this] { channelPairChosen(); };
    addChildComponent (channelPairBox);   // visibility toggled in resized()

    enableToggle.setState (processor.getEnabledParameter()->get(), false);
    enableToggle.onToggle = [this] (bool on)
    {
        auto* param = processor.getEnabledParameter();
        param->beginChangeGesture();
        *param = on;
        param->endChangeGesture();
    };
    addAndMakeVisible (enableToggle);

    statusCard.onRetry = [this] { retryDevice(); };
    addAndMakeVisible (statusCard);
    addAndMakeVisible (bufferMeter);

    diagnostics.onDetailsToggled = [this] (bool) { setSize (editorWidth, requiredHeight()); };
    addAndMakeVisible (diagnostics);

    processor.getDeviceManager().deviceListChanged.addChangeListener (this);

    refreshDeviceList (false);
    updateStatusCard();
    wasLicensed = processor.getLicenseClient().isLicensed();
    setSize (editorWidth, wasLicensed ? requiredHeight() : gateHeight);
    updateLicenseGateVisibility();
    startTimerHz (10);
}

SecondOutEditor::~SecondOutEditor()
{
    processor.getDeviceManager().deviceListChanged.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

int SecondOutEditor::requiredHeight() const
{
    const int diagH = diagnostics.isExpanded() ? DiagnosticsPanel::expandedHeight
                                               : DiagnosticsPanel::collapsedHeight;
    return diagY + extraRowsHeight() + diagH + 48;
}

bool SecondOutEditor::channelBoxVisible() const noexcept
{
    return processor.getDeviceManager().getDeviceChannelCount() > 2;
}

bool SecondOutEditor::asioWarningVisible() const noexcept
{
    return SecondaryDeviceManager::isAsioDeviceName (processor.getDeviceManager().getSelectedDeviceName());
}

int SecondOutEditor::extraRowsHeight() const noexcept
{
    return (asioWarningVisible() ? asioRowExtra : 0) + (channelBoxVisible() ? channelRowExtra : 0);
}

/**
 * Swaps the whole editor between the license gate and the normal UI. Checked
 * every timer tick (cheap: one atomic read via isLicensed()) so activating -
 * or a license expiring after a re-validation failure - takes effect without
 * needing the plugin window reopened.
 */
void SecondOutEditor::updateLicenseGateVisibility()
{
    const bool licensed = processor.getLicenseClient().isLicensed();

    licenseGate.setVisible (! licensed);
    header.setVisible (licensed);
    deviceBox.setVisible (licensed);
    rescanButton.setVisible (licensed);
    enableToggle.setVisible (licensed);
    statusCard.setVisible (licensed);
    bufferMeter.setVisible (licensed);
    diagnostics.setVisible (licensed);
    if (! licensed)
        channelPairBox.setVisible (false);

    if (licensed != wasLicensed)
    {
        wasLicensed = licensed;
        setSize (editorWidth, licensed ? requiredHeight() : gateHeight);
    }
    resized();
}

//==============================================================================
void SecondOutEditor::refreshDeviceList (bool rescan)
{
    auto& mgr = processor.getDeviceManager();
    knownDevices = mgr.getOutputDeviceNames (rescan);

    deviceBox.clear (juce::dontSendNotification);
    for (int i = 0; i < knownDevices.size(); ++i)
        deviceBox.addItem (knownDevices[i], i + 1);

    const auto current = mgr.getSelectedDeviceName();
    const int idx = knownDevices.indexOf (current);
    if (idx >= 0)
        deviceBox.setSelectedId (idx + 1, juce::dontSendNotification);
}

void SecondOutEditor::deviceChosen()
{
    const auto name = deviceBox.getText();
    if (name.isEmpty())
        return;

    // selectDevice() returns immediately - the actual open happens on a
    // background thread so the UI never freezes while switching. Progress
    // shows up via getStatus()/getLastError() on the next timer tick.
    processor.getDeviceManager().selectDevice (name);
    updateStatusCard();
}

void SecondOutEditor::channelPairChosen()
{
    const int id = channelPairBox.getSelectedId();
    if (id > 0)
        processor.getDeviceManager().setChannelPair (id - 1);
}

/** Channel count is discovered asynchronously (after the switch thread opens
    the device), so this is polled every timer tick. The item list is only
    rebuilt when the count actually changes (avoids flicker/closing an open
    dropdown); the selected id is kept in sync every tick regardless, since a
    same-channel-count device swap still resets which pair is active. */
void SecondOutEditor::refreshChannelPairBox()
{
    auto& mgr = processor.getDeviceManager();
    const int channelCount = mgr.getDeviceChannelCount();

    if (channelCount != lastKnownChannelCount)
    {
        lastKnownChannelCount = channelCount;

        channelPairBox.clear (juce::dontSendNotification);
        const int numPairs = juce::jmax (1, channelCount / 2);
        for (int i = 0; i < numPairs; ++i)
            channelPairBox.addItem ("Outputs " + juce::String (i * 2 + 1) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) + juce::String (i * 2 + 2), i + 1);

        setSize (editorWidth, requiredHeight());
    }

    const int wantId = juce::jlimit (1, juce::jmax (1, channelPairBox.getNumItems()), mgr.getChannelPairIndex() + 1);
    if (channelPairBox.getSelectedId() != wantId)
        channelPairBox.setSelectedId (wantId, juce::dontSendNotification);
}

void SecondOutEditor::retryDevice()
{
    const auto name = deviceBox.getText();
    if (name.isNotEmpty())
    {
        processor.getDeviceManager().selectDevice (name);
        updateStatusCard();
    }
}

void SecondOutEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshDeviceList (false);
}

//==============================================================================
void SecondOutEditor::updateStatusCard()
{
    using Status = SecondaryDeviceManager::Status;
    auto& mgr = processor.getDeviceManager();
    const auto status = mgr.getStatus();

    juce::Colour stateColour = theme::neutral;

    switch (status)
    {
        case Status::idle:
            statusCard.setStatus ("NO DEVICE", "Choose an output device to begin",
                                  "SETUP REQUIRED", "Select a device above", theme::neutral, false, false);
            stateColour = theme::neutral;
            break;
        case Status::prefilling:
            statusCard.setStatus ("BUFFERING", "Building a safe output buffer...",
                                  "PREPARING", "Usually ready in a few seconds", theme::caution, false, false);
            stateColour = theme::caution;
            break;
        case Status::streaming:
            statusCard.setStatus ("STREAMING", "Output is active and healthy",
                                  "LIVE " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " STABLE",
                                  {}, theme::healthy, true, false);
            stateColour = theme::healthy;
            break;
        case Status::bypassed:
            statusCard.setStatus ("OUTPUT OFF", "Enable output to resume the second feed",
                                  "BYPASSED", "Turn Output back on above", theme::neutral, false, false);
            stateColour = theme::neutral;
            break;
        case Status::disconnected:
            statusCard.setStatus ("DISCONNECTED", "The output device stopped responding",
                                  "CONNECTION LOST", "Reconnect the device, then select Retry",
                                  theme::fault, false, true);
            stateColour = theme::fault;
            break;
        case Status::deviceError:
            statusCard.setStatus ("COULD NOT OPEN", "Another app may be using this device",
                                  "DEVICE BUSY", "Close the other app or choose a different output",
                                  theme::fault, false, true);
            stateColour = theme::fault;
            break;
    }

    // Toggle reflects the user's actual armed/bypass intent (the plugin
    // parameter), never the live device status - it must hold whatever the
    // user last clicked. Colour alone is tinted by the current status so it
    // still doubles as a glance indicator without fighting the user's click.
    enableToggle.setActiveColour (stateColour);
    enableToggle.setState (processor.getEnabledParameter()->get());
    enableToggle.setInteractable (status != Status::idle);

    // Compact resampling chip; only shown alongside the streaming state (no
    // room to coexist with the hint/retry region other states use).
    const double dawRate = mgr.getDawSampleRate();
    const double devRate = mgr.getDeviceSampleRate();
    const bool resampling = devRate > 0.0 && std::abs (dawRate - devRate) > 1.0
                         && status == Status::streaming;

    if (resampling)
        statusCard.setWarning ("RESAMPLING",
                               juce::String (dawRate / 1000.0, 1) + " -> "
                             + juce::String (devRate / 1000.0, 1) + " kHz");
    else
        statusCard.setWarning ({}, {});
}

void SecondOutEditor::timerCallback()
{
    updateLicenseGateVisibility();
    if (! processor.getLicenseClient().isLicensed())
        return;

    auto& mgr = processor.getDeviceManager();
    const auto status = mgr.getStatus();

    updateStatusCard();
    refreshChannelPairBox();

    const bool meterActive = status == SecondaryDeviceManager::Status::streaming
                          || status == SecondaryDeviceManager::Status::prefilling;
    const juce::Colour fillColour = status == SecondaryDeviceManager::Status::prefilling
                                        ? theme::caution : theme::healthy;
    bufferMeter.setValue (mgr.getBufferFillRatio(), meterActive, fillColour);

    const double dawRate = mgr.getDawSampleRate();
    const double devRate = mgr.getDeviceSampleRate();
    const double latencyMs = (double) mgr.getBufferFillRatio() * 400.0;

    juce::String detail = "DAW " + juce::String (dawRate / 1000.0, 1) + " kHz";
    if (devRate > 0.0)
        detail << "    Device " << juce::String (devRate / 1000.0, 1) << " kHz"
               << "    Latency ~" << juce::String ((int) latencyMs) << " ms";
    const auto lastError = mgr.getLastError();
    if (lastError.isNotEmpty())
        detail << "    " << lastError;

    const int underruns = mgr.getUnderrunCount();
    const bool active = meterActive;

    diagnostics.setValues (active ? juce::String ((int) (mgr.getBufferFillRatio() * 100.0f)) + "%" : "0%",
                           active ? juce::String (mgr.getTrimPpm(), 1) + " ppm" : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                           juce::String (underruns), underruns > 0, detail);

    // The ASIO warning row (unlike the channel-pair picker) doesn't have its
    // own dedicated refresh step - its visibility depends only on which
    // device is selected, so just catch any required-height drift here.
    const int wantHeight = requiredHeight();
    if (getHeight() != wantHeight)
        setSize (editorWidth, wantHeight);
}

//==============================================================================
void SecondOutEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient bg (theme::canvas1, 0, 0, theme::canvas0, 0, (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();
    g.setColour (theme::hairline.withAlpha (0.6f));
    g.drawRect (getLocalBounds());

    // Section labels above the control row.
    g.setFont (theme::labelFont());
    g.setColour (theme::textMuted);
    g.drawText ("OUTPUT DEVICE", 16, controlsY - 22, 150, 12, juce::Justification::bottomLeft);
    g.drawText ("OUTPUT", getWidth() - 16 - 100, controlsY - 22, 100, 12, juce::Justification::bottomRight);

    // ASIO exclusivity warning, directly under the device row, above anything
    // else that's shown - it must be seen before the user commits to a switch.
    int rowY = controlsY + controlsH + 8;
    if (asioWarningVisible())
    {
        auto warnArea = juce::Rectangle<int> (16, rowY, getWidth() - 32, 22);
        g.setColour (theme::caution.withAlpha (0.12f));
        g.fillRoundedRectangle (warnArea.toFloat(), 5.0f);
        g.setColour (theme::caution.withAlpha (0.4f));
        g.drawRoundedRectangle (warnArea.toFloat().reduced (0.5f), 5.0f, 1.0f);
        g.setColour (theme::caution);
        g.setFont (theme::uiFont (11.0f, true));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\xa0"))
                       + "  ASIO is exclusive - don't pick the same interface your DAW uses",
                    warnArea.reduced (10, 0), juce::Justification::centredLeft);
        rowY += asioRowExtra;
    }

    // Channel-pair picker label, only when a multi-channel device is active.
    if (channelBoxVisible())
        g.drawText ("OUTPUT CHANNELS", 16, rowY + 8, 200, 12, juce::Justification::bottomLeft);

    // Footer tagline, sits just below the diagnostics card.
    const int diagH = diagnostics.isExpanded() ? DiagnosticsPanel::expandedHeight
                                               : DiagnosticsPanel::collapsedHeight;
    const int extra = extraRowsHeight();
    auto footer = theme::uiFont (7.0f);
    footer.setExtraKerningFactor (0.02f);
    g.setFont (footer);
    g.setColour (theme::textDim);
    g.drawText ("SecondOut mirrors your audio to a second output.",
                0, diagY + extra + diagH + 12, getWidth(), 16, juce::Justification::centred);
}

void SecondOutEditor::resized()
{
    licenseGate.setBounds (getLocalBounds());

    header.setBounds (0, 0, getWidth(), headerHeight);

    deviceBox.setBounds (16, controlsY, 270, controlsH);
    rescanButton.setBounds (294, controlsY, 65, controlsH);
    enableToggle.setBounds (371, controlsY + 5, 53, 46);

    int rowY = controlsY + controlsH + 8;
    if (asioWarningVisible())
        rowY += asioRowExtra;

    const bool showChannels = channelBoxVisible();
    channelPairBox.setVisible (showChannels);
    if (showChannels)
        channelPairBox.setBounds (16, rowY + 14, 200, 28);

    const int extra = extraRowsHeight();
    statusCard.setBounds (16, heroY + extra, 408, heroH);
    bufferMeter.setBounds (16, healthY + extra, 408, healthH);

    const int diagH = diagnostics.isExpanded() ? DiagnosticsPanel::expandedHeight
                                               : DiagnosticsPanel::collapsedHeight;
    diagnostics.setBounds (16, diagY + extra, 408, diagH);
}
