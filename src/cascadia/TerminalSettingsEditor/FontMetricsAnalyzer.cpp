// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "FontMetricsAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string_view>

namespace Microsoft::Terminal::Settings::Editor
{
    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    // Extract the first token from a comma-separated font face spec.
    static std::wstring FirstFaceName(std::wstring_view spec)
    {
        const auto comma = spec.find(L',');
        auto name = std::wstring{ comma == std::wstring_view::npos ? spec : spec.substr(0, comma) };
        // Trim leading/trailing whitespace
        const auto ltrim = name.find_first_not_of(L" \t");
        if (ltrim == std::wstring::npos)
            return {};
        const auto rtrim = name.find_last_not_of(L" \t");
        return name.substr(ltrim, rtrim - ltrim + 1);
    }

    // -------------------------------------------------------------------------
    // AnalyzeFontFace
    // -------------------------------------------------------------------------

    FontAnalysisResult AnalyzeFontFace(
        std::wstring_view fontFaceSpec,
        uint32_t weight,
        winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, float> axes,
        float dpi)
    {
        FontAnalysisResult result;

        const auto familyName = FirstFaceName(fontFaceSpec);
        if (familyName.empty())
        {
            result.errorMessage = L"Font face spec is empty";
            return result;
        }

        // Create DWrite factory (base interface for stable API surface)
        wil::com_ptr<IDWriteFactory> factory;
        {
            HRESULT hr = DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(factory),
                reinterpret_cast<IUnknown**>(factory.put()));
            if (FAILED(hr))
            {
                result.errorMessage = L"DWriteCreateFactory failed: hr=" + std::to_wstring(hr);
                return result;
            }
        }

        // Get system font collection
        wil::com_ptr<IDWriteFontCollection> collection;
        {
            HRESULT hr = factory->GetSystemFontCollection(collection.put(), FALSE);
            if (FAILED(hr))
            {
                result.errorMessage = L"GetSystemFontCollection failed: hr=" + std::to_wstring(hr);
                return result;
            }
        }

        // Find the family
        UINT32 familyIndex{};
        BOOL familyFound{};
        HRESULT hr = collection->FindFamilyName(familyName.c_str(), &familyIndex, &familyFound);
        if (FAILED(hr) || !familyFound)
        {
            result.errorMessage = L"Font family not found: " + familyName;
            return result;
        }

        wil::com_ptr<IDWriteFontFamily> family;
        hr = collection->GetFontFamily(familyIndex, family.put());
        if (FAILED(hr))
        {
            result.errorMessage = L"GetFontFamily failed: hr=" + std::to_wstring(hr);
            return result;
        }

        result.resolvedFamilyName = familyName;

        // Match the best static font for the given weight
        const auto dwriteWeight = static_cast<DWRITE_FONT_WEIGHT>(weight);
        wil::com_ptr<IDWriteFont> font;
        hr = family->GetFirstMatchingFont(dwriteWeight, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, font.put());
        if (FAILED(hr) || !font)
        {
            result.errorMessage = L"No matching font face found in family";
            return result;
        }

        // Create the base font face
        wil::com_ptr<IDWriteFontFace> baseFace;
        hr = font->CreateFontFace(baseFace.put());
        if (FAILED(hr))
        {
            result.errorMessage = L"CreateFontFace failed: hr=" + std::to_wstring(hr);
            return result;
        }

        // Try to use IDWriteFontFace5 for variable font instance with effective axes
        wil::com_ptr<IDWriteFontFace> resolvedFace;
        auto face5 = baseFace.try_query<IDWriteFontFace5>();
        if (face5)
        {
            // Build axis values — honour explicit wght axis over weight param
            std::vector<DWRITE_FONT_AXIS_VALUE> axisValues;
            bool hasExplicitWght = false;

            if (axes)
            {
                for (const auto& pair : axes)
                {
                    const auto tagStr = pair.Key();
                    if (tagStr.size() == 4)
                    {
                        const UINT32 tag = DWRITE_MAKE_OPENTYPE_TAG(
                            static_cast<UINT8>(tagStr[0]),
                            static_cast<UINT8>(tagStr[1]),
                            static_cast<UINT8>(tagStr[2]),
                            static_cast<UINT8>(tagStr[3]));
                        DWRITE_FONT_AXIS_VALUE axis{};
                        axis.axisTag = static_cast<DWRITE_FONT_AXIS_TAG>(tag);
                        axis.value = pair.Value();
                        axisValues.push_back(axis);
                        if (tag == DWRITE_FONT_AXIS_TAG_WEIGHT)
                            hasExplicitWght = true;
                    }
                }
            }

            if (!hasExplicitWght && weight != 0)
            {
                DWRITE_FONT_AXIS_VALUE axis{};
                axis.axisTag = DWRITE_FONT_AXIS_TAG_WEIGHT;
                axis.value = static_cast<float>(weight);
                axisValues.push_back(axis);
            }

            // Try IDWriteFontResource to create a face with effective axes
            wil::com_ptr<IDWriteFontResource> fontResource;
            if (SUCCEEDED(face5->GetFontResource(fontResource.put())) && fontResource)
            {
                wil::com_ptr<IDWriteFontFace5> varFace;
                if (SUCCEEDED(fontResource->CreateFontFace(
                        DWRITE_FONT_SIMULATIONS_NONE,
                        axisValues.data(),
                        static_cast<UINT32>(axisValues.size()),
                        varFace.put())) &&
                    varFace)
                {
                    resolvedFace = varFace.query<IDWriteFontFace>();
                    result.isVariable = true;
                }
            }
        }

        if (!resolvedFace)
            resolvedFace = baseFace;

        // Read PostScript name for diagnostics
        {
            wil::com_ptr<IDWriteLocalizedStrings> psNames;
            BOOL exists{};
            if (auto face3 = resolvedFace.try_query<IDWriteFontFace3>())
            {
                if (SUCCEEDED(face3->GetInformationalStrings(
                        DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME, psNames.put(), &exists)) &&
                    exists && psNames)
                {
                    UINT32 len{};
                    if (SUCCEEDED(psNames->GetStringLength(0, &len)))
                    {
                        std::wstring ps(len, L'\0');
                        if (SUCCEEDED(psNames->GetString(0, ps.data(), len + 1)))
                            result.resolvedPostScriptName = std::move(ps);
                    }
                }
            }
        }

        // Read DWRITE_FONT_METRICS
        DWRITE_FONT_METRICS metrics{};
        resolvedFace->GetMetrics(&metrics);
        result.unitsPerEm = metrics.designUnitsPerEm;
        result.ascentUnits = metrics.ascent;
        result.descentUnits = metrics.descent;
        result.lineGapUnits = metrics.lineGap;
        result.lineHeightUnits = metrics.ascent + metrics.descent + metrics.lineGap;

        if (result.unitsPerEm == 0)
        {
            result.errorMessage = L"Font reports unitsPerEm=0";
            return result;
        }

        // Get advance width of '0' (U+0030)
        {
            const UINT32 cp = L'0';
            UINT16 glyphId{};
            if (SUCCEEDED(resolvedFace->GetGlyphIndicesW(&cp, 1, &glyphId)) && glyphId != 0)
            {
                DWRITE_GLYPH_METRICS gm{};
                if (SUCCEEDED(resolvedFace->GetDesignGlyphMetrics(&glyphId, 1, &gm, FALSE)))
                {
                    result.cellAdvanceUnits = gm.advanceWidth;
                    result.cellWidthUnits = gm.advanceWidth;
                }
            }
        }

        // Compute point-size recommendations
        if (result.cellWidthUnits > 0)
        {
            Microsoft::Console::Render::PixelFontDesignMetrics dm{};
            dm.unitsPerEm = result.unitsPerEm;
            dm.lineHeightUnits = result.lineHeightUnits;
            dm.cellWidthUnits = result.cellWidthUnits;

            if (dpi <= 0.0f)
                dpi = 96.0f;

            try
            {
                result.pointSizeResult = Microsoft::Console::Render::ComputePixelFontPointSizes(dm, dpi);
            }
            catch (const std::exception&)
            {
                // Point-size recommendation is best-effort; don't fail the whole analysis
            }
        }

        result.succeeded = true;
        return result;
    }

} // namespace Microsoft::Terminal::Settings::Editor
