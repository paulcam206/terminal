// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "pch.h"
#include "../../renderer/inc/PixelFontMetrics.hpp"

namespace Microsoft::Terminal::Settings::Editor
{
    // -------------------------------------------------------------------------
    // Result of a single font analysis run
    // -------------------------------------------------------------------------

    struct FontAnalysisResult
    {
        bool succeeded{ false };
        std::wstring errorMessage;

        // Primary face resolved (first name in FontFace comma-separated list)
        std::wstring resolvedFamilyName;
        std::wstring resolvedPostScriptName;
        bool isVariable{ false };

        // Design metrics (raw DWrite values)
        uint32_t unitsPerEm{ 0 };
        uint32_t ascentUnits{ 0 };
        uint32_t descentUnits{ 0 };
        uint32_t lineGapUnits{ 0 };
        uint32_t cellAdvanceUnits{ 0 }; // advance width of '0'

        // Derived
        uint32_t lineHeightUnits{ 0 }; // ascent + descent + lineGap
        uint32_t cellWidthUnits{ 0 };  // same as cellAdvanceUnits

        // Recommendations
        Microsoft::Console::Render::PointSizeResult pointSizeResult;
    };

    // -------------------------------------------------------------------------
    // Analyzer seam — can be dependency-injected into AppearanceViewModel later
    // -------------------------------------------------------------------------

    // Analyze the first face named in `fontFaceSpec` (comma-separated list, same
    // format as FontFace setting).  `weight` is the DWRITE_FONT_WEIGHT integer.
    // `axes` may be null (no variable-font axis overrides).
    // The call is synchronous; no caching.  Errors are returned in the result,
    // not thrown, unless an internal contract is violated.
    [[nodiscard]] FontAnalysisResult AnalyzeFontFace(
        std::wstring_view fontFaceSpec,
        uint32_t weight,
        winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, float> axes,
        float dpi);

} // namespace Microsoft::Terminal::Settings::Editor
