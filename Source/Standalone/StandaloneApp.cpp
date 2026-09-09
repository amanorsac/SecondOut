// ============================================================================
//  StandaloneApp.cpp -- SecondOut's own standalone application.
//
//  JUCE's stock StandaloneFilterWindow wraps the editor in chrome this product
//  has no use for: an "Options" button in the top-left corner, and a yellow
//  bar reading "Audio input is muted to avoid feedback loop" with its own
//  second "Settings..." button. That is three settings entry points for an app
//  whose only real setting is which device to listen to, and it advertises a
//  problem instead of solving it.
//
//  This replaces the whole thing: the editor fills the window, and there is
//  one gear, in the header, styled like the rest of the interface (company
//  standard: one gear icon, one settings surface).
//
//  The muted input goes away for a real reason rather than by hiding the
//  warning. SecondOutProcessor silences its main output when it is the
//  standalone (see processBlock), because the mirror feed goes to the chosen
//  second device and the default output plays no part in the job -- so live
//  input has nothing to loop back through, and can simply stay live.
//
//  Compiled into the Standalone target ONLY, together with
//  JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1. The VST3 and AU builds never see
//  this file, and the editor itself is untouched by it.
// ============================================================================

// NB: deliberately NOT juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h,
// which JUCE's own wrappers use. It does `#define Component juce::Component`
// (to dodge a Carbon name clash), so every `juce::Component` written after it
// expands to `juce::juce::Component`. Include the modules directly instead.
// JucePlugin_* (e.g. JucePlugin_VersionString) arrive as compile definitions
// on this target from juce_add_plugin - there is no JucePluginDefines.h to
// include under the CMake build.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_plugin_client/detail/juce_CreatePluginFilter.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "../UI/Theme.h"

namespace
{

//==============================================================================
/** The one piece of chrome the standalone adds. Drawn rather than shipped as
    an image so it stays crisp at any scale and needs no binary resource. */
class SettingsButton final : public juce::Button
{
public:
    SettingsButton() : juce::Button ("Audio settings") {}

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const auto centre = bounds.getCentre();
        const float outer = bounds.getWidth() * 0.5f;
        const float body  = outer * 0.60f;

        g.setColour (down        ? theme::textPrimary
                   : highlighted ? theme::textSecondary
                                 : theme::textMuted);

        juce::Path gear;
        constexpr int teeth = 8;

        for (int i = 0; i < teeth; ++i)
        {
            const auto angle = juce::MathConstants<float>::twoPi * (float) i / (float) teeth;

            juce::Path tooth;
            tooth.addRoundedRectangle (-outer * 0.15f, -outer, outer * 0.30f, outer * 0.45f, outer * 0.06f);
            tooth.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));
            gear.addPath (tooth);
        }

        gear.addEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f);
        g.fillPath (gear);

        // Hub punched back out to the window colour, so this still reads as a
        // gear at 20px rather than as a filled blob.
        const float hub = body * 0.42f;
        g.setColour (theme::canvas1);
        g.fillEllipse (centre.x - hub, centre.y - hub, hub * 2.0f, hub * 2.0f);
    }
};

//==============================================================================
/** Applied as the application-wide default so JUCE's audio settings dialog
    inherits the product's palette instead of arriving in stock grey. The
    editor sets its own LookAndFeel on itself, so nothing here can alter how
    the plugin UI is drawn -- in a DAW this class does not exist at all. */
class StandaloneLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    StandaloneLookAndFeel()
    {
        setColourScheme ({ theme::canvas0,     // window background
                           theme::surface1,    // widget background
                           theme::surface2,    // menu background
                           theme::hairline,    // outline
                           theme::textPrimary, // default text
                           theme::blue,        // default fill
                           theme::textPrimary, // highlighted text
                           theme::blue,        // highlighted fill
                           theme::textPrimary  // menu text
                         });

        setColour (juce::ResizableWindow::backgroundColourId, theme::canvas0);
        setColour (juce::ComboBox::backgroundColourId,        theme::control);
        setColour (juce::ComboBox::outlineColourId,           theme::hairline);
        setColour (juce::Label::textColourId,                 theme::textSecondary);
        setColour (juce::TextButton::buttonColourId,          theme::control);
        setColour (juce::TextButton::textColourOffId,         theme::textPrimary);
    }
};

//==============================================================================
class StandaloneContent final : public juce::Component,
                                private juce::ComponentListener
{
public:
    explicit StandaloneContent (juce::StandalonePluginHolder& holderIn)
        : holder (holderIn),
          editor (holder.processor != nullptr && holder.processor->hasEditor()
                      ? holder.processor->createEditorAndMakeActive()
                      : nullptr)
    {
        if (editor != nullptr)
        {
            addAndMakeVisible (*editor);
            editor->addComponentListener (this);
        }

        settingsButton.onClick = [this] { holder.showAudioSettingsDialog(); };
        settingsButton.setTooltip ("Choose which device SecondOut listens to");
        addAndMakeVisible (settingsButton);

        setSize (editor != nullptr ? editor->getWidth()  : 440,
                 editor != nullptr ? editor->getHeight() : 300);
    }

    ~StandaloneContent() override
    {
        if (editor != nullptr)
        {
            editor->removeComponentListener (this);

            // Required by the AudioProcessor/editor contract: the processor
            // caches the editor it handed out and must be told it is going.
            holder.processor->editorBeingDeleted (editor.get());
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::canvas0);
    }

    void resized() override
    {
        if (editor != nullptr)
            editor->setBounds (getLocalBounds());

        // Top-right of the header strip the editor draws, clear of the version
        // string it puts along that strip's bottom edge.
        settingsButton.setBounds (getWidth() - 38, 9, 20, 20);
        settingsButton.toFront (false);
    }

private:
    void componentMovedOrResized (juce::Component& component, bool, bool wasResized) override
    {
        // The editor owns its own size: it grows when the license gate gives
        // way to the full interface, and again when a multi-output device adds
        // the channel-pair row. Follow it here, and the window follows this
        // component (setContentOwned's resizeToFitWhenContentChangesSize).
        if (wasResized && &component == editor.get())
            setSize (editor->getWidth(), editor->getHeight());
    }

    juce::StandalonePluginHolder& holder;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    SettingsButton settingsButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StandaloneContent)
};

//==============================================================================
class StandaloneWindow final : public juce::DocumentWindow
{
public:
    explicit StandaloneWindow (juce::StandalonePluginHolder& holder)
        : juce::DocumentWindow ("SecondOut", theme::canvas0,
                                juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new StandaloneContent (holder), true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
    }

    void closeButtonPressed() override
    {
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StandaloneWindow)
};

//==============================================================================
class SecondOutStandaloneApp final : public juce::JUCEApplication
{
public:
    SecondOutStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = "SecondOut";
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        // Company data-path convention, and the same folder name the license
        // cache already uses - not a bare "SecondOut.settings" in the root of
        // the user's app-data directory the way JUCE's default app does it.
        options.folderName          = "Amanorsac Studio/SecondOut";

        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override    { return "SecondOut"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override           { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        holder = std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(), false);

        // Both of these are safe only because the processor silences the main
        // output in the standalone (see the file header): with nothing routed
        // from input to output there is no loop to mute, so the input stays
        // live and the "Feedback Loop / Mute audio input" row disappears from
        // the settings dialog rather than sitting there as a trap.
        holder->processorHasPotentialFeedbackLoop = false;
        holder->getMuteInputValue() = false;

        mainWindow = std::make_unique<StandaloneWindow> (*holder);
        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        // Window first: its content tells the processor the editor is going.
        mainWindow = nullptr;

        if (holder != nullptr)
            holder->savePluginState();

        holder = nullptr;

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (holder != nullptr)
            holder->savePluginState();

        quit();
    }

private:
    juce::ApplicationProperties appProperties;
    StandaloneLookAndFeel lookAndFeel;
    std::unique_ptr<juce::StandalonePluginHolder> holder;
    std::unique_ptr<StandaloneWindow> mainWindow;
};

} // namespace

JUCE_CREATE_APPLICATION_DEFINE (SecondOutStandaloneApp)
