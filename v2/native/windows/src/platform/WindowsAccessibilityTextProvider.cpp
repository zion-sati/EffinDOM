#include "WindowsAccessibilityTextProvider.h"

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

namespace effindom::v2::native {
namespace {

HRESULT StatusResult(NativeAccessibilityTextStatus status) {
    switch (status) {
        case NativeAccessibilityTextStatus::Ok: return S_OK;
        case NativeAccessibilityTextStatus::StaleRevision:
        case NativeAccessibilityTextStatus::NotText: return UIA_E_ELEMENTNOTAVAILABLE;
        case NativeAccessibilityTextStatus::Obscured:
        case NativeAccessibilityTextStatus::ReadOnly: return UIA_E_NOTSUPPORTED;
        case NativeAccessibilityTextStatus::InvalidRange:
        case NativeAccessibilityTextStatus::InvalidText: return E_INVALIDARG;
    }
    return E_FAIL;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring output(static_cast<std::size_t>(length), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), output.data(), length) == length ? output : std::wstring{};
}

std::string WideToUtf8(const wchar_t* value, int source_length = -1) {
    if (value == nullptr) return {};
    if (source_length < 0) source_length = static_cast<int>(std::wcslen(value));
    if (source_length == 0) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, source_length,
        nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string output(static_cast<std::size_t>(length), '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, source_length,
        output.data(), length, nullptr, nullptr) == length ? output : std::string{};
}

std::uint32_t CountUtf8Characters(const std::string& text) {
    std::uint32_t count = 0U;
    for (unsigned char byte : text) if ((byte & 0xC0U) != 0x80U) ++count;
    return count;
}

class TextRange final : public ITextRangeProvider {
public:
    TextRange(HWND window, std::uint64_t handle,
        std::shared_ptr<NativeAccessibilityTextProvider> provider,
        IRawElementProviderSimple* owner, std::uint64_t revision,
        std::uint32_t start, std::uint32_t end)
        : window_(window), handle_(handle), provider_(std::move(provider)), owner_(owner),
          revision_(revision), start_(start), end_(end) {
        owner_->AddRef();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(ITextRangeProvider))
            *result = static_cast<ITextRangeProvider*>(this);
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
    HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = new TextRange(window_, handle_, provider_, owner_, revision_, start_, end_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider* other, BOOL* result) override {
        if (other == nullptr || result == nullptr) return E_INVALIDARG;
        const auto* range = dynamic_cast<TextRange*>(other);
        *result = range != nullptr && range->handle_ == handle_ && range->start_ == start_ &&
            range->end_ == end_ ? TRUE : FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CompareEndpoints(TextPatternRangeEndpoint endpoint,
        ITextRangeProvider* other, TextPatternRangeEndpoint other_endpoint, int* result) override {
        if (other == nullptr || result == nullptr) return E_INVALIDARG;
        const auto* range = dynamic_cast<TextRange*>(other);
        if (range == nullptr || range->handle_ != handle_) return E_INVALIDARG;
        const auto left = endpoint == TextPatternRangeEndpoint_Start ? start_ : end_;
        const auto right = other_endpoint == TextPatternRangeEndpoint_Start ? range->start_ : range->end_;
        *result = left < right ? -1 : left > right ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit) override {
        NativeAccessibilityTextInfo info;
        const HRESULT status = CurrentInfo(info);
        if (FAILED(status)) return status;
        if (unit == TextUnit_Character) {
            start_ = std::min(start_, info.character_count);
            end_ = std::min(start_ + 1U, info.character_count);
        } else {
            start_ = 0U;
            end_ = info.character_count;
        }
        revision_ = info.revision;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID, VARIANT, BOOL,
        ITextRangeProvider** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FindText(BSTR text, BOOL backward, BOOL ignore_case,
        ITextRangeProvider** result) override {
        if (text == nullptr || result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        std::string utf8;
        const auto read = provider_->ReadRange(handle_, revision_, start_, end_, utf8);
        if (read != NativeAccessibilityTextStatus::Ok) return StatusResult(read);
        std::wstring haystack = Utf8ToWide(utf8);
        std::wstring needle(text, SysStringLen(text));
        if (ignore_case) {
            CharLowerBuffW(haystack.data(), static_cast<DWORD>(haystack.size()));
            CharLowerBuffW(needle.data(), static_cast<DWORD>(needle.size()));
        }
        const auto found = backward ? haystack.rfind(needle) : haystack.find(needle);
        if (found == std::wstring::npos) return S_OK;
        const auto prefix = WideToUtf8(haystack.data(), static_cast<int>(found));
        const auto match = WideToUtf8(haystack.data() + found, static_cast<int>(needle.size()));
        const auto match_start = start_ + CountUtf8Characters(prefix);
        *result = new TextRange(window_, handle_, provider_, owner_, revision_, match_start,
            match_start + CountUtf8Characters(match));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID, VARIANT* result) override {
        if (result == nullptr) return E_INVALIDARG;
        VariantInit(result);
        result->vt = VT_UNKNOWN;
        return UiaGetReservedNotSupportedValue(&result->punkVal);
    }
    HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        std::vector<NativeAccessibilityTextRect> rects;
        const auto query = provider_->RangeRects(handle_, revision_, start_, end_, rects);
        if (query != NativeAccessibilityTextStatus::Ok) return StatusResult(query);
        SAFEARRAY* values = SafeArrayCreateVector(VT_R8, 0, static_cast<ULONG>(rects.size() * 4U));
        if (values == nullptr) return E_OUTOFMEMORY;
        double* data = nullptr;
        const HRESULT access = SafeArrayAccessData(values, reinterpret_cast<void**>(&data));
        if (FAILED(access)) { SafeArrayDestroy(values); return access; }
        POINT origin{0, 0};
        ClientToScreen(window_, &origin);
        const double scale = static_cast<double>(GetDpiForWindow(window_)) / 96.0;
        for (std::size_t index = 0U; index < rects.size(); ++index) {
            data[index * 4U] = origin.x + rects[index].x * scale;
            data[index * 4U + 1U] = origin.y + rects[index].y * scale;
            data[index * 4U + 2U] = rects[index].width * scale;
            data[index * 4U + 3U] = rects[index].height * scale;
        }
        SafeArrayUnaccessData(values);
        *result = values;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEnclosingElement(IRawElementProviderSimple** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = owner_;
        owner_->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetText(int max_length, BSTR* result) override {
        if (result == nullptr || max_length < -1) return E_INVALIDARG;
        *result = nullptr;
        std::string utf8;
        const auto read = provider_->ReadRange(handle_, revision_, start_, end_, utf8);
        if (read != NativeAccessibilityTextStatus::Ok) return StatusResult(read);
        std::wstring wide = Utf8ToWide(utf8);
        if (max_length >= 0 && wide.size() > static_cast<std::size_t>(max_length)) {
            std::size_t length = static_cast<std::size_t>(max_length);
            if (length > 0U && wide[length - 1U] >= 0xD800 && wide[length - 1U] <= 0xDBFF) --length;
            wide.resize(length);
        }
        *result = SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
        return *result != nullptr || wide.empty() ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count, int* moved) override {
        if (moved == nullptr) return E_INVALIDARG;
        NativeAccessibilityTextInfo info;
        const HRESULT status = CurrentInfo(info);
        if (FAILED(status)) return status;
        const bool character_unit = unit == TextUnit_Character;
        const auto old = start_;
        const std::int64_t delta = !character_unit ?
            (count < 0 ? -static_cast<std::int64_t>(info.character_count) : info.character_count) : count;
        start_ = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(start_) + delta, 0, info.character_count));
        end_ = start_;
        *moved = start_ == old ? 0 : count < 0 ? -1 : 1;
        revision_ = info.revision;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(TextPatternRangeEndpoint endpoint,
        TextUnit unit, int count, int* moved) override {
        if (moved == nullptr) return E_INVALIDARG;
        NativeAccessibilityTextInfo info;
        const HRESULT status = CurrentInfo(info);
        if (FAILED(status)) return status;
        const bool character_unit = unit == TextUnit_Character;
        auto& target = endpoint == TextPatternRangeEndpoint_Start ? start_ : end_;
        const auto old = target;
        const std::int64_t delta = !character_unit ?
            (count < 0 ? -static_cast<std::int64_t>(info.character_count) : info.character_count) : count;
        target = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(target) + delta, 0, info.character_count));
        if (start_ > end_) endpoint == TextPatternRangeEndpoint_Start ? end_ = start_ : start_ = end_;
        *moved = static_cast<int>(target) - static_cast<int>(old);
        revision_ = info.revision;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveEndpointByRange(TextPatternRangeEndpoint endpoint,
        ITextRangeProvider* other, TextPatternRangeEndpoint other_endpoint) override {
        if (other == nullptr) return E_INVALIDARG;
        const auto* range = dynamic_cast<TextRange*>(other);
        if (range == nullptr || range->handle_ != handle_) return E_INVALIDARG;
        auto& target = endpoint == TextPatternRangeEndpoint_Start ? start_ : end_;
        target = other_endpoint == TextPatternRangeEndpoint_Start ? range->start_ : range->end_;
        if (start_ > end_) endpoint == TextPatternRangeEndpoint_Start ? end_ = start_ : start_ = end_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Select() override {
        return StatusResult(provider_->SetSelection(handle_, revision_, start_, end_));
    }
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL) override {
        return StatusResult(provider_->RevealRange(handle_, revision_, start_, end_));
    }
    HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
        return *result != nullptr ? S_OK : E_OUTOFMEMORY;
    }

private:
    ~TextRange() { owner_->Release(); }
    HRESULT CurrentInfo(NativeAccessibilityTextInfo& info) const {
        if (!provider_->GetInfo(handle_, info) || info.revision != revision_)
            return UIA_E_ELEMENTNOTAVAILABLE;
        return S_OK;
    }
    std::atomic<ULONG> references_{1U};
    HWND window_;
    std::uint64_t handle_;
    std::shared_ptr<NativeAccessibilityTextProvider> provider_;
    IRawElementProviderSimple* owner_;
    std::uint64_t revision_;
    std::uint32_t start_;
    std::uint32_t end_;
};

class TextPatternProvider final : public ITextProvider2, public IValueProvider {
public:
    TextPatternProvider(HWND window, std::uint64_t handle,
        std::shared_ptr<NativeAccessibilityTextProvider> provider, IRawElementProviderSimple* owner)
        : window_(window), handle_(handle), provider_(std::move(provider)), owner_(owner) { owner_->AddRef(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(ITextProvider) || id == __uuidof(ITextProvider2))
            *result = static_cast<ITextProvider2*>(this);
        else if (id == __uuidof(IValueProvider)) *result = static_cast<IValueProvider*>(this);
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
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** result) override {
        NativeAccessibilityTextInfo info;
        return Info(info) ? OneRange(info, info.selection_start, info.selection_end, result) : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY** result) override {
        NativeAccessibilityTextInfo info;
        return Info(info) ? OneRange(info, 0U, info.character_count, result) : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE RangeFromChild(IRawElementProviderSimple*, ITextRangeProvider** result) override {
        if (result == nullptr) return E_INVALIDARG; *result = nullptr; return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE RangeFromPoint(UiaPoint, ITextRangeProvider** result) override {
        if (result == nullptr) return E_INVALIDARG;
        NativeAccessibilityTextInfo info;
        if (!Info(info)) { *result = nullptr; return UIA_E_ELEMENTNOTAVAILABLE; }
        *result = Range(info, info.selection_end, info.selection_end); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_DocumentRange(ITextRangeProvider** result) override {
        if (result == nullptr) return E_INVALIDARG;
        NativeAccessibilityTextInfo info;
        if (!Info(info)) { *result = nullptr; return UIA_E_ELEMENTNOTAVAILABLE; }
        *result = Range(info, 0U, info.character_count); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(SupportedTextSelection* result) override {
        if (result == nullptr) return E_INVALIDARG; *result = SupportedTextSelection_Single; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RangeFromAnnotation(IRawElementProviderSimple*, ITextRangeProvider** result) override {
        if (result == nullptr) return E_INVALIDARG; *result = nullptr; return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE GetCaretRange(BOOL* active, ITextRangeProvider** result) override {
        if (active == nullptr || result == nullptr) return E_INVALIDARG;
        NativeAccessibilityTextInfo info;
        if (!Info(info)) { *result = nullptr; return UIA_E_ELEMENTNOTAVAILABLE; }
        *active = TRUE; *result = Range(info, info.selection_end, info.selection_end); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {
        if (value == nullptr) return E_INVALIDARG;
        NativeAccessibilityTextInfo info;
        if (!Info(info)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (info.read_only) return UIA_E_ELEMENTNOTENABLED;
        const std::string replacement = WideToUtf8(value);
        if (*value != L'\0' && replacement.empty()) return E_INVALIDARG;
        std::uint64_t revision = 0U;
        return StatusResult(provider_->ReplaceRange(handle_, info.revision, 0U,
            info.character_count, replacement, revision));
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        NativeAccessibilityTextInfo info;
        if (!Info(info)) return UIA_E_ELEMENTNOTAVAILABLE;
        std::string utf8;
        const auto read = provider_->ReadRange(handle_, info.revision, 0U, info.character_count, utf8);
        if (read != NativeAccessibilityTextStatus::Ok) return StatusResult(read);
        const std::wstring wide = Utf8ToWide(utf8);
        *result = SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
        return *result != nullptr || wide.empty() ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* result) override {
        if (result == nullptr) return E_INVALIDARG;
        NativeAccessibilityTextInfo info;
        if (!Info(info)) return UIA_E_ELEMENTNOTAVAILABLE;
        *result = info.read_only ? TRUE : FALSE; return S_OK;
    }

private:
    ~TextPatternProvider() { owner_->Release(); }
    bool Info(NativeAccessibilityTextInfo& info) const { return provider_->GetInfo(handle_, info); }
    TextRange* Range(const NativeAccessibilityTextInfo& info,
        std::uint32_t start, std::uint32_t end) const {
        return new TextRange(window_, handle_, provider_, owner_, info.revision, start, end);
    }
    HRESULT OneRange(const NativeAccessibilityTextInfo& info, std::uint32_t start,
        std::uint32_t end, SAFEARRAY** result) const {
        if (result == nullptr) return E_INVALIDARG;
        *result = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
        if (*result == nullptr) return E_OUTOFMEMORY;
        ITextRangeProvider* range = Range(info, start, end);
        LONG index = 0;
        const HRESULT status = SafeArrayPutElement(*result, &index, range);
        range->Release();
        if (FAILED(status)) { SafeArrayDestroy(*result); *result = nullptr; }
        return status;
    }
    std::atomic<ULONG> references_{1U};
    HWND window_;
    std::uint64_t handle_;
    std::shared_ptr<NativeAccessibilityTextProvider> provider_;
    IRawElementProviderSimple* owner_;
};

} // namespace

EVENTID WindowsAccessibilityTextEventId(NativeAccessibilityTextEvent event) {
    return event == NativeAccessibilityTextEvent::DocumentChanged ?
        UIA_Text_TextChangedEventId : UIA_Text_TextSelectionChangedEventId;
}

HRESULT CreateWindowsAccessibilityTextPattern(HWND window, std::uint64_t handle,
    std::shared_ptr<NativeAccessibilityTextProvider> provider,
    IRawElementProviderSimple* owner, PATTERNID pattern, IUnknown** result) {
    if (result == nullptr) return E_INVALIDARG;
    *result = nullptr;
    if (provider == nullptr || owner == nullptr) return S_OK;
    NativeAccessibilityTextInfo info;
    if (!provider->GetInfo(handle, info)) return S_OK;
    auto* text = new TextPatternProvider(window, handle, std::move(provider), owner);
    HRESULT status = S_OK;
    if (pattern == UIA_TextPatternId)
        status = text->QueryInterface(__uuidof(ITextProvider), reinterpret_cast<void**>(result));
    else if (pattern == UIA_TextPattern2Id)
        status = text->QueryInterface(__uuidof(ITextProvider2), reinterpret_cast<void**>(result));
    else if (pattern == UIA_ValuePatternId)
        status = text->QueryInterface(__uuidof(IValueProvider), reinterpret_cast<void**>(result));
    text->Release();
    return status;
}

} // namespace effindom::v2::native
