#include "platform/WindowsAccessibilityTextProvider.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>

namespace {

using namespace effindom::v2::native;

class OwnerProvider final : public IRawElementProviderSimple {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IRawElementProviderSimple))
            *result = static_cast<IRawElementProviderSimple*>(this);
        if (*result == nullptr) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0U) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID, VARIANT* result) override {
        if (result == nullptr) return E_INVALIDARG;
        VariantInit(result);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        return S_OK;
    }
private:
    ~OwnerProvider() = default;
    std::atomic<ULONG> references_{1U};
};

class FakeTextProvider final : public NativeAccessibilityTextProvider {
public:
    bool GetInfo(std::uint64_t, NativeAccessibilityTextInfo& output) const override {
        if (obscured) return false;
        output = {revision, 4U, selection_start, selection_end, false, true};
        return true;
    }
    NativeAccessibilityTextStatus ReadRange(std::uint64_t, std::uint64_t requested_revision,
        std::uint32_t start, std::uint32_t end, std::string& output) const override {
        if (requested_revision != revision) return NativeAccessibilityTextStatus::StaleRevision;
        if (start == 0U && end == 4U) { output = text; return NativeAccessibilityTextStatus::Ok; }
        if (start == 1U && end == 3U) { output = "我😀"; return NativeAccessibilityTextStatus::Ok; }
        return NativeAccessibilityTextStatus::InvalidRange;
    }
    NativeAccessibilityTextStatus RangeRects(std::uint64_t, std::uint64_t requested_revision,
        std::uint32_t, std::uint32_t, std::vector<NativeAccessibilityTextRect>& output) const override {
        if (requested_revision != revision) return NativeAccessibilityTextStatus::StaleRevision;
        output = {{1.0f, 2.0f, 3.0f, 4.0f}};
        return NativeAccessibilityTextStatus::Ok;
    }
    NativeAccessibilityTextStatus SetSelection(std::uint64_t, std::uint64_t requested_revision,
        std::uint32_t start, std::uint32_t end) const override {
        if (requested_revision != revision) return NativeAccessibilityTextStatus::StaleRevision;
        selection_start = start;
        selection_end = end;
        return NativeAccessibilityTextStatus::Ok;
    }
    NativeAccessibilityTextStatus RevealRange(std::uint64_t, std::uint64_t requested_revision,
        std::uint32_t start, std::uint32_t end) const override {
        if (requested_revision != revision) return NativeAccessibilityTextStatus::StaleRevision;
        revealed_start = start;
        revealed_end = end;
        return NativeAccessibilityTextStatus::Ok;
    }
    NativeAccessibilityTextStatus ReplaceRange(std::uint64_t, std::uint64_t requested_revision,
        std::uint32_t start, std::uint32_t end, const std::string& replacement,
        std::uint64_t& output_revision) const override {
        if (requested_revision != revision) return NativeAccessibilityTextStatus::StaleRevision;
        if (start != 0U || end != 4U) return NativeAccessibilityTextStatus::InvalidRange;
        text = replacement;
        output_revision = ++revision;
        return NativeAccessibilityTextStatus::Ok;
    }

    mutable std::string text = "A我😀B";
    mutable std::uint64_t revision = 7U;
    mutable std::uint32_t selection_start = 1U;
    mutable std::uint32_t selection_end = 3U;
    mutable std::uint32_t revealed_start = 0U;
    mutable std::uint32_t revealed_end = 0U;
    bool obscured = false;
};

} // namespace

TEST_CASE("Windows UIA text patterns lazily query Unicode ranges and forward operations",
    "[v2][native][windows][accessibility][text]") {
    auto provider = std::make_shared<FakeTextProvider>();
    auto* owner = new OwnerProvider();
    IUnknown* unknown = nullptr;
    REQUIRE(SUCCEEDED(CreateWindowsAccessibilityTextPattern(
        nullptr, 42U, provider, owner, UIA_TextPattern2Id, &unknown)));
    REQUIRE(unknown != nullptr);

    ITextProvider2* text_provider = nullptr;
    REQUIRE(SUCCEEDED(unknown->QueryInterface(__uuidof(ITextProvider2),
        reinterpret_cast<void**>(&text_provider))));
    ITextRangeProvider* document = nullptr;
    REQUIRE(SUCCEEDED(text_provider->get_DocumentRange(&document)));
    BSTR value = nullptr;
    REQUIRE(SUCCEEDED(document->GetText(-1, &value)));
    CHECK(std::wstring(value, SysStringLen(value)) == L"A我😀B");
    SysFreeString(value);

    int moved = 0;
    REQUIRE(SUCCEEDED(document->MoveEndpointByUnit(
        TextPatternRangeEndpoint_Start, TextUnit_Character, 1, &moved)));
    REQUIRE(SUCCEEDED(document->MoveEndpointByUnit(
        TextPatternRangeEndpoint_End, TextUnit_Character, -1, &moved)));
    REQUIRE(SUCCEEDED(document->Select()));
    CHECK(provider->selection_start == 1U);
    CHECK(provider->selection_end == 3U);
    REQUIRE(SUCCEEDED(document->ScrollIntoView(TRUE)));
    CHECK(provider->revealed_start == 1U);
    CHECK(provider->revealed_end == 3U);

    ++provider->revision;
    value = nullptr;
    CHECK(document->GetText(-1, &value) == UIA_E_ELEMENTNOTAVAILABLE);
    CHECK(value == nullptr);

    document->Release();
    text_provider->Release();
    unknown->Release();

    REQUIRE(SUCCEEDED(CreateWindowsAccessibilityTextPattern(
        nullptr, 42U, provider, owner, UIA_ValuePatternId, &unknown)));
    IValueProvider* value_provider = nullptr;
    REQUIRE(SUCCEEDED(unknown->QueryInterface(__uuidof(IValueProvider),
        reinterpret_cast<void**>(&value_provider))));
    REQUIRE(SUCCEEDED(value_provider->SetValue(L"changed")));
    CHECK(provider->text == "changed");
    value_provider->Release();
    unknown->Release();

    provider->obscured = true;
    REQUIRE(SUCCEEDED(CreateWindowsAccessibilityTextPattern(
        nullptr, 42U, provider, owner, UIA_TextPatternId, &unknown)));
    CHECK(unknown == nullptr);
    owner->Release();
}

TEST_CASE("Windows accessibility text events map to the corresponding UIA events",
    "[v2][native][windows][accessibility][text]") {
    CHECK(WindowsAccessibilityTextEventId(NativeAccessibilityTextEvent::DocumentChanged) ==
        UIA_Text_TextChangedEventId);
    CHECK(WindowsAccessibilityTextEventId(NativeAccessibilityTextEvent::SelectionChanged) ==
        UIA_Text_TextSelectionChangedEventId);
}
