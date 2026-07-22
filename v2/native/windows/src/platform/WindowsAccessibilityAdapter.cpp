#include "WindowsAccessibilityAdapter.h"

#include "SDL3/SDL.h"

#include <windows.h>
#include <ole2.h>
#include <CommCtrl.h>
#include <UIAutomation.h>

#include <atomic>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace effindom::v2::native {
namespace {

constexpr UINT_PTR kAccessibilitySubclassId = 0x45444641U;
constexpr std::size_t kRootIndex = std::numeric_limits<std::size_t>::max();

struct AccessibilityState {
    std::mutex mutex;
    NativeAccessibilitySnapshot snapshot;
    NativeAccessibilityActionHandler action_handler;
    HWND window = nullptr;
    bool active = true;
};

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), length) != length) return {};
    return result;
}

CONTROLTYPEID ControlType(NativeAccessibilityRole role) {
    switch (role) {
        case NativeAccessibilityRole::Button: return UIA_ButtonControlTypeId;
        case NativeAccessibilityRole::TextBox: return UIA_EditControlTypeId;
        case NativeAccessibilityRole::Link: return UIA_HyperlinkControlTypeId;
        case NativeAccessibilityRole::Heading: return UIA_TextControlTypeId;
        case NativeAccessibilityRole::Form: return UIA_GroupControlTypeId;
        case NativeAccessibilityRole::List: return UIA_ListControlTypeId;
        case NativeAccessibilityRole::ListItem: return UIA_ListItemControlTypeId;
        case NativeAccessibilityRole::Image: return UIA_ImageControlTypeId;
        case NativeAccessibilityRole::Dialog: return UIA_WindowControlTypeId;
        case NativeAccessibilityRole::StaticText: return UIA_TextControlTypeId;
        case NativeAccessibilityRole::CheckBox: return UIA_CheckBoxControlTypeId;
        case NativeAccessibilityRole::Radio: return UIA_RadioButtonControlTypeId;
        case NativeAccessibilityRole::RadioGroup: return UIA_GroupControlTypeId;
        case NativeAccessibilityRole::Switch: return UIA_CheckBoxControlTypeId;
        case NativeAccessibilityRole::Slider: return UIA_SliderControlTypeId;
        case NativeAccessibilityRole::ComboBox: return UIA_ComboBoxControlTypeId;
    }
    return UIA_CustomControlTypeId;
}

bool IsInvokable(NativeAccessibilityRole role) {
    return role == NativeAccessibilityRole::Button || role == NativeAccessibilityRole::Link ||
        role == NativeAccessibilityRole::CheckBox || role == NativeAccessibilityRole::Radio ||
        role == NativeAccessibilityRole::Switch || role == NativeAccessibilityRole::ComboBox;
}

bool IsToggle(NativeAccessibilityRole role) {
    return role == NativeAccessibilityRole::CheckBox || role == NativeAccessibilityRole::Switch;
}

bool IsFocusable(NativeAccessibilityRole role) {
    return IsInvokable(role) || role == NativeAccessibilityRole::TextBox ||
        role == NativeAccessibilityRole::Slider;
}

class AccessibilityProvider final : public IRawElementProviderSimple,
                                    public IRawElementProviderFragment,
                                    public IRawElementProviderFragmentRoot,
                                    public IInvokeProvider,
                                    public IToggleProvider {
public:
    AccessibilityProvider(std::shared_ptr<AccessibilityState> state, std::size_t index)
        : state_(std::move(state)), index_(index) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IRawElementProviderSimple)) {
            *result = static_cast<IRawElementProviderSimple*>(this);
        } else if (id == __uuidof(IRawElementProviderFragment)) {
            *result = static_cast<IRawElementProviderFragment*>(this);
        } else if (id == __uuidof(IRawElementProviderFragmentRoot) && IsRoot()) {
            *result = static_cast<IRawElementProviderFragmentRoot*>(this);
        } else if (id == __uuidof(IInvokeProvider) && NodeSupportsInvoke()) {
            *result = static_cast<IInvokeProvider*>(this);
        } else if (id == __uuidof(IToggleProvider) && NodeSupportsToggle()) {
            *result = static_cast<IToggleProvider*>(this);
        }
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

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        if (pattern == UIA_InvokePatternId && NodeSupportsInvoke()) {
            return QueryInterface(__uuidof(IInvokeProvider), reinterpret_cast<void**>(result));
        }
        if (pattern == UIA_TogglePatternId && NodeSupportsToggle()) {
            return QueryInterface(__uuidof(IToggleProvider), reinterpret_cast<void**>(result));
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* result) override {
        if (result == nullptr) return E_INVALIDARG;
        VariantInit(result);
        if (IsRoot()) {
            if (property == UIA_ControlTypePropertyId) SetInt(result, UIA_PaneControlTypeId);
            else if (property == UIA_NamePropertyId) SetString(result, L"EffinDOM application");
            else if (property == UIA_IsControlElementPropertyId || property == UIA_IsContentElementPropertyId ||
                property == UIA_IsEnabledPropertyId) SetBool(result, true);
            return S_OK;
        }
        const auto node = Node();
        if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
        if (property == UIA_ControlTypePropertyId) SetInt(result, ControlType(node->role));
        else if (property == UIA_NamePropertyId) SetString(result, Utf8ToWide(node->label));
        else if (property == UIA_AutomationIdPropertyId) SetString(result, L"effindom-" + std::to_wstring(node->handle));
        else if (property == UIA_IsControlElementPropertyId || property == UIA_IsContentElementPropertyId) SetBool(result, true);
        else if (property == UIA_IsEnabledPropertyId) SetBool(result, !node->disabled);
        else if (property == UIA_IsKeyboardFocusablePropertyId) SetBool(result, IsFocusable(node->role));
        else if (property == UIA_HasKeyboardFocusPropertyId) SetBool(result, FocusedHandle() == node->handle);
        else if (property == UIA_IsOffscreenPropertyId) SetBool(result, node->bounds.width <= 0.0f || node->bounds.height <= 0.0f);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        return IsRoot() ? UiaHostProviderFromHwnd(state_->window, result) : S_OK;
    }

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
        IRawElementProviderFragment** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        const std::size_t count = NodeCount();
        if (IsRoot()) {
            if (count == 0U) return S_OK;
            if (direction == NavigateDirection_FirstChild) return MakeFragment(0U, result);
            if (direction == NavigateDirection_LastChild) return MakeFragment(count - 1U, result);
            return S_OK;
        }
        if (direction == NavigateDirection_Parent) return MakeFragment(kRootIndex, result);
        if (direction == NavigateDirection_NextSibling && index_ + 1U < count) return MakeFragment(index_ + 1U, result);
        if (direction == NavigateDirection_PreviousSibling && index_ > 0U) return MakeFragment(index_ - 1U, result);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        if (IsRoot()) return S_OK;
        const auto node = Node();
        if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
        SAFEARRAY* values = SafeArrayCreateVector(VT_I4, 0, 3);
        if (values == nullptr) return E_OUTOFMEMORY;
        LONG* data = nullptr;
        SafeArrayAccessData(values, reinterpret_cast<void**>(&data));
        data[0] = UiaAppendRuntimeId;
        data[1] = static_cast<LONG>(node->handle & 0xFFFFFFFFULL);
        data[2] = static_cast<LONG>(node->handle >> 32U);
        SafeArrayUnaccessData(values);
        *result = values;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* result) override {
        if (result == nullptr) return E_INVALIDARG;
        if (IsRoot()) {
            RECT rect{};
            if (!GetWindowRect(state_->window, &rect)) return UIA_E_ELEMENTNOTAVAILABLE;
            *result = {static_cast<double>(rect.left), static_cast<double>(rect.top),
                static_cast<double>(rect.right - rect.left), static_cast<double>(rect.bottom - rect.top)};
            return S_OK;
        }
        const auto node = Node();
        if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
        POINT origin{0, 0};
        ClientToScreen(state_->window, &origin);
        const double scale = static_cast<double>(GetDpiForWindow(state_->window)) / 96.0;
        *result = {origin.x + node->bounds.x * scale, origin.y + node->bounds.y * scale,
            node->bounds.width * scale, node->bounds.height * scale};
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override {
        return Perform(NativeAccessibilityAction::Focus);
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = new AccessibilityProvider(state_, kRootIndex);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
        IRawElementProviderFragment** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        POINT origin{0, 0};
        ClientToScreen(state_->window, &origin);
        const double scale = static_cast<double>(GetDpiForWindow(state_->window)) / 96.0;
        const float local_x = static_cast<float>((x - origin.x) / scale);
        const float local_y = static_cast<float>((y - origin.y) / scale);
        std::lock_guard lock(state_->mutex);
        for (std::size_t index = state_->snapshot.nodes.size(); index > 0U; --index) {
            const auto& bounds = state_->snapshot.nodes[index - 1U].bounds;
            if (local_x >= bounds.x && local_x <= bounds.x + bounds.width &&
                local_y >= bounds.y && local_y <= bounds.y + bounds.height) {
                *result = new AccessibilityProvider(state_, index - 1U);
                return S_OK;
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** result) override {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        std::lock_guard lock(state_->mutex);
        for (std::size_t index = 0U; index < state_->snapshot.nodes.size(); ++index) {
            if (state_->snapshot.nodes[index].handle == state_->snapshot.focused_handle) {
                *result = new AccessibilityProvider(state_, index);
                break;
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Invoke() override { return Perform(NativeAccessibilityAction::Press); }
    HRESULT STDMETHODCALLTYPE Toggle() override { return Perform(NativeAccessibilityAction::Press); }

    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* result) override {
        if (result == nullptr) return E_INVALIDARG;
        const auto node = Node();
        if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
        *result = node->checked == NativeAccessibilityCheckedState::True ? ToggleState_On :
            node->checked == NativeAccessibilityCheckedState::Mixed ? ToggleState_Indeterminate : ToggleState_Off;
        return S_OK;
    }

private:
    ~AccessibilityProvider() = default;
    bool IsRoot() const { return index_ == kRootIndex; }

    std::optional<NativeAccessibilityNode> Node() const {
        std::lock_guard lock(state_->mutex);
        if (!state_->active || index_ >= state_->snapshot.nodes.size()) return std::nullopt;
        return state_->snapshot.nodes[index_];
    }

    std::size_t NodeCount() const {
        std::lock_guard lock(state_->mutex);
        return state_->active ? state_->snapshot.nodes.size() : 0U;
    }

    std::uint64_t FocusedHandle() const {
        std::lock_guard lock(state_->mutex);
        return state_->snapshot.focused_handle;
    }

    bool NodeSupportsInvoke() const {
        const auto node = Node();
        return node && IsInvokable(node->role);
    }

    bool NodeSupportsToggle() const {
        const auto node = Node();
        return node && IsToggle(node->role);
    }

    HRESULT Perform(NativeAccessibilityAction action) {
        NativeAccessibilityActionHandler handler;
        std::uint64_t handle = 0U;
        {
            std::lock_guard lock(state_->mutex);
            if (!state_->active || index_ >= state_->snapshot.nodes.size()) return UIA_E_ELEMENTNOTAVAILABLE;
            const auto& node = state_->snapshot.nodes[index_];
            if (node.disabled) return UIA_E_ELEMENTNOTENABLED;
            handle = node.handle;
            handler = state_->action_handler;
        }
        if (!handler) return UIA_E_NOTSUPPORTED;
        handler(action, handle);
        return S_OK;
    }

    HRESULT MakeFragment(std::size_t index, IRawElementProviderFragment** result) {
        *result = new AccessibilityProvider(state_, index);
        return S_OK;
    }

    static void SetBool(VARIANT* value, bool enabled) {
        value->vt = VT_BOOL;
        value->boolVal = enabled ? VARIANT_TRUE : VARIANT_FALSE;
    }

    static void SetInt(VARIANT* value, int number) {
        value->vt = VT_I4;
        value->lVal = number;
    }

    static void SetString(VARIANT* value, const std::wstring& text) {
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
    }

    std::atomic<ULONG> references_{1U};
    std::shared_ptr<AccessibilityState> state_;
    std::size_t index_;
};

class WindowsAccessibilityAdapter final : public NativeAccessibilityAdapter {
public:
    WindowsAccessibilityAdapter(HWND window, NativeAccessibilityActionHandler action_handler)
        : state_(std::make_shared<AccessibilityState>()) {
        state_->window = window;
        state_->action_handler = std::move(action_handler);
        root_ = new AccessibilityProvider(state_, kRootIndex);
        SetWindowSubclass(window, &WindowsAccessibilityAdapter::WindowProc,
            kAccessibilitySubclassId, reinterpret_cast<DWORD_PTR>(this));
    }

    ~WindowsAccessibilityAdapter() override {
        RemoveWindowSubclass(state_->window, &WindowsAccessibilityAdapter::WindowProc,
            kAccessibilitySubclassId);
        Clear();
        {
            std::lock_guard lock(state_->mutex);
            state_->active = false;
            state_->action_handler = {};
        }
        root_->Release();
    }

    void Update(const NativeAccessibilitySnapshot& snapshot) override {
        std::uint64_t old_focus = 0U;
        {
            std::lock_guard lock(state_->mutex);
            old_focus = state_->snapshot.focused_handle;
            state_->snapshot = snapshot;
        }
        UiaRaiseStructureChangedEvent(root_, StructureChangeType_ChildrenInvalidated, nullptr, 0);
        if (snapshot.focused_handle != old_focus) {
            IRawElementProviderFragment* focused = nullptr;
            if (SUCCEEDED(root_->GetFocus(&focused)) && focused != nullptr) {
                IRawElementProviderSimple* simple = nullptr;
                if (SUCCEEDED(focused->QueryInterface(__uuidof(IRawElementProviderSimple),
                        reinterpret_cast<void**>(&simple))) && simple != nullptr) {
                    UiaRaiseAutomationEvent(simple, UIA_AutomationFocusChangedEventId);
                    simple->Release();
                }
                focused->Release();
            }
        }
    }

    void Announce(const NativeAccessibilityNode& node) override {
        AccessibilityProvider* element = ProviderForHandle(node.handle);
        if (element == nullptr) return;
        const std::wstring text = Utf8ToWide(node.label);
        BSTR display = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
        UiaRaiseNotificationEvent(element, NotificationKind_Other,
            NotificationProcessing_ImportantMostRecent, display, L"effindom-semantic-announcement");
        SysFreeString(display);
        element->Release();
    }

    void Clear() override {
        {
            std::lock_guard lock(state_->mutex);
            state_->snapshot = {};
        }
        if (root_ != nullptr) {
            UiaRaiseStructureChangedEvent(root_, StructureChangeType_ChildrenInvalidated, nullptr, 0);
        }
    }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param,
        LPARAM l_param, UINT_PTR, DWORD_PTR reference) {
        auto* adapter = reinterpret_cast<WindowsAccessibilityAdapter*>(reference);
        if (message == WM_GETOBJECT && l_param == UiaRootObjectId && adapter != nullptr) {
            return UiaReturnRawElementProvider(window, w_param, l_param, adapter->root_);
        }
        return DefSubclassProc(window, message, w_param, l_param);
    }

    AccessibilityProvider* ProviderForHandle(std::uint64_t handle) {
        std::lock_guard lock(state_->mutex);
        for (std::size_t index = 0U; index < state_->snapshot.nodes.size(); ++index) {
            if (state_->snapshot.nodes[index].handle == handle) {
                return new AccessibilityProvider(state_, index);
            }
        }
        return nullptr;
    }

    std::shared_ptr<AccessibilityState> state_;
    AccessibilityProvider* root_ = nullptr;
};

} // namespace

std::unique_ptr<NativeAccessibilityAdapter> CreateWindowsAccessibilityAdapter(
    SDL_Window* window, NativeAccessibilityActionHandler action_handler) {
    if (window == nullptr) return nullptr;
    HWND native_window = static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (native_window == nullptr) return nullptr;
    return std::make_unique<WindowsAccessibilityAdapter>(native_window, std::move(action_handler));
}

} // namespace effindom::v2::native
