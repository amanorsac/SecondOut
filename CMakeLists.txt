cmake_minimum_required(VERSION 3.22)

# Must be set before project(): one universal binary for Apple Silicon and
# Intel, and the oldest macOS the SecKey calls in LicenseCrypto.h support.
if (APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
endif()

project(SecondOut VERSION 1.3.2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

set(SECONDOUT_FORMATS VST3 Standalone)
if (APPLE)
    list(APPEND SECONDOUT_FORMATS AU)   # Logic and GarageBand only load AU
endif()

juce_add_plugin(SecondOut
    COMPANY_NAME "Amanorsac Studio"
    # Matches the identifier registered with Apple.
    BUNDLE_ID "com.amanorsac.secondout"
    PLUGIN_MANUFACTURER_CODE Amst
    PLUGIN_CODE Sc2o
    FORMATS ${SECONDOUT_FORMATS}
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    PRODUCT_NAME "SecondOut"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    # Required for notarisation; no effect on Windows.
    HARDENED_RUNTIME_ENABLED TRUE
    # Under the hardened runtime, a usage description alone is not enough to
    # reach an input device - the entitlement has to be there too, or macOS
    # kills the process on first access instead of prompting.
    HARDENED_RUNTIME_OPTIONS "com.apple.security.device.audio-input"
    # The standalone opens an audio input to have something to send; without a
    # usage description macOS terminates the app on first access instead of asking.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "SecondOut's standalone app needs audio input so it has something to send to the second output."
    VST3_CATEGORIES Tools)

target_sources(SecondOut PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SecondaryDeviceManager.cpp)

if (MSVC)
    target_compile_options(SecondOut PRIVATE /utf-8)
endif()

target_compile_definitions(SecondOut PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if (WIN32)
    target_compile_definitions(SecondOut PUBLIC
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=0)

    # Steinberg's ASIO SDK license forbids redistributing it with JUCE, so it's
    # not bundled - it lives in ThirdParty/ASIOSDK (gitignored) and must be
    # downloaded separately from Steinberg. Without it the plugin still builds,
    # just WASAPI-only (which is what CI produces); release builds need it.
    set(SECONDOUT_ASIO_SDK "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ASIOSDK")
    if (EXISTS "${SECONDOUT_ASIO_SDK}/common/iasiodrv.h")
        target_compile_definitions(SecondOut PUBLIC JUCE_ASIO=1)
        target_include_directories(SecondOut PRIVATE "${SECONDOUT_ASIO_SDK}/common")
    else()
        message(WARNING "ASIO SDK not found at ${SECONDOUT_ASIO_SDK} - building WASAPI-only. "
                        "Download it from https://www.steinberg.net/asiosdk to enable ASIO.")
        target_compile_definitions(SecondOut PUBLIC JUCE_ASIO=0)
    endif()
endif()

target_link_libraries(SecondOut
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_cryptography   # juce::SHA256, used to verify signed license/activation proofs
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if (APPLE)
    # LicenseCrypto.h: SecKey signature verification, SecRandom, hardware UUID.
    # (CommonCrypto is part of libSystem and needs nothing extra.)
    target_link_libraries(SecondOut PRIVATE "-framework Security" "-framework IOKit")
endif()

# ---- Standalone app ----
# Replaces JUCE's stock standalone window (an "Options" button plus a yellow
# muted-input notification bar) with Source/Standalone/StandaloneApp.cpp. Set
# on the Standalone target alone: the VST3 and AU builds never compile it.
if (TARGET SecondOut_Standalone)
    target_sources(SecondOut_Standalone PRIVATE Source/Standalone/StandaloneApp.cpp)
    target_compile_definitions(SecondOut_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

    if (MSVC)
        target_compile_options(SecondOut_Standalone PRIVATE /utf-8)
    endif()
endif()

# ---- Unit tests (pure C++, no JUCE dependency) ----
add_executable(SecondOutTests
    Tests/RingBufferTests.cpp
    Tests/DriftCorrectorTests.cpp
    Tests/ResamplerTests.cpp
    Tests/TestMain.cpp)

target_include_directories(SecondOutTests PRIVATE Source)

if (MSVC)
    target_compile_options(SecondOutTests PRIVATE /utf-8)
endif()

enable_testing()
add_test(NAME SecondOutTests COMMAND SecondOutTests)
