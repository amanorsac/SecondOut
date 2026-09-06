#pragma once

#include <algorithm>

/**
    Servo loop that keeps the ring buffer centred at its target fill level by
    nudging the resampling ratio a few PPM at a time.

    The DAW clock and the secondary device clock are independent crystals; left
    alone the buffer drifts toward empty or full. Rather than snapping, this
    applies a slew-limited proportional correction so the pitch change stays
    far below audibility.

    Called from the consumer (device) thread only.
*/
class DriftCorrector
{
public:
    struct Config
    {
        double targetFillRatio   = 0.5;    // aim for half-full buffer
        double maxCorrectionPpm  = 300.0;  // absolute clamp on the trim
        double slewPpmPerSecond  = 10.0;   // how fast the trim may move
        double updateIntervalSec = 0.5;    // how often the fill level is sampled
        double proportionalGain  = 400.0;  // ppm of desired trim per unit of fill error
    };

    void prepare (const Config& cfg, double consumerSampleRate)
    {
        config = cfg;
        sampleRate = consumerSampleRate > 0 ? consumerSampleRate : 48000.0;
        updateIntervalSamples = static_cast<long long> (config.updateIntervalSec * sampleRate);
        reset();
    }

    void reset() noexcept
    {
        trimPpm = 0.0;
        desiredTrimPpm = 0.0;
        samplesSinceUpdate = 0;
        fillAccumulator = 0.0;
        fillSamples = 0;
    }

    /** Feed the current fill ratio (0..1); call once per device callback with
        the number of output frames just rendered. Returns the current trim in PPM. */
    double process (double fillRatio, int numFramesRendered) noexcept
    {
        fillAccumulator += fillRatio;
        ++fillSamples;
        samplesSinceUpdate += numFramesRendered;

        if (samplesSinceUpdate >= updateIntervalSamples && fillSamples > 0)
        {
            const double avgFill = fillAccumulator / static_cast<double> (fillSamples);
            const double error   = avgFill - config.targetFillRatio;   // >0: too full -> consume faster

            desiredTrimPpm = std::clamp (error * config.proportionalGain,
                                         -config.maxCorrectionPpm, config.maxCorrectionPpm);

            samplesSinceUpdate = 0;
            fillAccumulator = 0.0;
            fillSamples = 0;
        }

        // Slew-limit the applied trim toward the desired trim.
        const double maxStep = config.slewPpmPerSecond
                             * (static_cast<double> (numFramesRendered) / sampleRate);
        const double delta = std::clamp (desiredTrimPpm - trimPpm, -maxStep, maxStep);
        trimPpm += delta;

        return trimPpm;
    }

    /** Multiply the base resampling ratio by this. */
    double getRatioMultiplier() const noexcept { return 1.0 + trimPpm * 1.0e-6; }

    double getTrimPpm() const noexcept { return trimPpm; }

private:
    Config config;
    double sampleRate = 48000.0;
    long long updateIntervalSamples = 24000;
    long long samplesSinceUpdate = 0;
    double fillAccumulator = 0.0;
    long long fillSamples = 0;
    double trimPpm = 0.0;
    double desiredTrimPpm = 0.0;
};
