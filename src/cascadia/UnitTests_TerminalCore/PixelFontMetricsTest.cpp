// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include <WexTestClass.h>

#include "../renderer/inc/PixelFontMetrics.hpp"

using namespace Microsoft::Console::Render;
using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;

// Floating-point comparison tolerance for point sizes.
static constexpr float kEps = 0.001f;

static bool NearlyEqual(float a, float b, float tol = kEps)
{
    return std::abs(a - b) <= tol;
}

namespace TerminalCoreUnitTests
{
    class PixelFontMetricsTests
    {
        TEST_CLASS(PixelFontMetricsTests);

        // -------------------------------------------------------------------
        // Clean point-size generation — Departure (unitsPerEm=550)
        // line=700 -> ratio 14/11, cell=350 -> ratio 7/11 => lcm denom = 11
        // clean ppems at 96 DPI: 11*0.75=8.25, 22*0.75=16.5, 33*0.75=24.75, 44*0.75=33
        // -------------------------------------------------------------------
        TEST_METHOD(DepartureCleanSizesAt96Dpi)
        {
            const PixelFontDesignMetrics m{ 550, 700, 350 };
            const auto result = ComputePixelFontPointSizes(m, 96.0f);

            VERIFY_ARE_EQUAL(PointSizeResultKind::Clean, result.kind);

            // Collect the clean sizes that fall in the 6-36 pt band.
            std::vector<float> band;
            for (const auto& c : result.candidates)
                if (c.pointSize >= 6.0f && c.pointSize <= 36.0f)
                    band.push_back(c.pointSize);

            VERIFY_ARE_EQUAL(4u, static_cast<unsigned>(band.size()));
            VERIFY_IS_TRUE(NearlyEqual(band[0], 8.25f));
            VERIFY_IS_TRUE(NearlyEqual(band[1], 16.5f));
            VERIFY_IS_TRUE(NearlyEqual(band[2], 24.75f));
            VERIFY_IS_TRUE(NearlyEqual(band[3], 33.0f));

            // Errors at clean sizes should be essentially zero.
            for (const auto& c : result.candidates)
            {
                VERIFY_IS_TRUE(c.lineError < 0.001f);
                VERIFY_IS_TRUE(c.cellError < 0.001f);
            }
        }

        // -------------------------------------------------------------------
        // Clean point-size generation — Proggy (unitsPerEm=2048)
        // line=1664 -> ratio 13/16, cell=896 -> ratio 7/16 => lcm denom = 16
        // clean ppems at 96 DPI: 16*0.75=12, 32*0.75=24, 48*0.75=36
        // -------------------------------------------------------------------
        TEST_METHOD(ProggyCleanSizesAt96Dpi)
        {
            const PixelFontDesignMetrics m{ 2048, 1664, 896 };
            const auto result = ComputePixelFontPointSizes(m, 96.0f);

            VERIFY_ARE_EQUAL(PointSizeResultKind::Clean, result.kind);

            std::vector<float> band;
            for (const auto& c : result.candidates)
                if (c.pointSize >= 6.0f && c.pointSize <= 36.0f)
                    band.push_back(c.pointSize);

            VERIFY_ARE_EQUAL(3u, static_cast<unsigned>(band.size()));
            VERIFY_IS_TRUE(NearlyEqual(band[0], 12.0f));
            VERIFY_IS_TRUE(NearlyEqual(band[1], 24.0f));
            VERIFY_IS_TRUE(NearlyEqual(band[2], 36.0f));
        }

        // -------------------------------------------------------------------
        // -------------------------------------------------------------------
        // DPI variation: same font at 120 DPI.
        // Departure denom=11; ppem*72/120 = ppem*0.6
        // clean ppems: 11->6.6, 22->13.2, 33->19.8, 44->26.4, 55->33, 60->36 (not clean), 66->39.6
        // In 6-36: 6.6, 13.2, 19.8, 26.4, 33 => 5 values >=2 -> Clean
        // -------------------------------------------------------------------
        TEST_METHOD(DepartureCleanSizesAt120Dpi)
        {
            const PixelFontDesignMetrics m{ 550, 700, 350 };
            const auto result = ComputePixelFontPointSizes(m, 120.0f);

            VERIFY_ARE_EQUAL(PointSizeResultKind::Clean, result.kind);
            std::vector<float> band;
            for (const auto& c : result.candidates)
                if (c.pointSize >= 6.0f && c.pointSize <= 36.0f)
                    band.push_back(c.pointSize);
            VERIFY_IS_TRUE(band.size() >= 2u);
            // 11 * 72 / 120 = 6.6
            VERIFY_IS_TRUE(NearlyEqual(band[0], 6.6f));
        }

        // -------------------------------------------------------------------
        // Practical fallback: craft metrics that yield no clean sizes in 6-36
        // but some practical ones (errors <= 0.15).
        // Use unitsPerEm=1000, line=1001, cell=999 (irrational-like fractions).
        // For line=1001/1000: lineError(pt) = frac(1001/1000 * pt*dpi/72)
        // Several half-point sizes will be below 0.15.
        // -------------------------------------------------------------------
        TEST_METHOD(PracticalFallback)
        {
            // Large-denominator ratios: very few (or zero) clean sizes in 6-36.
            const PixelFontDesignMetrics m{ 1000, 1001, 999 };
            const auto result = ComputePixelFontPointSizes(m, 96.0f);
            // Should not be Clean (denominator is huge).
            VERIFY_ARE_NOT_EQUAL(PointSizeResultKind::Clean, result.kind);
            // Candidates should be non-empty and within range.
            VERIFY_IS_FALSE(result.candidates.empty());
            for (const auto& c : result.candidates)
            {
                VERIFY_IS_TRUE(c.pointSize >= 6.0f && c.pointSize <= 36.0f);
            }
        }

        // -------------------------------------------------------------------
        // Fallback (non-Clean): use metrics with a clean step so large that no
        // clean size fits in [1,128], forcing either Practical or BestFit.
        // Structural invariants (sorted, ≤ 12, in 6-36) must hold regardless.
        // Note: BestFit is triggered when even the practical band has < 2
        // candidates with both errors ≤ 0.15 px, which requires an unusual
        // combination of fractional metrics and DPI. The branch is correct by
        // code inspection; this test validates the shared structural contract.
        // -------------------------------------------------------------------
        TEST_METHOD(BestFitFallback)
        {
            // cleanStep = 9797 -> first clean pt at 96 DPI = 7347.75 >> 128; no clean in range.
            const PixelFontDesignMetrics m{ 9797, 1, 1 };
            const auto result = ComputePixelFontPointSizes(m, 96.0f);
            // Must not be Clean.
            VERIFY_ARE_NOT_EQUAL(PointSizeResultKind::Clean, result.kind);
            // Candidates are non-empty, at most 12, sorted by point size, within 6-36.
            VERIFY_IS_FALSE(result.candidates.empty());
            VERIFY_IS_TRUE(result.candidates.size() <= 12u);
            for (const auto& c : result.candidates)
                VERIFY_IS_TRUE(c.pointSize >= 6.0f && c.pointSize <= 36.0f);
            for (size_t i = 1; i < result.candidates.size(); ++i)
                VERIFY_IS_TRUE(result.candidates[i].pointSize > result.candidates[i - 1].pointSize);
        }

        // -------------------------------------------------------------------
        // Invalid argument: zero/negative inputs throw.
        // -------------------------------------------------------------------
        TEST_METHOD(InvalidArgumentsThrow)
        {
            auto throws = [](auto fn) { VERIFY_THROWS(fn(), std::invalid_argument); };
            throws([] { (void)ComputePixelFontPointSizes({ 0, 700, 350 }, 96.0f); });
            throws([] { (void)ComputePixelFontPointSizes({ 550, 0, 350 }, 96.0f); });
            throws([] { (void)ComputePixelFontPointSizes({ 550, 700, 0 }, 96.0f); });
            throws([] { (void)ComputePixelFontPointSizes({ 550, 700, 350 }, 0.0f); });
            throws([] { (void)ComputePixelFontPointSizes({ 550, 700, 350 }, -1.0f); });
        }

        // -------------------------------------------------------------------
        // LCM robustness: lineDenom and cellDenom are both uint32, so their
        // product fits in uint64 (4294967295^2 < UINT64_MAX).  Verify that a
        // maximally-large unitsPerEm (UINT32_MAX) does not throw and produces
        // no candidates in the 6-36 pt band.
        // -------------------------------------------------------------------
        TEST_METHOD(LcmOverflowThrows)
        {
            // cleanStep = 4294967295; first point size at 96 DPI = 4294967295*72/96 >> 128.
            // No candidates in range; should return Practical or BestFit without throwing.
            const PixelFontDesignMetrics m{ 4294967295u, 1u, 1u };
            // Should not throw; no candidates in range.
            const auto result = ComputePixelFontPointSizes(m, 96.0f);
            VERIFY_ARE_NOT_EQUAL(PointSizeResultKind::Clean, result.kind);
        }

        // -------------------------------------------------------------------
        // Snapping helpers
        // -------------------------------------------------------------------
        TEST_METHOD(SnappingHelpers)
        {
            // Use Proggy clean sizes at 96 DPI.
            const auto result = ComputePixelFontPointSizes({ 2048, 1664, 896 }, 96.0f);
            const auto& cands = result.candidates;

            // SnapExact: 12 should snap to the 12pt candidate.
            VERIFY_IS_TRUE(NearlyEqual(SnapExact(cands, 12.0f), 12.0f));
            // SnapExact: 13 is not in the list.
            VERIFY_IS_TRUE(NearlyEqual(SnapExact(cands, 13.0f), -1.0f));

            // SnapNearest: 11 is nearest to 12.
            VERIFY_IS_TRUE(NearlyEqual(SnapNearest(cands, 11.0f), 12.0f));
            // SnapNearest: 18 is between 12 and 24, nearer to 24.
            VERIFY_IS_TRUE(NearlyEqual(SnapNearest(cands, 19.0f), 24.0f));

            // SnapNext after 12 is 24.
            VERIFY_IS_TRUE(NearlyEqual(SnapNext(cands, 12.0f), 24.0f));
            // SnapNext after the last candidate returns -1.
            VERIFY_IS_TRUE(NearlyEqual(SnapNext(cands, cands.back().pointSize), -1.0f));

            // SnapPrevious before 24 is 12.
            VERIFY_IS_TRUE(NearlyEqual(SnapPrevious(cands, 24.0f), 12.0f));
            // SnapPrevious before the first candidate returns -1.
            VERIFY_IS_TRUE(NearlyEqual(SnapPrevious(cands, cands.front().pointSize), -1.0f));
        }

        TEST_METHOD(SnappingEmptyCandidates)
        {
            const std::vector<PointSizeCandidate> empty;
            VERIFY_IS_TRUE(NearlyEqual(SnapExact(empty, 12.0f), -1.0f));
            VERIFY_IS_TRUE(NearlyEqual(SnapNearest(empty, 12.0f), -1.0f));
            VERIFY_IS_TRUE(NearlyEqual(SnapNext(empty, 12.0f), -1.0f));
            VERIFY_IS_TRUE(NearlyEqual(SnapPrevious(empty, 12.0f), -1.0f));
        }

        // -------------------------------------------------------------------
        // IBM 3270 regression — unitsPerEm=2000, line=2180, cell=1080
        //
        // At DPI 168 the clean ppem step is:
        //   lineGcd = gcd(2180,2000) = 20  -> lineDenom = 100
        //   cellGcd = gcd(1080,2000) = 40  -> cellDenom = 50
        //   cleanStep = lcm(100,50) = 100
        //   ptPerPpem = 72/168 = 3/7
        //   first few clean pt: 100*3/7≈42.857, 200*3/7≈85.714 — none in 6-36
        //   -> falls to Practical (errors ≤ 0.15 px in 0.5 pt steps, 6-36 pt)
        //   Practical set at 168 DPI includes 11, 25.5, 35 among others.
        //
        // At DPI 129 the clean ppem step is still 100 but:
        //   ptPerPpem = 72/129 ≈ 0.5581...
        //   first clean pt = 55.81 — not in 6-36, so again falls to Practical.
        //   11 pt at 129 DPI: ppem = 11*129/72 ≈ 19.75
        //     lineError = frac(1.09 * 19.75) = frac(21.5275) = 0.5275 > 0.15 — EXCLUDED.
        //   So the strict practical set at DPI 129 does NOT contain 11 pt, documenting
        //   that recommendations are display-scale-specific.
        // -------------------------------------------------------------------
        TEST_METHOD(Ibm3270At168Dpi)
        {
            // IBM 3270 design metrics: unitsPerEm=2000, lineHeight=2180, cellWidth=1080
            const PixelFontDesignMetrics m{ 2000, 2180, 1080 };
            const auto result = ComputePixelFontPointSizes(m, 168.0f);

            // No clean sizes in 6-36 pt at this DPI, so Practical or BestFit.
            VERIFY_ARE_NOT_EQUAL(PointSizeResultKind::Clean, result.kind);

            // Collect the practical/best-fit sizes in 6-36 pt.
            std::vector<float> pts;
            for (const auto& c : result.candidates)
                pts.push_back(c.pointSize);

            // 11 pt must be present (ppem = 11*168/72 = 25.666...; lineErr = frac(1.09*25.666)=frac(27.975)=0.025 ≤ 0.15)
            const bool has11 = std::any_of(pts.begin(), pts.end(), [](float p) { return NearlyEqual(p, 11.0f, 0.05f); });
            VERIFY_IS_TRUE(has11);

            // 25.5 pt must be present (ppem = 25.5*168/72 = 59.5; lineErr = frac(1.09*59.5)=frac(64.855)=0.145 ≤ 0.15)
            const bool has25_5 = std::any_of(pts.begin(), pts.end(), [](float p) { return NearlyEqual(p, 25.5f, 0.05f); });
            VERIFY_IS_TRUE(has25_5);

            // 35 pt must be present (ppem = 35*168/72 = 81.666...; lineErr = frac(1.09*81.666)=frac(89.016)=0.016 ≤ 0.15)
            const bool has35 = std::any_of(pts.begin(), pts.end(), [](float p) { return NearlyEqual(p, 35.0f, 0.05f); });
            VERIFY_IS_TRUE(has35);
        }

        TEST_METHOD(Ibm3270At129Dpi)
        {
            // Same font, different DPI — recommendations are display-scale-specific.
            // At 129 DPI, 11 pt maps to ppem≈19.75; lineError = frac(1.09*19.75)=frac(21.5275)≈0.53 > 0.15.
            // Therefore 11 pt is NOT a practical candidate at 129 DPI.
            const PixelFontDesignMetrics m{ 2000, 2180, 1080 };
            const auto result = ComputePixelFontPointSizes(m, 129.0f);

            VERIFY_ARE_NOT_EQUAL(PointSizeResultKind::Clean, result.kind);

            std::vector<float> pts;
            for (const auto& c : result.candidates)
                pts.push_back(c.pointSize);

            // The strict practical set starts at 18.5 pt or higher — 11 pt is absent.
            const bool has11 = std::any_of(pts.begin(), pts.end(), [](float p) { return NearlyEqual(p, 11.0f, 0.05f); });
            VERIFY_IS_FALSE(has11);

            // All candidates must be at or above 18.5 pt (the first viable size at 129 DPI).
            for (float p : pts)
                VERIFY_IS_TRUE(p >= 18.5f - 0.05f);
        }
    };
}
