// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "precomp.h"
#include "../inc/PixelFontMetrics.hpp"

#include <limits>

using namespace Microsoft::Console::Render;

// ---------------------------------------------------------------------------
// Internal math helpers
// ---------------------------------------------------------------------------

namespace
{
    // Overflow-safe LCM for uint64_t values.
    // Throws std::overflow_error if the result would exceed UINT64_MAX.
    static uint64_t SafeLcm(uint64_t a, uint64_t b)
    {
        if (a == 0 || b == 0)
            return 0;
        const uint64_t g = std::gcd(a, b);
        const uint64_t q = a / g; // divide first to reduce risk of overflow
        if (q > std::numeric_limits<uint64_t>::max() / b)
            throw std::overflow_error("LCM of pixel-font denominators overflows uint64");
        return q * b;
    }

    static float FracError(float v) noexcept
    {
        return std::abs(v - std::round(v));
    }

    static PointSizeCandidate MakeCandidate(float pt, float lineRatio, float cellRatio, float dpi) noexcept
    {
        const float ppem = pt * dpi / 72.0f;
        return { pt, FracError(lineRatio * ppem), FracError(cellRatio * ppem) };
    }
} // namespace

// ---------------------------------------------------------------------------
// ComputePixelFontPointSizes
// ---------------------------------------------------------------------------

PointSizeResult Microsoft::Console::Render::ComputePixelFontPointSizes(
    const PixelFontDesignMetrics& metrics,
    float dpi)
{
    if (metrics.unitsPerEm == 0 || metrics.lineHeightUnits == 0 || metrics.cellWidthUnits == 0)
        throw std::invalid_argument("unitsPerEm, lineHeightUnits, and cellWidthUnits must be positive");
    if (dpi <= 0.0f)
        throw std::invalid_argument("dpi must be positive");

    // Reduce line and cell fractions by their GCDs.
    const uint32_t lineGcd = std::gcd(metrics.lineHeightUnits, metrics.unitsPerEm);
    const uint32_t lineDenom = metrics.unitsPerEm / lineGcd;

    const uint32_t cellGcd = std::gcd(metrics.cellWidthUnits, metrics.unitsPerEm);
    const uint32_t cellDenom = metrics.unitsPerEm / cellGcd;

    // The clean ppem step is lcm(lineDenom, cellDenom).
    uint64_t cleanStep = SafeLcm(lineDenom, cellDenom);

    const float lineRatio = static_cast<float>(metrics.lineHeightUnits) / metrics.unitsPerEm;
    const float cellRatio = static_cast<float>(metrics.cellWidthUnits) / metrics.unitsPerEm;

    // -----------------------------------------------------------------------
    // Build the full clean-size list in the NumberBox range [1, 128] pt.
    // A clean ppem is an integer multiple of cleanStep; the corresponding point
    // size is ppem * 72 / dpi.
    // -----------------------------------------------------------------------
    std::vector<PointSizeCandidate> cleanCandidates;
    if (cleanStep > 0)
    {
        for (uint64_t k = 1; ; ++k)
        {
            const uint64_t ppemU = k * cleanStep;
            // Guard: ppem > 128 * dpi / 72 means pointSize > 128.
            const float ptMax = static_cast<float>(ppemU) * 72.0f / dpi;
            if (ptMax > 128.0f)
                break;
            if (ptMax >= 1.0f)
                cleanCandidates.push_back(MakeCandidate(ptMax, lineRatio, cellRatio, dpi));
        }
    }

    // Count how many clean sizes fall in the practical 6–36 pt band.
    int cleanInBand = 0;
    for (const auto& c : cleanCandidates)
        if (c.pointSize >= 6.0f && c.pointSize <= 36.0f)
            ++cleanInBand;

    if (cleanInBand >= 2)
        return { std::move(cleanCandidates), PointSizeResultKind::Clean };

    // -----------------------------------------------------------------------
    // Practical fallback: 6–36 pt in 0.5 pt steps, both errors ≤ 0.15.
    // Take up to 12 lowest total-error, sorted by point size.
    // -----------------------------------------------------------------------
    std::vector<PointSizeCandidate> practical;
    for (int i = 0; i <= 60; ++i) // 0 → 6.0, 60 → 36.0
    {
        const float pt = 6.0f + i * 0.5f;
        const auto c = MakeCandidate(pt, lineRatio, cellRatio, dpi);
        if (c.lineError <= 0.15f && c.cellError <= 0.15f)
            practical.push_back(c);
    }

    if (practical.size() >= 2)
    {
        // Sort by total error, keep best 12, then re-sort by point size.
        std::stable_sort(practical.begin(), practical.end(),
                         [](const PointSizeCandidate& a, const PointSizeCandidate& b) {
                             return (a.lineError + a.cellError) < (b.lineError + b.cellError);
                         });
        if (practical.size() > 12)
            practical.resize(12);
        std::sort(practical.begin(), practical.end(),
                  [](const PointSizeCandidate& a, const PointSizeCandidate& b) {
                      return a.pointSize < b.pointSize;
                  });
        return { std::move(practical), PointSizeResultKind::Practical };
    }

    // -----------------------------------------------------------------------
    // Best-fit fallback: up to 12 lowest total-error, sorted by point size.
    // -----------------------------------------------------------------------
    std::vector<PointSizeCandidate> all;
    all.reserve(61);
    for (int i = 0; i <= 60; ++i)
    {
        const float pt = 6.0f + i * 0.5f;
        all.push_back(MakeCandidate(pt, lineRatio, cellRatio, dpi));
    }
    std::stable_sort(all.begin(), all.end(),
                     [](const PointSizeCandidate& a, const PointSizeCandidate& b) {
                         return (a.lineError + a.cellError) < (b.lineError + b.cellError);
                     });
    if (all.size() > 12)
        all.resize(12);
    std::sort(all.begin(), all.end(),
              [](const PointSizeCandidate& a, const PointSizeCandidate& b) {
                  return a.pointSize < b.pointSize;
              });
    return { std::move(all), PointSizeResultKind::BestFit };
}

// ---------------------------------------------------------------------------
// Snapping helpers
// ---------------------------------------------------------------------------

float Microsoft::Console::Render::SnapExact(
    std::span<const PointSizeCandidate> candidates,
    float pointSize,
    float tolerance) noexcept
{
    for (const auto& c : candidates)
        if (std::abs(c.pointSize - pointSize) <= tolerance)
            return c.pointSize;
    return -1.0f;
}

float Microsoft::Console::Render::SnapNearest(
    std::span<const PointSizeCandidate> candidates,
    float pointSize) noexcept
{
    float best = -1.0f;
    float bestDist = std::numeric_limits<float>::max();
    for (const auto& c : candidates)
    {
        const float d = std::abs(c.pointSize - pointSize);
        if (d < bestDist)
        {
            bestDist = d;
            best = c.pointSize;
        }
    }
    return best;
}

float Microsoft::Console::Render::SnapNext(
    std::span<const PointSizeCandidate> candidates,
    float pointSize) noexcept
{
    float best = -1.0f;
    for (const auto& c : candidates)
        if (c.pointSize > pointSize && (best < 0.0f || c.pointSize < best))
            best = c.pointSize;
    return best;
}

float Microsoft::Console::Render::SnapPrevious(
    std::span<const PointSizeCandidate> candidates,
    float pointSize) noexcept
{
    float best = -1.0f;
    for (const auto& c : candidates)
        if (c.pointSize < pointSize && c.pointSize > best)
            best = c.pointSize;
    return best;
}
