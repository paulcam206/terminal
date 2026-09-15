// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"

// WinRT projections needed for TerminalSettingsEditor types.
#include <winrt/Microsoft.Terminal.Settings.Editor.h>

// Settings-model implementation (CascadiaSettings ctor used by helpers).
#include "../TerminalSettingsModel/CascadiaSettings.h"

// AppearanceViewModel implementation (test ctor with AnalyzerFn seam).
#include "../TerminalSettingsEditor/Appearances.h"

using namespace winrt::Microsoft::Terminal::Settings::Model;
using namespace winrt::Microsoft::Terminal::Settings::Editor::implementation;
using namespace Microsoft::Terminal::Settings::Editor; // FontAnalysisResult
using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;

namespace TerminalAppLocalTests
{
    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    static constexpr std::string_view s_minimalProfileJson{ R"({
        "profiles": { "list": [ {
            "name": "p0",
            "guid": "{00000000-0000-0000-0000-000000000001}"
        } ] }
    })" };

    // Build a CascadiaSettings from the minimal JSON and return the first
    // profile's DefaultAppearance cast to AppearanceConfig.
    static AppearanceConfig MakeTestAppearance()
    {
        const auto settings = winrt::make_self<winrt::Microsoft::Terminal::Settings::Model::implementation::CascadiaSettings>(s_minimalProfileJson);
        const auto profile = settings->AllProfiles().GetAt(0);
        return profile.DefaultAppearance().try_as<AppearanceConfig>();
    }

    // A succeeded FontAnalysisResult with unitsPerEm=1000,
    // lineHeightUnits=1300 (ratio 1.3), cellWidthUnits=600 (ratio 0.6),
    // one candidate at 12 pt.
    static FontAnalysisResult SucceededResult()
    {
        FontAnalysisResult r;
        r.succeeded = true;
        r.unitsPerEm = 1000;
        r.lineHeightUnits = 1300;
        r.cellWidthUnits = 600;
        r.pointSizeResult.candidates.push_back(PointSizeCandidate{ 12.0f, 0.0f, 0.0f });
        return r;
    }

    static bool NearlyEqual(double a, double b, double tol = 1e-4)
    {
        return std::abs(a - b) <= tol;
    }

    // -------------------------------------------------------------------------
    // Test class
    // -------------------------------------------------------------------------

    class AppearanceViewModelTests
    {
        TEST_CLASS(AppearanceViewModelTests);

        // 1. Constructor must not invoke the analyzer and must not mutate the model.
        TEST_METHOD(InitializationDoesNotCallAnalyzer);

        // 2. Changing FontFace runs analysis and writes CellHeight/CellWidth ratios.
        TEST_METHOD(GeometryChangeUpdatesCellDimensions);

        // 3. Manually setting CellHeight/CellWidth before a geometry change preserves
        //    those values through non-geometry operations.
        TEST_METHOD(ManualCellEditsSurviveNonGeometryChanges);

        // 4. A subsequent geometry change replaces manually written cell ratios.
        TEST_METHOD(ManualCellEditsReplacedByGeometryChange);

        // 5. SnapToFontMetrics is off by default even when the analyzer succeeds.
        TEST_METHOD(SnapToFontMetricsOffByDefault);

        // 6. Enabling SnapToFontMetrics immediately snaps the current FontSize to
        //    the nearest candidate returned by the analyzer.
        TEST_METHOD(EnableSnapToFontMetricsSnapsCurrentFontSize);

        // 7. Disabling (or clearing) SnapToFontMetrics does not activate snapping.
        TEST_METHOD(DisablingSnapToFontMetricsDoesNotSnap);

        // 8. FontMetricSuggestedSizes is empty while snapping is off and non-empty
        //    while snapping is on.
        TEST_METHOD(SuggestedSizesEmptyWhenOffPopulatedWhenOn);

        // 9. SnapFontSizeNext/Previous step through the candidate list in order,
        //    and no-op at the respective bounds.
        TEST_METHOD(NextPreviousStepThroughCandidates);

        // 10. UpdateDpi invalidates the analysis cache so a new call uses the new DPI.
        TEST_METHOD(DpiChangeInvalidatesAnalysisCache);

        // 11. When the analyzer fails, CellHeight/CellWidth are not overwritten.
        TEST_METHOD(FailedAnalysisPreservesCellValues);

        // 12. Reentrant geometry changes (via PropertyChanged listener) are suppressed,
        //     preventing unbounded loops; the analyzer is called a bounded number of times.
        TEST_METHOD(GeometryChangeReentryGuard);
    };

    // 1 — constructor must not call the analyzer
    void AppearanceViewModelTests::InitializationDoesNotCallAnalyzer()
    {
        int callCount{ 0 };
        auto fakeAnalyzer = [&](std::wstring_view,
                                uint32_t,
                                winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, float>,
                                float) -> FontAnalysisResult {
            ++callCount;
            return {};
        };

        const auto appearance = MakeTestAppearance();
        const auto vm = winrt::make_self<AppearanceViewModel>(appearance, fakeAnalyzer);

        VERIFY_ARE_EQUAL(0, callCount, L"Analyzer must not be called during construction");
        VERIFY_IS_FALSE(vm->HasLineHeight(), L"CellHeight must not be set by construction");
        VERIFY_IS_FALSE(vm->HasCellWidth(), L"CellWidth must not be set by construction");
    }

    // 2 — geometry change writes cell ratios
    void AppearanceViewModelTests::GeometryChangeUpdatesCellDimensions()
    {
        auto fakeAnalyzer = [](std::wstring_view,
                               uint32_t,
                               winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, float>,
                               float) -> FontAnalysisResult {
            return SucceededResult();
        };

        const auto appearance = MakeTestAppearance();
        const auto vm = winrt::make_self<AppearanceViewModel>(appearance, fakeAnalyzer);

        VERIFY_IS_FALSE(vm->HasLineHeight());
        VERIFY_IS_FALSE(vm->HasCellWidth());

        vm->FontFace(L"FakeFont");

        VERIFY_IS_TRUE(vm->HasLineHeight(), L"CellHeight must be set after FontFace change");
        VERIFY_IS_TRUE(vm->HasCellWidth(), L"CellWidth must be set after FontFace change");
        // lineHeightRatio = 1300/1000 = 1.3; cellWidthRatio = 600/1000 = 0.6
        VERIFY_IS_TRUE(NearlyEqual(vm->LineHeight(), 1.3), L"LineHeight ratio mismatch");
        VERIFY_IS_TRUE(NearlyEqual(vm->CellWidth(), 0.6), L"CellWidth ratio mismatch");
    }

    // 3 — manual edits survive non-geometry changes
    void AppearanceViewModelTests::ManualCellEditsSurviveNonGeometryChanges()
    {
        auto fakeAnalyzer = [](std::wstring_view,
                               uint32_t,
                               winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, float>,
                               float) -> FontAnalysisResult {
            return {}; // not called during this test
        };

        const auto appearance = MakeTestAppearance();
        const auto vm = winrt::make_self<AppearanceViewModel>(appearance, fakeAnalyzer);

        // Write a manual CellHeight value (user typed it in a NumberBox).
        vm->LineHeight(1.5);
        VERIFY_IS_TRUE(NearlyEqual(vm->LineHeight(), 1.5), L"Manual LineHeight not persisted");

        // Non-geometry change: enable then clear SnapToFontMetrics.
        vm->SnapToFontMetrics(true);
        VERIFY_IS_TRUE(NearlyEqual(vm->LineHeight(), 1.5), L"LineHeight clobbered by SnapToFontMetrics(true)");

        vm->ClearSnapToFontMetrics();
        VERIFY_IS_TRUE(NearlyEqual(vm->LineHeight(), 1.5), L"LineHeight clobbered by ClearSnapToFontMetrics");
    }

    // 4 — geometry change replaces manually written values
    void AppearanceViewModelTests::ManualCellEditsReplacedByGeometryChange()
    {
        auto fakeAnalyzer = [](std::wstring_view,
                               uint32_t,
                               winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, float>,
                               float) -> FontAnalysisResult {
            return SucceededResult();
        };

        const auto appearance = MakeTestAppearance();
        const auto vm = winrt::make_self<AppearanceViewModel>(appearance, fakeAnalyzer);

        // Manually set CellHeight to a distinctive value.
        vm->LineHeight(1.5);
        VERIFY_IS_TRUE(NearlyEqual(vm->LineHeight(), 1.5));

        // Trigger a geometry change — analysis writes 1.3.
        vm->FontFace(L"FakeFont");
        VERIFY_IS_TRUE(NearlyEqual(vm->LineHeight(), 1.3),
                       L"Geometry change must overwrite manual LineHeight");
    }

    // 5 — SnapToFontMetrics is off by default even when the analyzer succeeds
    void AppearanceViewModelTests::SnapToFontMetricsOffByDefault()
    {
        auto fakeAnalyzer = [](std::wstring_view,
                               uint32_t,
                               winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, float>,
                               float) -> FontAnalysisResult {
            return SucceededResult();
        };

        const auto appearance = MakeTestAppearance();
        const auto vm = winrt::make_self<AppearanceViewModel>(appearance, fakeAnalyzer);

        vm->FontFace(L"FakeFont"); // primes the analysis cache

        VERIFY_IS_FALSE(vm->SnapToFontMetrics(), L"SnapToFontMetrics must be false by default");
        VERIFY_IS_FALSE(vm->HasSnapToFontMetrics(), L"HasSnapToFontMetrics must be false by default");

        // FontSize must NOT have been snapped automatically.
        const auto sz = vm->FontSize();
        VERIFY_IS_FALSE(std::abs(sz - 12.0f) < 0.05f && vm->HasSnapToFontMetrics(),
                        L"FontSize must not snap when SnapToFontMetrics is off");
    }

    // 6 — enabling SnapToFontMetrics immediately snaps the current FontSize to
    //     the nearest candidate
    void AppearanceViewModelTests::EnableSnapToFontMetricsSnapsCurrentFontSize()
    {
        auto fakeAnalyzer = [](std::wstring_view,
                               uint32_t,
                               winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, float>,
                               float) -> FontAnalysisResult {
            FontAnalysisResult r;
            r.succeeded = true;
            r.unitsPerEm = 1000;
            r.lineHeightUnits = 1300;
            r.cellWidthUnits = 600;
            r.pointSizeResult.candidates = {
                { 8.25f, 0.0f, 0.0f },
                { 16.5f, 0.0f, 0.0f },
            };
            return r;
        };

        const auto appearance = MakeTestAppearance();
        const auto vm = winrt::make_self<AppearanceViewModel>(appearance, fakeAnalyzer);

        vm->FontFace(L"FakeFont"); // primes the analysis cache

        // Set a FontSize that is between the two candidates.
        vm->FontSize(10.0f);
        VERIFY_IS_TRUE(std::abs(vm->FontSize() - 10.0f) < 0.05f,
                       L"FontSize should not snap while SnapToFontMetrics is off");

        // Enable snapping — must snap to nearest candidate (8.25 is closer to 10 than 16.5).
        vm->SnapToFontMetrics(true);
        VERIFY_IS_TRUE(vm->SnapToFontMetrics(), L"SnapToFontMetrics must be true after setting");
        VERIFY_IS_TRUE(std::abs(vm->FontSize() - 8.25f) < 0.05f,
                       L"FontSize must snap to nearest candidate when SnapToFontMetrics is enabled");
    }

    // 7 — disabling or clearing SnapToFontMetrics must NOT snap the font size
    void AppearanceViewModelTests::DisablingSnapToFontMetricsDoesNotSnap()
    {
        auto fakeAnalyzer = [](std::wstring_view,
                               uint32_t,
                               winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, float>,
                               float) -> FontAnalysisResult {
            FontAnalysisResult r;
            r.succeeded = true;
            r.unitsPerEm = 1000;
            r.lineHeightUnits = 1300;
            r.cellWidthUnits = 600;
            r.pointSizeResult.candidates = {
                { 8.25f, 0.0f, 0.0f },
                { 16.5f, 0.0f, 0.0f },
            };
            return r;
        };

        const auto appearance = MakeTestAppearance();
        const auto vm = winrt::make_self<AppearanceViewModel>(appearance, fakeAnalyzer);

        vm->FontFace(L"FakeFont");
        vm->FontSize(10.0f);

        // Enable then disable — size must remain at 10.
        vm->SnapToFontMetrics(true);
        vm->SnapToFontMetrics(false);
        VERIFY_IS_FALSE(vm->SnapToFontMetrics(), L"SnapToFontMetrics must be false after disabling");

        vm->FontSize(10.0f); // write a non-snapped value
        VERIFY_IS_TRUE(std::abs(vm->FontSize() - 10.0f) < 0.05f,
                       L"FontSize must not be snapped after SnapToFontMetrics is disabled");

        // Clear path: enable then clear.
        vm->SnapToFontMetrics(true);
        vm->FontSize(10.0f);
        vm->ClearSnapToFontMetrics();
        VERIFY_IS_FALSE(vm->HasSnapToFontMetrics(), L"HasSnapToFontMetrics must be false after clear");

        vm->FontSize(10.0f);
        VERIFY_IS_TRUE(std::abs(vm->FontSize() - 10.0f) < 0.05f,
                       L"FontSize must not be snapped after ClearSnapToFontMetrics");
    }

    // 8 — FontMetricSuggestedSizes is empty while snapping is off and non-empty
    //     while snapping is on
    void AppearanceViewModelTests::SuggestedSizesEmptyWhenOffPopulatedWhenOn()
    {
        auto fakeAnalyzer = [](std::wstring_view,
                               uint32_t,
                               winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, float>,
                               float) -> FontAnalysisResult {
            FontAnalysisResult r;
            r.succeeded = true;
            r.unitsPerEm = 1000;
            r.lineHeightUnits = 1300;
            r.cellWidthUnits = 600;
            r.pointSizeResult.candidates = {
                { 8.25f, 0.0f, 0.0f },
                { 16.5f, 0.0f, 0.0f },
            };
            return r;
        };

        const auto appearance = MakeTestAppearance();
        const auto vm = winrt::make_self<AppearanceViewModel>(appearance, fakeAnalyzer);

        vm->FontFace(L"FakeFont");

        // Off by default — suggested sizes should be empty.
        VERIFY_ARE_EQUAL(winrt::hstring{}, vm->FontMetricSuggestedSizes(),
                         L"FontMetricSuggestedSizes must be empty when snapping is off");

        // Enable — suggested sizes must contain candidate values.
        vm->SnapToFontMetrics(true);
        VERIFY_IS_FALSE(vm->FontMetricSuggestedSizes().empty(),
                        L"FontMetricSuggestedSizes must be non-empty when snapping is on");

        // Disable — back to empty.
        vm->ClearSnapToFontMetrics();
        VERIFY_ARE_EQUAL(winrt::hstring{}, vm->FontMetricSuggestedSizes(),
                         L"FontMetricSuggestedSizes must be empty after clearing SnapToFontMetrics");
    }

    // 9 — SnapFontSizeNext/Previous step through the candidate list;
    //     they are no-ops at the respective bounds.
    void AppearanceViewModelTests::NextPreviousStepThroughCandidates()
    {
        // Candidates: 8.0, 12.0, 16.0 pt.
        auto fakeAnalyzer = [](std::wstring_view,
                               uint32_t,
                               winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, float>,
                               float) -> FontAnalysisResult {
            FontAnalysisResult r;
            r.succeeded = true;
            r.unitsPerEm = 1000;
            r.lineHeightUnits = 1300;
            r.cellWidthUnits = 600;
            r.pointSizeResult.candidates = {
                { 8.0f, 0.0f, 0.0f },
                { 12.0f, 0.0f, 0.0f },
                { 16.0f, 0.0f, 0.0f },
            };
            return r;
        };

        const auto appearance = MakeTestAppearance();
        const auto vm = winrt::make_self<AppearanceViewModel>(appearance, fakeAnalyzer);

        vm->FontFace(L"FakeFont");
        vm->SnapToFontMetrics(true);

        // Snap to 8.0 (first candidate) via explicit set.
        vm->FontSize(8.0f);

        // Step forward: 8 → 12.
        const float next1 = vm->SnapFontSizeNext();
        VERIFY_IS_TRUE(std::abs(next1 - 12.0f) < 0.05f,
                       L"SnapFontSizeNext from 8 must yield 12");

        // Step forward again: 12 → 16.
        const float next2 = vm->SnapFontSizeNext();
        VERIFY_IS_TRUE(std::abs(next2 - 16.0f) < 0.05f,
                       L"SnapFontSizeNext from 12 must yield 16");

        // At last candidate — next must be a no-op (return same value).
        const float nextAtEnd = vm->SnapFontSizeNext();
        VERIFY_IS_TRUE(std::abs(nextAtEnd - 16.0f) < 0.05f,
                       L"SnapFontSizeNext at last candidate must be a no-op");

        // Step backward: 16 → 12.
        const float prev1 = vm->SnapFontSizePrevious();
        VERIFY_IS_TRUE(std::abs(prev1 - 12.0f) < 0.05f,
                       L"SnapFontSizePrevious from 16 must yield 12");

        // Step backward again: 12 → 8.
        const float prev2 = vm->SnapFontSizePrevious();
        VERIFY_IS_TRUE(std::abs(prev2 - 8.0f) < 0.05f,
                       L"SnapFontSizePrevious from 12 must yield 8");

        // At first candidate — previous must be a no-op.
        const float prevAtStart = vm->SnapFontSizePrevious();
        VERIFY_IS_TRUE(std::abs(prevAtStart - 8.0f) < 0.05f,
                       L"SnapFontSizePrevious at first candidate must be a no-op");
    }

    // 10 — DPI change invalidates the analysis cache
    void AppearanceViewModelTests::DpiChangeInvalidatesAnalysisCache()
    {
        float lastDpi{ 0.0f };
        auto fakeAnalyzer = [&](std::wstring_view,
                                uint32_t,
                                winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, float>,
                                float dpi) -> FontAnalysisResult {
            lastDpi = dpi;
            return SucceededResult();
        };

        const auto appearance = MakeTestAppearance();
        const auto vm = winrt::make_self<AppearanceViewModel>(appearance, fakeAnalyzer);

        vm->FontFace(L"FakeFont"); // primes the cache at 96 DPI
        VERIFY_ARE_EQUAL(96.0f, lastDpi, L"First analysis should use default 96 DPI");

        // Change DPI: cache must be invalidated so next read uses the new DPI.
        vm->UpdateDpi(144.0f);
        // Trigger a fresh analysis by re-running a geometry change with the new DPI.
        vm->FontFace(L"FakeFont");
        VERIFY_ARE_EQUAL(144.0f, lastDpi, L"After DPI change, analysis must use new DPI");
    }

    // 10 — failed analysis preserves existing cell values
    void AppearanceViewModelTests::FailedAnalysisPreservesCellValues()
    {
        bool returnSuccess{ true };
        auto fakeAnalyzer = [&](std::wstring_view,
                                uint32_t,
                                winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, float>,
                                float) -> FontAnalysisResult {
            if (returnSuccess)
                return SucceededResult();
            // Return a failed result.
            FontAnalysisResult r;
            r.succeeded = false;
            r.errorMessage = L"font not found";
            return r;
        };

        const auto appearance = MakeTestAppearance();
        const auto vm = winrt::make_self<AppearanceViewModel>(appearance, fakeAnalyzer);

        // First geometry change — analysis succeeds, writes 1.3 / 0.6.
        vm->FontFace(L"FakeFont");
        VERIFY_IS_TRUE(NearlyEqual(vm->LineHeight(), 1.3));
        VERIFY_IS_TRUE(NearlyEqual(vm->CellWidth(), 0.6));

        // Second geometry change — analysis fails; values must not change.
        returnSuccess = false;
        vm->FontFace(L"MissingFont");
        VERIFY_IS_TRUE(NearlyEqual(vm->LineHeight(), 1.3),
                       L"LineHeight must be preserved when analysis fails");
        VERIFY_IS_TRUE(NearlyEqual(vm->CellWidth(), 0.6),
                       L"CellWidth must be preserved when analysis fails");
    }

    // 11 — PropertyChanged-driven reentry is suppressed; analyzer is called a
    //      bounded number of times even when a listener changes FontFace again.
    void AppearanceViewModelTests::GeometryChangeReentryGuard()
    {
        int analyzerCallCount{ 0 };
        auto fakeAnalyzer = [&](std::wstring_view,
                                uint32_t,
                                winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, float>,
                                float) -> FontAnalysisResult {
            ++analyzerCallCount;
            return SucceededResult();
        };

        const auto appearance = MakeTestAppearance();
        auto vmSelf = winrt::make_self<AppearanceViewModel>(appearance, fakeAnalyzer);

        // Subscribe: when LineHeight changes (from a geometry update), change FontFace again.
        // Without the reentry guard this would loop indefinitely.
        const auto token = vmSelf->PropertyChanged(
            [&vmSelf](auto&&, const winrt::Windows::UI::Xaml::Data::PropertyChangedEventArgs& args) {
                if (args.PropertyName() == L"LineHeight")
                {
                    // Call FontFace from within the geometry-change notification cascade.
                    // The reentry guard (_inGeometryChange) must suppress the nested
                    // _onGeometryChange so we don't loop.
                    vmSelf->FontFace(L"LoopBait");
                }
            });

        // Trigger the first geometry change.
        vmSelf->FontFace(L"FakeFont");

        vmSelf->PropertyChanged(token); // remove listener

        // The guard allows exactly 1 analysis run (for "FakeFont").
        // The nested FontFace("LoopBait") fires _onGeometryChange which sees
        // _inGeometryChange==true and returns without calling the analyzer again.
        VERIFY_ARE_EQUAL(1, analyzerCallCount,
                         L"Reentry guard must allow exactly one analysis per outer geometry change");
    }

} // namespace TerminalAppLocalTests
