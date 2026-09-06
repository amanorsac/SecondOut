#include "SecondaryDeviceManager.h"

namespace
{
    constexpr int maxRingCapacityFrames = 131072;   // >2.7s @ 48k - hard allocation cap
    constexpr double effectiveBufferSeconds = 0.4;  // working headroom (drift + jitter)
    // The shared-mode device type every interface shows up under. CoreAudio
    // already exposes each interface's native channel layout directly (the
    // thing ASIO exists to provide on Windows), and its HAL mixes clients
    // like WASAPI shared mode does - so the multi-instance behaviour described
    // in the header holds on both platforms.
   #if JUCE_WINDOWS
    const char* const primaryTypeName = "Windows Audio";
   #elif JUCE_MAC
    const char* const primaryTypeName = "CoreAudio";
   #else
    const char* const primaryTypeName = "ALSA";
   #endif
    const char* const asioTypeName = "ASIO";
    const char* const asioPrefix = "ASIO: ";

    // ASIO device names are prefixed for display/selection so the UI can warn
    // about them and so performSelect() knows which JUCE device type to use -
    // the prefix is stripped again before anything touches JUCE/the driver.
    // ASIO is Windows-only, so elsewhere no name ever carries the prefix.
   #if JUCE_WINDOWS
    bool isAsioName (const juce::String& name)                { return name.startsWith (asioPrefix); }
   #else
    bool isAsioName (const juce::String&)                     { return false; }
   #endif
    juce::String stripAsioPrefix (const juce::String& name)    { return isAsioName (name) ? name.substring (juce::String (asioPrefix).length()) : name; }
}

SecondaryDeviceManager::SecondaryDeviceManager()
    : juce::Thread ("SecondOut device switch")
{
    ring.prepare (maxRingCapacityFrames);
    setDawSampleRate (48000.0);

    // Note: don't touch device types here - enumerating them triggers a device
    // scan, and constructors run during host plugin-scanning. See performSelect().
    deviceManager.addChangeListener (this);

    startThread (juce::Thread::Priority::normal);
}

SecondaryDeviceManager::~SecondaryDeviceManager()
{
    deviceManager.removeChangeListener (this);

    // Wake the switch thread so it can see threadShouldExit() and stop; it may
    // currently be mid-switch, in which case stopThread waits for that to finish
    // before returning - safe here since teardown is expected to block briefly.
    switchRequestedEvent.signal();
    stopThread (5000);

    performClose();
}

//==============================================================================
juce::StringArray SecondaryDeviceManager::getOutputDeviceNames (bool rescan)
{
    // Never block the message thread on this: if a switch is in progress,
    // fall back to the last known list rather than waiting on the lock.
    const juce::GenericScopedTryLock<juce::CriticalSection> sl (deviceManagerLock);
    if (! sl.isLocked())
    {
        const juce::ScopedLock cl (deviceListCacheLock);
        return deviceListCache;
    }

    // WASAPI shared-mode devices are listed as-is; ASIO devices are prefixed
    // so the UI can tell them apart and warn about exclusive access. ASIO
    // devices are listed even when a scan isn't requested - scanForDevices()
    // is what's potentially slow (and only called on an explicit Rescan), but
    // getDeviceNames() on an already-known type list is just returning names
    // JUCE already has, which is cheap.
    juce::StringArray combined;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (type->getTypeName() == primaryTypeName)
        {
            if (rescan)
                type->scanForDevices();
            combined.addArray (type->getDeviceNames (false));
        }
       #if JUCE_WINDOWS
        else if (type->getTypeName() == asioTypeName)
        {
            if (rescan)
                type->scanForDevices();
            for (auto& name : type->getDeviceNames (false))
                combined.add (asioPrefix + name);
        }
       #endif
    }

    const juce::ScopedLock cl (deviceListCacheLock);
    deviceListCache = combined;
    return combined;
}

bool SecondaryDeviceManager::isAsioDeviceName (const juce::String& deviceName)
{
    return isAsioName (deviceName);
}

juce::String SecondaryDeviceManager::getSelectedDeviceName() const
{
    const juce::ScopedLock sl (selectedNameLock);
    return selectedDeviceNameCache;
}

void SecondaryDeviceManager::selectDevice (const juce::String& deviceName, int pairIndex)
{
    {
        const juce::ScopedLock sl (selectedNameLock);
        selectedDeviceNameCache = deviceName;
    }
    {
        const juce::ScopedLock sl (requestLock);
        requestedDeviceName = deviceName;
        requestedPairIndex = juce::jmax (0, pairIndex);
    }

    channelPairIndex.store (juce::jmax (0, pairIndex), std::memory_order_relaxed);
    status.store (deviceName.isEmpty() ? Status::idle : Status::prefilling, std::memory_order_relaxed);
    {
        const juce::ScopedLock sl (errorLock);
        lastError.clear();
    }

    requestGeneration.fetch_add (1, std::memory_order_relaxed);
    switchRequestedEvent.signal();
}

void SecondaryDeviceManager::setChannelPair (int pairIndex)
{
    const auto name = getSelectedDeviceName();
    if (name.isNotEmpty())
        selectDevice (name, pairIndex);
}

void SecondaryDeviceManager::closeDevice()
{
    selectDevice ({});
}

//==============================================================================
// Switch thread
void SecondaryDeviceManager::run()
{
    while (! threadShouldExit())
    {
        switchRequestedEvent.wait (-1);
        if (threadShouldExit())
            break;

        // Coalesce: if another request arrives while we're mid-switch, loop
        // again immediately with the newest one instead of stacking work.
        for (;;)
        {
            juce::String name;
            int pairIndex = 0;
            {
                const juce::ScopedLock sl (requestLock);
                name = requestedDeviceName;
                pairIndex = requestedPairIndex;
            }
            const int generationAtStart = requestGeneration.load (std::memory_order_relaxed);

            if (name.isEmpty())
                performClose();
            else
                performSelect (name, pairIndex);

            if (threadShouldExit())
                return;

            if (requestGeneration.load (std::memory_order_relaxed) == generationAtStart)
                break;   // nothing newer arrived while we worked - done
        }
    }
}

void SecondaryDeviceManager::performSelect (const juce::String& requestedName, int pairIndex)
{
    const juce::ScopedLock sl (deviceManagerLock);

    if (callbackRegistered.exchange (false))
        deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();

    // ASIO device names carry a display prefix (added in getOutputDeviceNames)
    // so the UI can warn about them; strip it before touching JUCE/the driver.
    const bool useAsio = isAsioName (requestedName);
    const juce::String deviceName = stripAsioPrefix (requestedName);
    const char* const typeName = useAsio ? asioTypeName : primaryTypeName;

    // getAvailableDeviceTypes() creates the type list; without it the
    // setCurrentAudioDeviceType call silently does nothing.
    auto& types = deviceManager.getAvailableDeviceTypes();
    deviceManager.setCurrentAudioDeviceType (typeName, true);

    // Probe the device's total output channel count without starting a
    // stream (createDevice() queries the endpoint's format but never calls
    // IAudioClient::Initialize/Start / ASIOInit), so a specific stereo pair
    // beyond the default can be requested on the one real open below.
    //
    // For WASAPI this is capped by whatever channel count Windows currently
    // has configured as the endpoint's shared-mode default format (Sound
    // Settings -> device Properties -> Advanced) - a device can report far
    // fewer channels here than it's physically capable of if that format
    // hasn't been set to a wider one, and any per-channel routing beyond
    // that is up to the interface's own control panel (e.g. Focusrite
    // Control's patch matrix). ASIO instead exposes the driver's native
    // channel count and layout directly - "ASIO channel N" reliably means
    // physical output N with no extra software routing step in between.
    int totalChannels = 2;
    for (auto* type : types)
    {
        if (type->getTypeName() == typeName)
        {
            std::unique_ptr<juce::AudioIODevice> probe (type->createDevice (deviceName, {}));
            if (probe != nullptr)
                totalChannels = juce::jmax (2, probe->getOutputChannelNames().size());
            break;
        }
    }

    // Multi-channel interfaces (e.g. an 18-output audio interface) are opened
    // at their full width, with only this one stereo pair "active" via the
    // bitmask - WASAPI shared mode mixes every connected client together, so
    // another SecondOut instance can claim a different, non-overlapping pair
    // on the SAME device and both stream independently at once. (ASIO is
    // single-client: a second SecondOut instance can still claim a different
    // pair on the same ASIO device only if the driver itself supports more
    // than one open channel subset per client, which varies by driver.)
    const int clampedPair = juce::jlimit (0, juce::jmax (0, (totalChannels / 2) - 1), pairIndex);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.outputDeviceName = deviceName;
    setup.inputDeviceName  = {};
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.outputChannels.setBit (clampedPair * 2);
    setup.outputChannels.setBit (clampedPair * 2 + 1);
    setup.sampleRate = 0.0;   // use the device's preferred rate; we resample
    setup.bufferSize = 0;     // default buffer size

    const auto error = deviceManager.initialise (0, 2, nullptr, false, deviceName, &setup);

    if (error.isNotEmpty() || deviceManager.getCurrentAudioDevice() == nullptr)
    {
        status.store (Status::deviceError, std::memory_order_relaxed);
        deviceSampleRate.store (0.0, std::memory_order_relaxed);
        const juce::ScopedLock el (errorLock);
        lastError = error.isNotEmpty() ? error
                  : useAsio ? juce::String ("Could not open \"" + deviceName + "\" - likely already in exclusive use by another app (e.g. your DAW)")
                            : juce::String ("Could not open \"" + deviceName + "\"");
        return;
    }

    // Trust the now-open device's own channel count (more authoritative than
    // the pre-open probe) and record which pair actually ended up active.
    deviceChannelCount.store (juce::jmax (2, deviceManager.getCurrentAudioDevice()->getOutputChannelNames().size()),
                              std::memory_order_relaxed);
    channelPairIndex.store (clampedPair, std::memory_order_relaxed);

    {
        const juce::ScopedLock el (errorLock);
        lastError.clear();
    }

    needsPrefill.store (true, std::memory_order_relaxed);
    deviceManager.addAudioCallback (this);
    callbackRegistered.store (true, std::memory_order_relaxed);
}

void SecondaryDeviceManager::performClose()
{
    const juce::ScopedLock sl (deviceManagerLock);

    if (callbackRegistered.exchange (false))
        deviceManager.removeAudioCallback (this);

    deviceManager.closeAudioDevice();
    status.store (Status::idle, std::memory_order_relaxed);
    deviceSampleRate.store (0.0, std::memory_order_relaxed);
    currentTrimPpm.store (0.0, std::memory_order_relaxed);
    deviceChannelCount.store (0, std::memory_order_relaxed);
}

void SecondaryDeviceManager::setDawSampleRate (double newRate) noexcept
{
    // Hosts call prepareToPlay on every transport start; only an actual rate
    // change should restart the stream (avoids a click/re-prefill per play press).
    if (newRate > 0.0 && newRate != dawSampleRate.load (std::memory_order_relaxed))
    {
        dawSampleRate.store (newRate, std::memory_order_relaxed);
        const int frames = juce::jmin (maxRingCapacityFrames,
                                       static_cast<int> (newRate * effectiveBufferSeconds));
        effectiveCapacityFrames.store (frames, std::memory_order_relaxed);
        needsFlush.store (true, std::memory_order_relaxed);
        needsPrefill.store (true, std::memory_order_relaxed);
    }
}

int SecondaryDeviceManager::getEffectiveCapacity() const noexcept
{
    const int c = effectiveCapacityFrames.load (std::memory_order_relaxed);
    return c > 0 ? c : 1;
}

float SecondaryDeviceManager::getBufferFillRatio() const noexcept
{
    return juce::jlimit (0.0f, 1.0f,
                         static_cast<float> (ring.getNumReady())
                       / static_cast<float> (getEffectiveCapacity()));
}

juce::String SecondaryDeviceManager::getLastError() const
{
    const juce::ScopedLock sl (errorLock);
    return lastError;
}

//==============================================================================
// Host audio thread
void SecondaryDeviceManager::pushAudio (const float* left, const float* right, int numFrames) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return;

    const auto s = status.load (std::memory_order_relaxed);
    if (s == Status::idle || s == Status::deviceError)
        return;

    // If the consumer is gone (disconnected) the buffer will fill up and
    // write() simply drops the excess - nothing to do here.
    ring.write (left, right, numFrames);
}

//==============================================================================
// Device B callback thread
void SecondaryDeviceManager::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double devRate = device->getCurrentSampleRate();
    deviceSampleRate.store (devRate, std::memory_order_relaxed);

    if (devRate <= 0.0)
    {
        {
            const juce::ScopedLock sl (errorLock);
            lastError = "Device reported an invalid sample rate";
        }
        status.store (Status::deviceError, std::memory_order_relaxed);
        return;
    }

    resampler.prepare (dawSampleRate.load (std::memory_order_relaxed) / devRate);

    DriftCorrector::Config cfg;
    drift.prepare (cfg, devRate);

    ring.flushFromConsumer();
    needsPrefill.store (true, std::memory_order_relaxed);
    status.store (Status::prefilling, std::memory_order_relaxed);
}

void SecondaryDeviceManager::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                               float* const* outputChannelData,
                                                               int numOutputChannels,
                                                               int numSamples,
                                                               const juce::AudioIODeviceCallbackContext&)
{
    auto clearOutputs = [&]
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
    };

    if (numOutputChannels < 1
        || outputChannelData[0] == nullptr
        || deviceSampleRate.load (std::memory_order_relaxed) <= 0.0)
    {
        clearOutputs();
        return;
    }

    if (! enabled.load (std::memory_order_relaxed))
    {
        // Disabled (bypassed): make that visible in status rather than leaving
        // a stale "streaming"/"prefilling" reading, and drop buffered audio so
        // re-enabling doesn't play a stale tail (see setEnabled()'s needsFlush).
        clearOutputs();
        status.store (Status::bypassed, std::memory_order_relaxed);
        return;
    }

    const int effectiveCapacity = getEffectiveCapacity();

    // If the producer overfilled while we weren't consuming (e.g. bypass toggled,
    // stall), drop the backlog beyond the effective window so latency stays bounded.
    const int excess = ring.getNumReady() - effectiveCapacity;
    if (excess > 0)
        ring.discard (excess);

    if (needsPrefill.load (std::memory_order_relaxed))
    {
        // A flush is requested when the buffered audio is known-stale (DAW rate
        // change, bypass re-enable) - drop it so prefill gathers fresh audio.
        if (needsFlush.exchange (false, std::memory_order_relaxed))
        {
            ring.flushFromConsumer();
            resampler.reset();
        }

        const int target = effectiveCapacity / 2;
        if (ring.getNumReady() < target)
        {
            status.store (Status::prefilling, std::memory_order_relaxed);
            clearOutputs();
            return;
        }

        resampler.reset();
        resampler.setRatio (dawSampleRate.load (std::memory_order_relaxed)
                          / deviceSampleRate.load (std::memory_order_relaxed));
        drift.reset();
        needsPrefill.store (false, std::memory_order_relaxed);
        status.store (Status::streaming, std::memory_order_relaxed);
    }

    // Drift servo: sample the fill level, nudge the resampling ratio.
    const double fillRatio = static_cast<double> (ring.getNumReady())
                           / static_cast<double> (effectiveCapacity);
    drift.process (fillRatio, numSamples);
    currentTrimPpm.store (drift.getTrimPpm(), std::memory_order_relaxed);

    const double baseRatio = dawSampleRate.load (std::memory_order_relaxed)
                           / deviceSampleRate.load (std::memory_order_relaxed);
    resampler.setRatio (baseRatio * drift.getRatioMultiplier());

    float* outL = outputChannelData[0];
    float* outR = numOutputChannels > 1 ? outputChannelData[1] : nullptr;

    // Render via a stack pair so we can handle mono devices and >2ch devices.
    float tmpL[4096];
    float tmpR[4096];
    int done = 0;

    while (done < numSamples)
    {
        const int block = juce::jmin (numSamples - done, 4096);
        const int rendered = resampler.render (ring, tmpL, tmpR, block);

        for (int i = 0; i < rendered; ++i)
        {
            outL[done + i] = tmpL[i];
            if (outR != nullptr)
                outR[done + i] = tmpR[i];
            else
                outL[done + i] = 0.5f * (tmpL[i] + tmpR[i]);   // mono downmix
        }

        done += rendered;

        if (rendered < block)
        {
            // Underrun: pad with silence and re-prefill.
            for (int i = done; i < numSamples; ++i)
            {
                outL[i] = 0.0f;
                if (outR != nullptr)
                    outR[i] = 0.0f;
            }
            underrunCount.fetch_add (1, std::memory_order_relaxed);
            needsPrefill.store (true, std::memory_order_relaxed);
            status.store (Status::prefilling, std::memory_order_relaxed);
            break;
        }
    }

    // Zero any extra channels beyond stereo.
    for (int ch = 2; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
}

void SecondaryDeviceManager::audioDeviceStopped()
{
    const auto s = status.load (std::memory_order_relaxed);
    if (s == Status::streaming || s == Status::prefilling || s == Status::bypassed)
        status.store (Status::disconnected, std::memory_order_relaxed);
}

void SecondaryDeviceManager::audioDeviceError (const juce::String& errorMessage)
{
    {
        const juce::ScopedLock sl (errorLock);
        lastError = errorMessage;
    }
    status.store (Status::disconnected, std::memory_order_relaxed);
}

void SecondaryDeviceManager::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Device list or device state changed (e.g. USB interface unplugged).
    // Never block the message thread here: if a switch is in progress on the
    // background thread, just skip this check - the switch's own completion
    // will leave status in the right place, and the next change message (or
    // the UI's 10Hz poll noticing a stuck prefill) will catch anything missed.
    const auto s = status.load (std::memory_order_relaxed);
    if (s == Status::streaming || s == Status::prefilling || s == Status::bypassed)
    {
        const juce::GenericScopedTryLock<juce::CriticalSection> sl (deviceManagerLock);
        if (sl.isLocked() && deviceManager.getCurrentAudioDevice() == nullptr)
            status.store (Status::disconnected, std::memory_order_relaxed);
    }

    deviceListChanged.sendChangeMessage();
}
