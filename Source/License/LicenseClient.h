// ============================================================================
//  LicenseClient.h -- SecondOut's client for the Amanorsac licensing service
//  (store-backend/src/routes/licenses.js). Adapted from PerformLive's
//  StoreClient.h, but much simpler: no accounts, no sessions, no downloads --
//  a plugin purchase should not require creating an account. The license key
//  itself is the whole credential.
//
//  Three decisions carried over from StoreClient.h, for the same reasons:
//
//  1. EVERY NETWORK CALL IS ASYNCHRONOUS, off the message thread. This runs
//     inside a DAW; a synchronous HTTP call from the message thread is a UI
//     freeze waiting for bad wifi (see the SecondOut device-switch freeze
//     fix earlier in this project for exactly this class of bug).
//
//  2. THE PLUGIN STAYS USABLE OFFLINE. A signed activation proof, cached
//     locally and verified against the key compiled into the binary, is what
//     says "this device is licensed" with no connection. A studio with no
//     internet must still be able to open a session using this plugin.
//
//  3. FAILURE IS ALWAYS A MESSAGE, NEVER A CRASH OR A LIE. Callbacks carry a
//     WeakReference so one that outlives this object is dropped silently.
// ============================================================================
#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "LicenseCrypto.h"

#include <atomic>
#include <functional>
#include <vector>

namespace amanorsacstudio
{

//==============================================================================
struct LicenseResult
{
    bool ok { false };
    int  httpStatus { 0 };
    juce::String code;        // machine-readable, e.g. "device_limit_reached"
    juce::String message;     // always safe to show the user
    juce::var    body;

    static LicenseResult success (juce::var b = {}) { LicenseResult r; r.ok = true; r.body = std::move (b); return r; }
    static LicenseResult failure (juce::String m, juce::String c = {}, int status = 0)
    {
        LicenseResult r; r.message = std::move (m); r.code = std::move (c); r.httpStatus = status; return r;
    }
};

struct LicenseDevice
{
    juce::String label;
    juce::int64  activatedAt { 0 };
    juce::int64  lastSeenAt { 0 };
};

//==============================================================================
class LicenseClient : private juce::Timer
{
public:
    explicit LicenseClient (juce::String baseUrlIn = {})
        : pool (1)
    {
        baseUrl = normaliseBaseUrl (baseUrlIn.isNotEmpty() ? baseUrlIn : defaultBaseUrl());
        loadDeviceKey();
        loadCachedProof();

        // Re-validate periodically so a long-running DAW session's offline
        // window keeps rolling forward rather than counting down to zero.
        startTimer (60 * 60 * 1000);
    }

    ~LicenseClient() override
    {
        stopTimer();
        pool.removeAllJobs (true, 4000);
        masterReference.clear();
    }

    static juce::String defaultBaseUrl()
    {
        auto fromEnv = juce::SystemStats::getEnvironmentVariable ("AMANORSAC_LICENSE_URL", {});
        return fromEnv.isNotEmpty() ? fromEnv : "http://127.0.0.1:4790";
    }

    juce::String getBaseUrl() const { return baseUrl; }

    //==========================================================================
    //  activation
    //==========================================================================

    using ResultCallback = std::function<void (LicenseResult)>;

    /**
     * Activates (or re-validates) this device against a license key. Safe to
     * call repeatedly -- re-activating an already-active device refreshes the
     * cached proof instead of consuming a second seat (see repo/licenses.js).
     */
    void activate (const juce::String& licenseKeyIn, ResultCallback done)
    {
        const auto key = licenseKeyIn.trim().toUpperCase();
        if (key.isEmpty())
        {
            if (done) done (LicenseResult::failure ("Enter a license key.", "license_key_required"));
            return;
        }

        auto* obj = new juce::DynamicObject();
        obj->setProperty ("licenseKey", key);
        obj->setProperty ("deviceKey", deviceKey);
        obj->setProperty ("deviceLabel", juce::SystemStats::getComputerName());

        request ("POST", "/licenses/activate", juce::var (obj),
                 [safe = makeSafe(), key, cb = std::move (done)] (LicenseResult r)
        {
            auto* self = safe.get();
            if (self == nullptr)
                return;

            if (r.ok)
            {
                self->licenseKey = key;
                const auto proof = r.body.getProperty ("proof", "").toString();
                if (proof.isNotEmpty())
                    self->adoptProof (proof);
                self->persistLicenseKey();
            }
            else if (r.code == "device_limit_reached")
            {
                r.message = "This license is already active on the maximum number of devices. "
                            "Deactivate one from within the plugin on that machine, or contact support.";
            }

            if (cb) cb (r);
        });
    }

    /** Frees this device's seat so it can be re-activated elsewhere. */
    void deactivateThisDevice (ResultCallback done)
    {
        if (licenseKey.isEmpty())
        {
            if (done) done (LicenseResult::failure ("No license is active on this device.", "no_license"));
            return;
        }

        auto* obj = new juce::DynamicObject();
        obj->setProperty ("licenseKey", licenseKey);
        obj->setProperty ("deviceKey", deviceKey);

        request ("POST", "/licenses/deactivate", juce::var (obj),
                 [safe = makeSafe(), cb = std::move (done)] (LicenseResult r)
        {
            if (auto* self = safe.get())
                if (r.ok)
                    self->clearLicense();
            if (cb) cb (r);
        });
    }

    void forgetLicenseOnThisDeviceOnly()
    {
        clearLicense();
    }

    //==========================================================================
    //  status -- answers "is this device licensed?" with no network at all
    //==========================================================================

    juce::String getLicenseKey() const noexcept { return licenseKey; }

    /**
     * Is this device licensed RIGHT NOW, as far as we can tell -- including
     * with no network. The cached proof is a server-signed statement
     * verified against the key compiled into this binary and bound to this
     * device, so it cannot be forged by editing a file or copying it from
     * another machine.
     *
     * REAL-TIME SAFE: this is the gate processBlock() calls on the audio
     * thread, so it must never touch the non-atomic fields below directly -
     * those are written from the message thread inside activate()'s
     * callback. licensedFlag is the single atomic word published for this;
     * recomputeLicensedFlag() is what keeps it current.
     */
    bool isLicensed() const noexcept { return licensedFlag.load (std::memory_order_relaxed); }

    /** Days until the offline proof stops working. Negative if it already has. */
    int daysOfOfflineAccessRemaining() const
    {
        if (! proofValid)
            return 0;
        const auto now = juce::Time::getCurrentTime().toMilliseconds();
        return (int) ((proofGraceUntil - now) / (24LL * 60LL * 60LL * 1000LL));
    }

    bool isOfflineProofExpiring() const
    {
        if (! proofValid)
            return false;
        const auto now = juce::Time::getCurrentTime().toMilliseconds();
        return now > proofExpiresAt && now <= proofGraceUntil;
    }

    //==========================================================================
private:
    //==========================================================================

    juce::WeakReference<LicenseClient> makeSafe() { return this; }

    static juce::String normaliseBaseUrl (juce::String url)
    {
        while (url.endsWithChar ('/'))
            url = url.dropLastCharacters (1);
        return url;
    }

    void request (juce::String method, juce::String path, juce::var jsonBody, ResultCallback done)
    {
        const auto url = baseUrl + path;

        pool.addJob ([method, url, jsonBody, cb = std::move (done)]() mutable
        {
            auto result = performRequest (method, url, jsonBody);
            juce::MessageManager::callAsync ([result, cb = std::move (cb)]() mutable
            {
                if (cb) cb (result);
            });
        });
    }

    static LicenseResult performRequest (const juce::String& method, const juce::String& url,
                                         const juce::var& jsonBody)
    {
        juce::URL u (url);
        const bool hasBody = ! jsonBody.isVoid();
        if (hasBody)
            u = u.withPOSTData (juce::JSON::toString (jsonBody, true));

        juce::String headers;
        headers << "Accept: application/json\r\n";
        if (hasBody)
            headers << "Content-Type: application/json\r\n";

        int status = 0;
        auto options = juce::URL::InputStreamOptions (hasBody ? juce::URL::ParameterHandling::inPostData
                                                              : juce::URL::ParameterHandling::inAddress)
                           .withExtraHeaders (headers)
                           .withConnectionTimeoutMs (15000)
                           .withStatusCode (&status)
                           .withHttpRequestCmd (method);

        std::unique_ptr<juce::InputStream> stream (u.createInputStream (options));
        if (stream == nullptr)
            return LicenseResult::failure ("Couldn't reach the license server. Check your connection.",
                                           "network_error", 0);

        const auto text = stream->readEntireStreamAsString();
        const auto parsed = juce::JSON::parse (text);

        LicenseResult r;
        r.httpStatus = status;
        r.body = parsed;

        if (status >= 200 && status < 300)
        {
            r.ok = true;
            return r;
        }

        r.ok = false;
        r.code = parsed.getProperty ("error", "").toString();
        r.message = parsed.getProperty ("message", "").toString();
        if (r.message.isEmpty())
            r.message = friendlyMessageFor (status, r.code);
        return r;
    }

    static juce::String friendlyMessageFor (int status, const juce::String& code)
    {
        if (code == "no_such_license")      return "That license key wasn't recognised. Check for typos.";
        if (code == "license_revoked")      return "This license is no longer valid. Contact support.";
        if (code == "device_limit_reached") return "This license is already active on too many devices.";
        if (status == 429)                  return "Too many attempts. Please wait a moment.";
        if (status >= 500)                  return "The license server is having trouble. Please try again shortly.";
        return "Something went wrong (" + juce::String (status) + ").";
    }

    void timerCallback() override
    {
        recomputeLicensedFlag();   // catches the boundary even with no network at all
        if (licenseKey.isNotEmpty())
            activate (licenseKey, {});   // silent re-validation; refreshes the cached proof
    }

    /**
     * Message-thread-only: recomputes the real-time-safe licensedFlag from
     * the current proof state and wall-clock time. Called whenever the proof
     * changes and once an hour regardless, so the flag correctly flips false
     * once graceUntil passes even if the plugin never talks to the server
     * again (no crash, no dialog on the audio thread - just silently stops
     * streaming, exactly like every other "can't get what it needs" state in
     * SecondaryDeviceManager).
     */
    void recomputeLicensedFlag()
    {
        bool licensed = false;
        if (proofValid && licenseKey.isNotEmpty())
        {
            const auto now = juce::Time::getCurrentTime().toMilliseconds();
            // Clock rolled back behind the proof's own issue time: refuse
            // rather than hand out an indefinite offline license.
            const bool clockRolledBack = now + (48LL * 60LL * 60LL * 1000LL) < proofIssuedAt;
            licensed = ! clockRolledBack && now <= proofGraceUntil;
        }
        licensedFlag.store (licensed, std::memory_order_relaxed);
    }

    //==========================================================================
    //  persistence
    //==========================================================================

    static juce::File appDataDir()
    {
       #if JUCE_WINDOWS
        auto base = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
       #elif JUCE_MAC
        // userApplicationDataDirectory is ~/Library on macOS; app state belongs
        // one level down, in Application Support.
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("Application Support");
       #else
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #endif
        auto dir = base.getChildFile ("Amanorsac Studio").getChildFile ("SecondOut");
        dir.createDirectory();
        return dir;
    }

    static juce::File proofFile()  { return appDataDir().getChildFile ("license-proof.dat"); }
    static juce::File deviceFile() { return appDataDir().getChildFile ("device.id"); }
    static juce::File keyFile()    { return appDataDir().getChildFile ("license-key.dat"); }

    /**
     * A random id generated once, stored locally. Deliberately NOT a hardware
     * serial or MAC address -- same reasoning as PerformLive's device key:
     * it's personal data under GDPR, it breaks when a drive is upgraded, and
     * a determined sharer defeats either kind of id anyway. This limits
     * casual sharing, which is the honest goal.
     */
    void loadDeviceKey()
    {
        auto f = deviceFile();
        if (f.existsAsFile())
        {
            deviceKey = f.loadFileAsString().trim();
            if (deviceKey.length() >= 16 && deviceKey.length() <= 128)
                return;
        }
        deviceKey = juce::Uuid().toDashedString();
        f.replaceWithText (deviceKey);
    }

    void persistLicenseKey()
    {
        if (! licensecrypto::secretStorageIsEncrypted())
            return;   // no OS secret store on this build: don't write plaintext

        juce::MemoryBlock plain (licenseKey.toRawUTF8(), (size_t) licenseKey.getNumBytesAsUTF8());
        auto blob = licensecrypto::protectSecret (plain);
        if (blob.getSize() > 0)
            keyFile().replaceWithData (blob.getData(), blob.getSize());
    }

    void loadPersistedLicenseKey()
    {
        auto f = keyFile();
        if (! f.existsAsFile())
            return;
        juce::MemoryBlock blob;
        if (! f.loadFileAsData (blob))
            return;
        auto plain = licensecrypto::unprotectSecret (blob);
        if (plain.getSize() == 0)
        {
            f.deleteFile();
            return;
        }
        licenseKey = juce::String::fromUTF8 ((const char*) plain.getData(), (int) plain.getSize());
    }

    void clearLicense()
    {
        licenseKey.clear();
        proofValid = false;
        proofExpiresAt = proofGraceUntil = proofIssuedAt = 0;
        keyFile().deleteFile();
        proofFile().deleteFile();
        recomputeLicensedFlag();
    }

    void adoptProof (const juce::String& signedBlob)
    {
        auto body = licensecrypto::parseAndVerifySignedBlob (signedBlob);
        if (! body.isObject())
        {
            proofValid = false;
            recomputeLicensedFlag();
            return;
        }

        // Bound to THIS device and THIS license: a proof copied to another
        // machine, or presented for a different key, verifies cryptographically
        // and then fails right here -- which is the point.
        if (body.getProperty ("deviceKey", "").toString() != deviceKey
            || body.getProperty ("licenseKey", "").toString() != licenseKey)
        {
            proofValid = false;
            recomputeLicensedFlag();
            return;
        }

        proofExpiresAt  = (juce::int64) body.getProperty ("expiresAt", 0);
        proofGraceUntil = (juce::int64) body.getProperty ("graceUntil", 0);
        proofIssuedAt   = (juce::int64) body.getProperty ("issuedAt", 0);
        proofValid = true;
        recomputeLicensedFlag();

        if (licensecrypto::secretStorageIsEncrypted())
        {
            juce::MemoryBlock plain (signedBlob.toRawUTF8(), (size_t) signedBlob.getNumBytesAsUTF8());
            auto enc = licensecrypto::protectSecret (plain);
            if (enc.getSize() > 0)
                proofFile().replaceWithData (enc.getData(), enc.getSize());
        }
    }

    void loadCachedProof()
    {
        loadPersistedLicenseKey();
        if (licenseKey.isEmpty())
            return;

        auto f = proofFile();
        if (! f.existsAsFile())
            return;
        juce::MemoryBlock blob;
        if (! f.loadFileAsData (blob))
            return;
        auto plain = licensecrypto::unprotectSecret (blob);
        if (plain.getSize() == 0)
        {
            f.deleteFile();
            return;
        }
        adoptProof (juce::String::fromUTF8 ((const char*) plain.getData(), (int) plain.getSize()));
    }

    //==========================================================================
    juce::String baseUrl;
    juce::String deviceKey;
    juce::String licenseKey;

    bool proofValid { false };
    juce::int64 proofExpiresAt { 0 }, proofGraceUntil { 0 }, proofIssuedAt { 0 };

    // The only field the audio thread may touch - see isLicensed().
    std::atomic<bool> licensedFlag { false };

    juce::ThreadPool pool;

    JUCE_DECLARE_WEAK_REFERENCEABLE (LicenseClient)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseClient)
};

} // namespace amanorsacstudio
