// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// PixelFontMetrics: pure-math helper for pixel-font point-size generation and
// outline classification.  No DWrite, COM, or WinRT dependencies, so it can be
// used from TerminalSettingsEditor or unit tests without a graphics context.

namespace Microsoft::Console::Render
{
    // -------------------------------------------------------------------------
    // Point-size generation
    // -------------------------------------------------------------------------

    // Describes what path the result candidates came from.
    enum class PointSizeResultKind : uint8_t
    {
        // At least two clean (zero fractional-pixel-error) sizes fall in 6–36 pt.
        // All clean sizes in the 1–128 pt NumberBox range are returned.
        Clean,
        // Fewer than two clean sizes in 6–36 pt; practical candidates from 6–36 pt
        // in 0.5 pt steps with line and cell errors each ≤ 0.15 px.
        Practical,
        // Fewer than two practical sizes passed; best-fit fallback (up to 12,
        // lowest total error, 6–36 pt in 0.5 pt steps).
        BestFit,
    };

    struct PointSizeCandidate
    {
        float pointSize{};
        float lineError{}; // fractional-pixel error for line height
        float cellError{}; // fractional-pixel error for cell width
    };

    struct PointSizeResult
    {
        std::vector<PointSizeCandidate> candidates;
        PointSizeResultKind kind{ PointSizeResultKind::Clean };
    };

    struct PixelFontDesignMetrics
    {
        uint32_t unitsPerEm{};
        uint32_t lineHeightUnits{};
        uint32_t cellWidthUnits{};
    };

    // Compute recommended point sizes for the font at a given screen DPI.
    // Throws std::invalid_argument if any required metric or dpi is zero/negative.
    // Throws std::overflow_error if the LCM of the denominators exceeds uint64_t.
    [[nodiscard]] PointSizeResult ComputePixelFontPointSizes(const PixelFontDesignMetrics& metrics, float dpi);

    // -------------------------------------------------------------------------
    // Snapping helpers
    // -------------------------------------------------------------------------

    // Return the candidate whose pointSize is within |tolerance| of pointSize,
    // or -1.0f if none.
    [[nodiscard]] float SnapExact(std::span<const PointSizeCandidate> candidates,
                                  float pointSize,
                                  float tolerance = 0.01f) noexcept;

    // Return the candidate pointSize nearest to pointSize, or -1.0f if empty.
    [[nodiscard]] float SnapNearest(std::span<const PointSizeCandidate> candidates,
                                    float pointSize) noexcept;

    // Return the smallest candidate pointSize strictly greater than pointSize,
    // or -1.0f if none.
    [[nodiscard]] float SnapNext(std::span<const PointSizeCandidate> candidates,
                                 float pointSize) noexcept;

    // Return the largest candidate pointSize strictly less than pointSize,
    // or -1.0f if none.
    [[nodiscard]] float SnapPrevious(std::span<const PointSizeCandidate> candidates,
                                     float pointSize) noexcept;
}
