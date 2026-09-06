#include "TestFramework.h"
#include "DriftCorrector.h"

#include <cmath>

TEST_CASE (drift_noErrorNoTrim)
{
    DriftCorrector d;
    d.prepare ({}, 48000.0);

    for (int i = 0; i < 1000; ++i)
        d.process (0.5, 480);   // exactly on target

    EXPECT_NEAR (d.getTrimPpm(), 0.0, 0.01);
    EXPECT_NEAR (d.getRatioMultiplier(), 1.0, 1.0e-7);
}

TEST_CASE (drift_overfullPullsPositiveTrim)
{
    DriftCorrector d;
    d.prepare ({}, 48000.0);

    for (int i = 0; i < 2000; ++i)
        d.process (0.7, 480);   // persistently too full

    // Too full -> consume faster -> positive trim.
    EXPECT (d.getTrimPpm() > 1.0);
    EXPECT (d.getRatioMultiplier() > 1.0);
}

TEST_CASE (drift_underfullPullsNegativeTrim)
{
    DriftCorrector d;
    d.prepare ({}, 48000.0);

    for (int i = 0; i < 2000; ++i)
        d.process (0.3, 480);

    EXPECT (d.getTrimPpm() < -1.0);
    EXPECT (d.getRatioMultiplier() < 1.0);
}

TEST_CASE (drift_trimIsClamped)
{
    DriftCorrector::Config cfg;
    cfg.maxCorrectionPpm = 100.0;
    DriftCorrector d;
    d.prepare (cfg, 48000.0);

    for (int i = 0; i < 100000; ++i)
        d.process (1.0, 480);   // completely full forever

    EXPECT (d.getTrimPpm() <= 100.0 + 1.0e-9);
}

TEST_CASE (drift_slewRateLimited)
{
    DriftCorrector::Config cfg;
    cfg.slewPpmPerSecond = 10.0;
    DriftCorrector d;
    d.prepare (cfg, 48000.0);

    // One second of persistently-full buffer: trim may move at most 10 ppm.
    for (int i = 0; i < 100; ++i)
        d.process (1.0, 480);

    EXPECT (std::abs (d.getTrimPpm()) <= 10.0 + 1.0e-6);
}

TEST_CASE (drift_servoConvergesInClosedLoop)
{
    // Simulate: producer at 48000 Hz nominal, consumer crystal 40 ppm fast.
    // The servo should settle the fill level near target without oscillating.
    DriftCorrector d;
    d.prepare ({}, 48000.0);

    const double producerRate = 48000.0;
    const double consumerRate = 48000.0 * (1.0 + 40.0e-6);
    const double capacity = 19200.0;   // 400 ms
    double fill = capacity * 0.5;

    const int blockSize = 480;   // 10 ms consumer blocks
    double maxFillAfterSettle = 0.0, minFillAfterSettle = 1.0;

    const int totalBlocks = 100 * 3600;   // 1 hour simulated
    for (int i = 0; i < totalBlocks; ++i)
    {
        const double trim = d.process (fill / capacity, blockSize);
        const double ratio = 1.0 + trim * 1.0e-6;

        // Producer adds real-time worth of samples; consumer removes
        // blockSize * ratio producer-samples per block.
        fill += producerRate * (blockSize / consumerRate);
        fill -= blockSize * ratio;
        if (fill < 0) fill = 0;
        if (fill > capacity) fill = capacity;

        if (i > totalBlocks / 2)
        {
            const double r = fill / capacity;
            if (r > maxFillAfterSettle) maxFillAfterSettle = r;
            if (r < minFillAfterSettle) minFillAfterSettle = r;
        }
    }

    // After settling, fill should hover near 50% and not swing widely.
    EXPECT (minFillAfterSettle > 0.30);
    EXPECT (maxFillAfterSettle < 0.70);
    EXPECT (maxFillAfterSettle - minFillAfterSettle < 0.10);

    // And the trim should be near the actual clock offset (-40 ppm side).
    EXPECT_NEAR (d.getTrimPpm(), -40.0, 15.0);
}
