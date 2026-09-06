#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "RingBuffer.h"
#include "Resampler.h"
#include "DriftCorrector.h"

/**
    Owns the independent output stream to "Device B" - WASAPI shared mode by
    default (or ASIO when the user explicitly picks an ASIO device) on
    Windows, CoreAudio on macOS - and the ring buffer that decouples it from
    the host's audio thread.

    Threads involved:
      - Host audio thread:   pushAudio()             (lock-free, wait-free)
      - Device B callback:   audioDeviceIOCallback   (reads ring, resamples)
      - Message thread:      enumeration, status polling, requesting a switch
      - Switch thread:       actually opens/closes the WASAPI device

    Device open/close (deviceManager.initialise()/closeAudioDevice()) can
    block for hundreds of ms on some WASAPI drivers, and on Windows a blocking
    COM call on an STA thread can pump queued input messages mid-call - which
    can reenter this class while the first call is still in flight. Running
    the switch on its own thread keeps the UI responsive and makes reentrancy
    impossible: the message thread never calls into deviceManager to open or
    close it, only the switch thread does (behind switchLock).

    Multi-channel interfaces (e.g. an 18-output audio interface) are opened at
    their FULL channel width, with only one stereo pair "active" via the
    outputChannels bitmask - which pair is chosen by channelPairIndex. WASAPI
    shared mode mixes every connected client together, so several plugin
    instances can each claim a different, non-overlapping pair on the SAME
    physical device and coexist without conflict - that's what lets multiple
    SecondOut instances send independent mixes out different outputs of one
    interface simultaneously.
*/
class SecondaryDeviceManager : private juce::AudioIODeviceCallback,
                               private juce::ChangeListener,
                               private juce::Thread
{
public:
    enum class Status
    {
        idle,           // no device selected
        prefilling,     // device open, waiting for buffer to reach target fill
        streaming,      // audio flowing
        disconnected,   // device vanished / stopped unexpectedly
        deviceError,    // couldn't open the device
        bypassed        // device open and ready, but output disabled by the user
    };

    SecondaryDeviceManager();
    ~SecondaryDeviceManager() override;

    //==============================================================================
    // Message thread API
    juce::StringArray getOutputDeviceNames (bool rescan);

    /** Name the UI should show as selected - updated immediately (optimistically)
        when selectDevice() is called, independent of whether the switch has
        actually completed yet. */
    juce::String getSelectedDeviceName() const;

    /** Requests the named device be opened (or, if empty, the device closed) on
        the given stereo output-channel pair (0 = channels 1-2, 1 = channels
        3-4, ...). Returns immediately - the actual open/close happens on a
        background thread. Progress is reflected via getStatus()/getLastError().
        Calling this again before a previous request finishes simply replaces
        the pending request; only the latest wins. */
    void selectDevice (const juce::String& deviceName, int pairIndex = 0);

    /** Re-opens the currently selected device on a different output-channel
        pair, without touching the device name. */
    void setChannelPair (int pairIndex);

    void closeDevice();

    /** True if the given device name (as returned by getOutputDeviceNames()
        or getSelectedDeviceName()) is an ASIO device. ASIO is single-client:
        selecting the same interface the host DAW is already using via ASIO
        will fail to open (or, on some drivers, could disrupt the DAW's own
        connection) - the UI uses this to warn before that happens. */
    static bool isAsioDeviceName (const juce::String& deviceName);

    /** Total output channels the currently open (or last opened) device
        reports, and the stereo pair currently active on it. Together these
        let the UI build an "Outputs 1-2 / 3-4 / ..." picker; both are 0/2
        until a device has actually been opened at least once. */
    int getDeviceChannelCount() const noexcept  { return deviceChannelCount.load (std::memory_order_relaxed); }
    int getChannelPairIndex() const noexcept    { return channelPairIndex.load (std::memory_order_relaxed); }

    void setEnabled (bool shouldBeEnabled) noexcept
    {
        const bool wasEnabled = enabled.exchange (shouldBeEnabled, std::memory_order_relaxed);
        if (shouldBeEnabled && ! wasEnabled)
        {
            // Re-enable: drop the stale tail frozen in the buffer and restart cleanly.
            needsFlush.store (true, std::memory_order_relaxed);
            needsPrefill.store (true, std::memory_order_relaxed);
        }
    }
    bool isEnabled() const noexcept                 { return enabled.load (std::memory_order_relaxed); }

    /** Called from prepareToPlay. Real-time safe w.r.t. the consumer thread. */
    void setDawSampleRate (double newRate) noexcept;

    //==============================================================================
    // Host audio thread API
    void pushAudio (const float* left, const float* right, int numFrames) noexcept;

    //==============================================================================
    // UI polling (any thread)
    Status getStatus() const noexcept          { return status.load (std::memory_order_relaxed); }
    float getBufferFillRatio() const noexcept;
    double getDeviceSampleRate() const noexcept { return deviceSampleRate.load (std::memory_order_relaxed); }
    double getDawSampleRate() const noexcept    { return dawSampleRate.load (std::memory_order_relaxed); }
    double getTrimPpm() const noexcept          { return currentTrimPpm.load (std::memory_order_relaxed); }
    int getUnderrunCount() const noexcept       { return underrunCount.load (std::memory_order_relaxed); }
    juce::String getLastError() const;

    /** Listeners are notified on the message thread when the device list or
        device state may have changed. */
    juce::ChangeBroadcaster deviceListChanged;

private:
    //==============================================================================
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& errorMessage) override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    // juce::Thread - the switch worker.
    void run() override;

    void performClose();                              // switch thread only (or dtor, after stopThread)
    void performSelect (const juce::String& name, int pairIndex);   // switch thread only

    int getEffectiveCapacity() const noexcept;

    //==============================================================================
    juce::CriticalSection deviceManagerLock;   // guards all deviceManager access
    juce::AudioDeviceManager deviceManager;

    // Switch request queue-of-one: the latest request always wins.
    juce::WaitableEvent switchRequestedEvent;
    juce::CriticalSection requestLock;
    juce::String requestedDeviceName;
    int requestedPairIndex = 0;
    std::atomic<int> requestGeneration { 0 };

    // What the UI should show as selected; set optimistically by selectDevice()
    // so the combo box updates instantly without touching deviceManager.
    mutable juce::CriticalSection selectedNameLock;
    juce::String selectedDeviceNameCache;

    mutable juce::CriticalSection deviceListCacheLock;
    juce::StringArray deviceListCache;

    std::atomic<int> deviceChannelCount { 0 };   // total output channels of the last-opened device
    std::atomic<int> channelPairIndex   { 0 };   // 0-based: pair N = channels [2N, 2N+1]

    RingBuffer ring;
    Resampler resampler;
    DriftCorrector drift;

    std::atomic<Status> status { Status::idle };
    std::atomic<bool> enabled { true };
    std::atomic<bool> callbackRegistered { false };

    std::atomic<double> dawSampleRate    { 0.0 };   // 0 = not yet set; first setDawSampleRate always applies
    std::atomic<double> deviceSampleRate { 0.0 };
    std::atomic<double> currentTrimPpm   { 0.0 };
    std::atomic<int>    underrunCount    { 0 };
    std::atomic<int>    effectiveCapacityFrames { 0 };
    std::atomic<bool>   needsPrefill { true };
    std::atomic<bool>   needsFlush   { false };

    mutable juce::CriticalSection errorLock;
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SecondaryDeviceManager)
};
