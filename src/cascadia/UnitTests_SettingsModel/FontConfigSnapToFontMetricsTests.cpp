// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"

#include "../TerminalSettingsModel/CascadiaSettings.h"
#include "../TerminalSettingsModel/FontConfig.h"
#include "JsonTestClass.h"

using namespace Microsoft::Console;
using namespace winrt::Microsoft::Terminal::Settings::Model;
using namespace winrt::Windows::Foundation;
using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;

namespace SettingsModelUnitTests
{
    class FontConfigSnapToFontMetricsTests : public JsonTestClass
    {
        TEST_CLASS(FontConfigSnapToFontMetricsTests);

        TEST_METHOD(DefaultValueIsFalse);
        TEST_METHOD(AbsentKeyYieldsDefault);
        TEST_METHOD(JsonTrueYieldsTrue);
        TEST_METHOD(JsonFalseYieldsFalse);
        TEST_METHOD(SerializationRoundTrip);
        TEST_METHOD(ClearRestoresInheritance);
        TEST_METHOD(ParentInheritance);
        TEST_METHOD(ExplicitChildOverride);
        TEST_METHOD(ProfileDuplicationCopiesSnapToFontMetrics);
        TEST_METHOD(LegacyPixelFontTrueMapsTrue);
        TEST_METHOD(LegacyPixelFontFalseMapsFalse);
        TEST_METHOD(LegacyPixelFontNullMapsFalse);
        TEST_METHOD(NewKeyWinsOverLegacy);
    };

    // The default value of SnapToFontMetrics is false.
    void FontConfigSnapToFontMetricsTests::DefaultValueIsFalse()
    {
        static constexpr std::string_view settingsJson{ R"({
            "profiles": { "list": [ { "name": "p0", "guid": "{00000000-0000-0000-0000-000000000001}" } ] }
        })" };
        const auto settings = winrt::make_self<winrt::Microsoft::Terminal::Settings::Model::implementation::CascadiaSettings>(settingsJson);
        const auto profile = settings->AllProfiles().GetAt(0);
        VERIFY_ARE_EQUAL(false, profile.FontInfo().SnapToFontMetrics());
    }

    // When "experimental.snapToFontMetrics" is absent, HasSnapToFontMetrics() is false and SnapToFontMetrics() returns false (default).
    void FontConfigSnapToFontMetricsTests::AbsentKeyYieldsDefault()
    {
        static constexpr std::string_view settingsJson{ R"({
            "profiles": { "list": [ { "name": "p0", "guid": "{00000000-0000-0000-0000-000000000001}" } ] }
        })" };
        const auto settings = winrt::make_self<winrt::Microsoft::Terminal::Settings::Model::implementation::CascadiaSettings>(settingsJson);
        const auto profile = settings->AllProfiles().GetAt(0);
        VERIFY_IS_FALSE(profile.FontInfo().HasSnapToFontMetrics());
        VERIFY_ARE_EQUAL(false, profile.FontInfo().SnapToFontMetrics());
    }

    // JSON "experimental.snapToFontMetrics": true → SnapToFontMetrics() returns true.
    void FontConfigSnapToFontMetricsTests::JsonTrueYieldsTrue()
    {
        static constexpr std::string_view settingsJson{ R"({
            "profiles": { "list": [ {
                "name": "p0", "guid": "{00000000-0000-0000-0000-000000000001}",
                "font": { "experimental.snapToFontMetrics": true }
            } ] }
        })" };
        const auto settings = winrt::make_self<winrt::Microsoft::Terminal::Settings::Model::implementation::CascadiaSettings>(settingsJson);
        const auto profile = settings->AllProfiles().GetAt(0);
        VERIFY_IS_TRUE(profile.FontInfo().HasSnapToFontMetrics());
        VERIFY_ARE_EQUAL(true, profile.FontInfo().SnapToFontMetrics());
    }

    // JSON "experimental.snapToFontMetrics": false → SnapToFontMetrics() returns false, HasSnapToFontMetrics() is true.
    void FontConfigSnapToFontMetricsTests::JsonFalseYieldsFalse()
    {
        static constexpr std::string_view settingsJson{ R"({
            "profiles": { "list": [ {
                "name": "p0", "guid": "{00000000-0000-0000-0000-000000000001}",
                "font": { "experimental.snapToFontMetrics": false }
            } ] }
        })" };
        const auto settings = winrt::make_self<winrt::Microsoft::Terminal::Settings::Model::implementation::CascadiaSettings>(settingsJson);
        const auto profile = settings->AllProfiles().GetAt(0);
        VERIFY_IS_TRUE(profile.FontInfo().HasSnapToFontMetrics());
        VERIFY_ARE_EQUAL(false, profile.FontInfo().SnapToFontMetrics());
    }

    // Setting SnapToFontMetrics to true, serializing, then deserializing recovers the same value.
    void FontConfigSnapToFontMetricsTests::SerializationRoundTrip()
    {
        static constexpr std::string_view settingsJson{ R"({
            "profiles": { "list": [ {
                "name": "p0", "guid": "{00000000-0000-0000-0000-000000000001}",
                "font": { "experimental.snapToFontMetrics": true }
            } ] }
        })" };
        auto settings = winrt::make_self<winrt::Microsoft::Terminal::Settings::Model::implementation::CascadiaSettings>(settingsJson);
        const auto profile = settings->AllProfiles().GetAt(0);

        // Serialize
        const auto fontJson = winrt::get_self<winrt::Microsoft::Terminal::Settings::Model::implementation::FontConfig>(profile.FontInfo())->ToJson();
        const auto fontJsonStr = toString(fontJson);

        // The serialized JSON must contain the new key, not the legacy key
        VERIFY_IS_TRUE(fontJsonStr.find("experimental.snapToFontMetrics") != std::string::npos);
        VERIFY_IS_TRUE(fontJsonStr.find("experimental.pixelFont") == std::string::npos);

        // Deserialize into a fresh profile and verify
        static constexpr std::string_view settingsJson2{ R"({
            "profiles": { "list": [ {
                "name": "p0", "guid": "{00000000-0000-0000-0000-000000000001}"
            } ] }
        })" };
        auto settings2 = winrt::make_self<winrt::Microsoft::Terminal::Settings::Model::implementation::CascadiaSettings>(settingsJson2);
        const auto profile2 = settings2->AllProfiles().GetAt(0);

        Json::Value wrapper{ Json::ValueType::objectValue };
        wrapper["font"] = fontJson;
        winrt::get_self<winrt::Microsoft::Terminal::Settings::Model::implementation::FontConfig>(profile2.FontInfo())->LayerJson(wrapper);

        VERIFY_IS_TRUE(profile2.FontInfo().HasSnapToFontMetrics());
        VERIFY_ARE_EQUAL(true, profile2.FontInfo().SnapToFontMetrics());
    }

    // After setting SnapToFontMetrics explicitly and then clearing it, the value falls back to the parent.
    void FontConfigSnapToFontMetricsTests::ClearRestoresInheritance()
    {
        static constexpr std::string_view parentJson{ R"({
            "name": "parent", "guid": "{00000000-0000-0000-0000-000000000001}",
            "font": { "experimental.snapToFontMetrics": false }
        })" };
        static constexpr std::string_view childJson{ R"({
            "name": "child", "guid": "{00000000-0000-0000-0000-000000000001}",
            "font": { "experimental.snapToFontMetrics": true }
        })" };

        const auto parentJsonV = VerifyParseSucceeded(parentJson);
        const auto childJsonV = VerifyParseSucceeded(childJson);

        auto parent = winrt::Microsoft::Terminal::Settings::Model::implementation::Profile::FromJson(parentJsonV);
        auto child = parent->CreateChild();
        child->LayerJson(childJsonV);

        // Child override: true
        VERIFY_IS_TRUE(child->FontInfo().HasSnapToFontMetrics());
        VERIFY_ARE_EQUAL(true, child->FontInfo().SnapToFontMetrics());

        // Clear the child's value → should fall back to parent (false)
        child->FontInfo().ClearSnapToFontMetrics();
        VERIFY_IS_FALSE(child->FontInfo().HasSnapToFontMetrics());
        VERIFY_ARE_EQUAL(false, child->FontInfo().SnapToFontMetrics());
    }

    // A child profile with no SnapToFontMetrics set inherits from its parent.
    void FontConfigSnapToFontMetricsTests::ParentInheritance()
    {
        static constexpr std::string_view userSettings{ R"({
            "profiles": {
                "defaults": { "font": { "experimental.snapToFontMetrics": true } },
                "list": [
                    { "name": "p0", "guid": "{00000000-0000-0000-0000-000000000001}" },
                    { "name": "p1", "guid": "{00000000-0000-0000-0000-000000000002}",
                      "font": { "experimental.snapToFontMetrics": false } }
                ]
            }
        })" };
        const auto settings = winrt::make_self<winrt::Microsoft::Terminal::Settings::Model::implementation::CascadiaSettings>(userSettings);
        const auto allProfiles = settings->AllProfiles();
        VERIFY_ARE_EQUAL(2u, allProfiles.Size());

        // p0 inherits true from defaults
        VERIFY_IS_FALSE(allProfiles.GetAt(0).FontInfo().HasSnapToFontMetrics());
        VERIFY_ARE_EQUAL(true, allProfiles.GetAt(0).FontInfo().SnapToFontMetrics());

        // p1 explicitly overrides to false
        VERIFY_IS_TRUE(allProfiles.GetAt(1).FontInfo().HasSnapToFontMetrics());
        VERIFY_ARE_EQUAL(false, allProfiles.GetAt(1).FontInfo().SnapToFontMetrics());
    }

    // A profile with explicit SnapToFontMetrics=false overrides a parent that has SnapToFontMetrics=true.
    void FontConfigSnapToFontMetricsTests::ExplicitChildOverride()
    {
        static constexpr std::string_view parentJson{ R"({
            "name": "parent", "guid": "{00000000-0000-0000-0000-000000000001}",
            "font": { "experimental.snapToFontMetrics": true }
        })" };
        static constexpr std::string_view childJson{ R"({
            "name": "child", "guid": "{00000000-0000-0000-0000-000000000001}",
            "font": { "experimental.snapToFontMetrics": false }
        })" };

        const auto parentJsonV = VerifyParseSucceeded(parentJson);
        const auto childJsonV = VerifyParseSucceeded(childJson);

        auto parent = winrt::Microsoft::Terminal::Settings::Model::implementation::Profile::FromJson(parentJsonV);
        auto child = parent->CreateChild();
        child->LayerJson(childJsonV);

        VERIFY_IS_TRUE(child->FontInfo().HasSnapToFontMetrics());
        VERIFY_ARE_EQUAL(false, child->FontInfo().SnapToFontMetrics());
    }

    // CopyFontInfo (the underlying copy used in DuplicateProfile) carries SnapToFontMetrics to the copy.
    void FontConfigSnapToFontMetricsTests::ProfileDuplicationCopiesSnapToFontMetrics()
    {
        static constexpr std::string_view profileJson{ R"({
            "name": "p0", "guid": "{00000000-0000-0000-0000-000000000001}",
            "font": { "experimental.snapToFontMetrics": true }
        })" };
        const auto jsonV = VerifyParseSucceeded(profileJson);
        auto profile = winrt::Microsoft::Terminal::Settings::Model::implementation::Profile::FromJson(jsonV);

        const auto* fontImpl = winrt::get_self<winrt::Microsoft::Terminal::Settings::Model::implementation::FontConfig>(profile->FontInfo());
        const auto copy = winrt::Microsoft::Terminal::Settings::Model::implementation::FontConfig::CopyFontInfo(fontImpl, winrt::weak_ref<winrt::Microsoft::Terminal::Settings::Model::Profile>{});

        VERIFY_IS_TRUE(copy->HasSnapToFontMetrics());
        VERIFY_ARE_EQUAL(true, copy->SnapToFontMetrics());
    }

    // Legacy "experimental.pixelFont": true maps to SnapToFontMetrics = true.
    void FontConfigSnapToFontMetricsTests::LegacyPixelFontTrueMapsTrue()
    {
        static constexpr std::string_view settingsJson{ R"({
            "profiles": { "list": [ {
                "name": "p0", "guid": "{00000000-0000-0000-0000-000000000001}",
                "font": { "experimental.pixelFont": true }
            } ] }
        })" };
        const auto settings = winrt::make_self<winrt::Microsoft::Terminal::Settings::Model::implementation::CascadiaSettings>(settingsJson);
        const auto profile = settings->AllProfiles().GetAt(0);
        VERIFY_IS_TRUE(profile.FontInfo().HasSnapToFontMetrics());
        VERIFY_ARE_EQUAL(true, profile.FontInfo().SnapToFontMetrics());
    }

    // Legacy "experimental.pixelFont": false maps to SnapToFontMetrics = false (has value, not unset).
    void FontConfigSnapToFontMetricsTests::LegacyPixelFontFalseMapsFalse()
    {
        static constexpr std::string_view settingsJson{ R"({
            "profiles": { "list": [ {
                "name": "p0", "guid": "{00000000-0000-0000-0000-000000000001}",
                "font": { "experimental.pixelFont": false }
            } ] }
        })" };
        const auto settings = winrt::make_self<winrt::Microsoft::Terminal::Settings::Model::implementation::CascadiaSettings>(settingsJson);
        const auto profile = settings->AllProfiles().GetAt(0);
        VERIFY_IS_TRUE(profile.FontInfo().HasSnapToFontMetrics());
        VERIFY_ARE_EQUAL(false, profile.FontInfo().SnapToFontMetrics());
    }

    // Legacy "experimental.pixelFont": null maps to SnapToFontMetrics = false (has value, not unset).
    void FontConfigSnapToFontMetricsTests::LegacyPixelFontNullMapsFalse()
    {
        static constexpr std::string_view settingsJson{ R"({
            "profiles": { "list": [ {
                "name": "p0", "guid": "{00000000-0000-0000-0000-000000000001}",
                "font": { "experimental.pixelFont": null }
            } ] }
        })" };
        const auto settings = winrt::make_self<winrt::Microsoft::Terminal::Settings::Model::implementation::CascadiaSettings>(settingsJson);
        const auto profile = settings->AllProfiles().GetAt(0);
        VERIFY_IS_TRUE(profile.FontInfo().HasSnapToFontMetrics());
        VERIFY_ARE_EQUAL(false, profile.FontInfo().SnapToFontMetrics());
    }

    // When both the new and legacy keys are present, the new key wins.
    void FontConfigSnapToFontMetricsTests::NewKeyWinsOverLegacy()
    {
        static constexpr std::string_view settingsJson{ R"({
            "profiles": { "list": [ {
                "name": "p0", "guid": "{00000000-0000-0000-0000-000000000001}",
                "font": {
                    "experimental.pixelFont": true,
                    "experimental.snapToFontMetrics": false
                }
            } ] }
        })" };
        const auto settings = winrt::make_self<winrt::Microsoft::Terminal::Settings::Model::implementation::CascadiaSettings>(settingsJson);
        const auto profile = settings->AllProfiles().GetAt(0);
        VERIFY_IS_TRUE(profile.FontInfo().HasSnapToFontMetrics());
        VERIFY_ARE_EQUAL(false, profile.FontInfo().SnapToFontMetrics());
    }
}
