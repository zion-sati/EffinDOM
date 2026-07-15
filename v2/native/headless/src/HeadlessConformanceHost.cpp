#include "HeadlessConformanceHost.h"

#include "Engine.h"
#include "UiPlatformHost.h"
#include "UiRuntime.h"
#include "effindom_ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <include/core/SkCanvas.h>
#include <include/core/SkData.h>
#include <include/core/SkImage.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkSurface.h>
#include <include/encode/SkPngEncoder.h>

#ifndef EFFINDOM_SOURCE_DIR
#define EFFINDOM_SOURCE_DIR "."
#endif

namespace effindom::v2::headless {

namespace {

constexpr std::uint32_t kPhysicalWidth = 320U;
constexpr std::uint32_t kPhysicalHeight = 240U;
constexpr float kDevicePixelRatio = 1.0f;
constexpr double kFrameStepMilliseconds = 1000.0 / 60.0;

void Require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool WriteText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

float WordToFloat(std::uint32_t word) {
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(word));
    std::memcpy(&value, &word, sizeof(value));
    return value;
}

std::string EscapeJson(std::string_view value) {
    std::string escaped{};
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += character; break;
        }
    }
    return escaped;
}

class RecordingPlatformHost final : public ui::UiPlatformHost {
public:
    void WriteClipboard(std::string_view plain_text, std::string_view rich_json) override {
        calls_.push_back(
            "clipboard-write plain=\"" + EscapeJson(plain_text) + "\" rich=\"" + EscapeJson(rich_json) + "\"");
    }

    void RequestClipboardRead(std::uint64_t handle) override {
        calls_.push_back("clipboard-read handle=" + std::to_string(handle));
    }

    void RequestFontLoad(std::uint32_t font_id, std::string_view url) override {
        calls_.push_back("font-load id=" + std::to_string(font_id) + " url=\"" + EscapeJson(url) + "\"");
    }

    void ReportMissingFontCoverage(
        std::uint32_t font_id,
        std::uint32_t coverage_kind,
        std::string_view sample_text) override {
        calls_.push_back(
            "font-coverage id=" + std::to_string(font_id) +
            " kind=" + std::to_string(coverage_kind) +
            " sample=\"" + EscapeJson(sample_text) + "\"");
    }

    void RequestSemanticAnnouncement(std::uint64_t handle) override {
        calls_.push_back("semantic-announcement handle=" + std::to_string(handle));
    }

    std::size_t CallCount() const { return calls_.size(); }

    std::string Trace() const {
        std::ostringstream output{};
        for (const std::string& call : calls_) {
            output << call << '\n';
        }
        return output.str();
    }

private:
    std::vector<std::string> calls_{};
};

struct FixtureHandles {
    std::uint64_t root = UI_INVALID_HANDLE;
    std::uint64_t header = UI_INVALID_HANDLE;
    std::uint64_t scroll = UI_INVALID_HANDLE;
    std::uint64_t content = UI_INVALID_HANDLE;
    std::uint64_t editor = UI_INVALID_HANDLE;
    std::uint64_t action = UI_INVALID_HANDLE;
};

std::vector<std::uint8_t> ReadImageRgba(const std::filesystem::path& path) {
    const sk_sp<SkData> encoded = SkData::MakeFromFileName(path.string().c_str());
    if (!encoded) {
        return {};
    }
    const sk_sp<SkImage> image = SkImages::DeferredFromEncodedData(encoded);
    if (!image || image->width() != static_cast<int>(kPhysicalWidth) ||
        image->height() != static_cast<int>(kPhysicalHeight)) {
        return {};
    }
    const SkImageInfo info = SkImageInfo::MakeN32Premul(kPhysicalWidth, kPhysicalHeight);
    std::vector<std::uint8_t> pixels(info.computeMinByteSize());
    if (!image->readPixels(nullptr, info, pixels.data(), info.minRowBytes(), 0, 0)) {
        return {};
    }
    return pixels;
}

} // namespace

struct HeadlessConformanceHost::Impl {
    RecordingPlatformHost platform_host{};
    std::unique_ptr<ui::UiRuntime> runtime{};
    std::unique_ptr<Engine> engine{};
    sk_sp<SkSurface> surface{};
    FixtureHandles handles{};
    double now_ms = 0.0;
    std::uint64_t frame_count = 0U;
    bool frame_pending = false;
    bool mounted = false;
    bool pointer_route_observed = false;
    bool keyboard_route_observed = false;

    void BuildFixture() {
        runtime = std::make_unique<ui::UiRuntime>(platform_host);
        engine = std::make_unique<Engine>();
        engine->Init(kPhysicalWidth, kPhysicalHeight, kDevicePixelRatio);
        engine->SetViewportSize(static_cast<float>(kPhysicalWidth), static_cast<float>(kPhysicalHeight));
        surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(kPhysicalWidth, kPhysicalHeight));
        Require(surface != nullptr, "failed to create the headless raster surface");

        const std::vector<std::uint8_t> font = ReadBytes(
            std::filesystem::path(EFFINDOM_SOURCE_DIR) / "v2/fonts/DejaVuSans.ttf");
        Require(!font.empty(), "failed to read the headless fixture font");
        Require(runtime->RegisterFont(1U, font.data(), static_cast<std::uint32_t>(font.size())),
            "failed to register the Tier 2 fixture font");
        engine->RegisterFont(1U, font.data(), static_cast<std::uint32_t>(font.size()));

        handles.root = runtime->CreateNode(UI_NODE_FLEX_BOX);
        handles.header = runtime->CreateNode(UI_NODE_FLEX_BOX);
        handles.scroll = runtime->CreateNode(UI_NODE_SCROLLVIEW);
        handles.content = runtime->CreateNode(UI_NODE_FLEX_BOX);
        handles.editor = runtime->CreateNode(UI_NODE_TEXT);
        handles.action = runtime->CreateNode(UI_NODE_FLEX_BOX);
        Require(handles.root != UI_INVALID_HANDLE && handles.action != UI_INVALID_HANDLE,
            "failed to allocate the retained fixture");

        Require(runtime->SetWidth(handles.root, 320.0f, UI_SIZE_UNIT_PIXEL), "failed to size root width");
        Require(runtime->SetHeight(handles.root, 240.0f, UI_SIZE_UNIT_PIXEL), "failed to size root height");
        Require(runtime->SetFlexDirection(handles.root, UI_FLEX_DIRECTION_COLUMN), "failed to orient root");
        Require(runtime->SetPadding(handles.root, 10.0f, 10.0f, 10.0f, 10.0f), "failed to pad root");
        Require(runtime->SetBoxStyle(handles.root, 0xF5F7FAFFU, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0U, ED_BORDER_SOLID, 0.0f, 0.0f), "failed to style root");

        Require(runtime->SetWidth(handles.header, 300.0f, UI_SIZE_UNIT_PIXEL), "failed to size header");
        Require(runtime->SetHeight(handles.header, 34.0f, UI_SIZE_UNIT_PIXEL), "failed to size header");
        Require(runtime->SetBoxStyle(handles.header, 0x2563EBFFU, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0U, ED_BORDER_SOLID, 0.0f, 0.0f), "failed to style header");
        Require(runtime->SetSemanticRole(handles.header, UI_SEMANTIC_HEADING), "failed to set heading role");
        static constexpr std::string_view kHeading = "Native conformance";
        Require(runtime->SetSemanticLabel(handles.header,
            reinterpret_cast<const std::uint8_t*>(kHeading.data()), static_cast<std::uint32_t>(kHeading.size())),
            "failed to label heading");

        Require(runtime->SetWidth(handles.scroll, 300.0f, UI_SIZE_UNIT_PIXEL), "failed to size scroll width");
        Require(runtime->SetHeight(handles.scroll, 72.0f, UI_SIZE_UNIT_PIXEL), "failed to size scroll height");
        Require(runtime->SetScrollEnabled(handles.scroll, false, true), "failed to enable vertical scroll");
        Require(runtime->SetSmoothScrolling(handles.scroll, false), "failed to disable smooth scroll");
        Require(runtime->SetClipToBounds(handles.scroll, true), "failed to clip scroll view");
        Require(runtime->SetSemanticRole(handles.scroll, UI_SEMANTIC_LIST), "failed to set list role");
        static constexpr std::string_view kList = "Native scrolling fixture";
        Require(runtime->SetSemanticLabel(handles.scroll,
            reinterpret_cast<const std::uint8_t*>(kList.data()), static_cast<std::uint32_t>(kList.size())),
            "failed to label list");

        Require(runtime->SetWidth(handles.content, 300.0f, UI_SIZE_UNIT_PIXEL), "failed to size content width");
        Require(runtime->SetHeight(handles.content, 180.0f, UI_SIZE_UNIT_PIXEL), "failed to size content height");
        Require(runtime->SetBoxStyle(handles.content, 0xDCE7F7FFU, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0U, ED_BORDER_SOLID, 0.0f, 0.0f), "failed to style content");

        Require(runtime->SetWidth(handles.editor, 300.0f, UI_SIZE_UNIT_PIXEL), "failed to size editor width");
        Require(runtime->SetHeight(handles.editor, 48.0f, UI_SIZE_UNIT_PIXEL), "failed to size editor height");
        Require(runtime->SetFont(handles.editor, 1U, 16.0f), "failed to set editor font");
        Require(runtime->SetTextColor(handles.editor, 0x00000000U), "failed to hide platform-variable glyph pixels");
        Require(runtime->SetBoxStyle(handles.editor, 0xFFFFFFFFU, 0.0f, 0.0f, 0.0f, 0.0f,
            1.0f, 0x64748BFFU, ED_BORDER_SOLID, 0.0f, 0.0f), "failed to style editor");
        static constexpr std::string_view kInitialText = "Native";
        Require(runtime->SetText(handles.editor,
            reinterpret_cast<const std::uint8_t*>(kInitialText.data()), static_cast<std::uint32_t>(kInitialText.size())),
            "failed to set editor text");
        Require(runtime->SetEditable(handles.editor, true), "failed to make editor editable");
        Require(runtime->SetSelectable(handles.editor, true, 0x2563EB40U), "failed to make editor selectable");
        Require(runtime->SetInteractive(handles.editor, true), "failed to make editor interactive");
        Require(runtime->SetFocusable(handles.editor, true, 0), "failed to make editor focusable");
        Require(runtime->SetSemanticRole(handles.editor, UI_SEMANTIC_TEXTBOX), "failed to set textbox role");
        static constexpr std::string_view kEditorLabel = "Native editor";
        Require(runtime->SetSemanticLabel(handles.editor,
            reinterpret_cast<const std::uint8_t*>(kEditorLabel.data()),
            static_cast<std::uint32_t>(kEditorLabel.size())), "failed to label editor");

        Require(runtime->SetWidth(handles.action, 300.0f, UI_SIZE_UNIT_PIXEL), "failed to size action width");
        Require(runtime->SetHeight(handles.action, 40.0f, UI_SIZE_UNIT_PIXEL), "failed to size action height");
        Require(runtime->SetBoxStyle(handles.action, 0x16A34AFFU, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0U, ED_BORDER_SOLID, 0.0f, 0.0f), "failed to style action");
        Require(runtime->SetInteractive(handles.action, true), "failed to make action interactive");
        Require(runtime->SetFocusable(handles.action, true, 1), "failed to make action focusable");
        Require(runtime->SetSemanticRole(handles.action, UI_SEMANTIC_BUTTON), "failed to set button role");
        static constexpr std::string_view kActionLabel = "Continue";
        Require(runtime->SetSemanticLabel(handles.action,
            reinterpret_cast<const std::uint8_t*>(kActionLabel.data()),
            static_cast<std::uint32_t>(kActionLabel.size())), "failed to label action");

        Require(runtime->AddChild(handles.scroll, handles.content), "failed to attach scroll content");
        Require(runtime->AddChild(handles.root, handles.header), "failed to attach header");
        Require(runtime->AddChild(handles.root, handles.scroll), "failed to attach scroll view");
        Require(runtime->AddChild(handles.root, handles.editor), "failed to attach editor");
        Require(runtime->AddChild(handles.root, handles.action), "failed to attach action");
        Require(runtime->SetScrollContentSize(handles.scroll, 300.0f, 180.0f), "failed to set scroll extent");
        Require(runtime->SetRoot(handles.root), "failed to mount retained root");
        runtime->ResizeWindow(320.0f, 240.0f);
        mounted = true;
        frame_pending = true;
    }

    std::vector<std::uint8_t> SnapshotRgba() const {
        if (!surface) {
            return {};
        }
        const SkImageInfo info = SkImageInfo::MakeN32Premul(kPhysicalWidth, kPhysicalHeight);
        std::vector<std::uint8_t> pixels(info.computeMinByteSize());
        if (!surface->readPixels(info, pixels.data(), info.minRowBytes(), 0, 0)) {
            return {};
        }
        return pixels;
    }

    std::string SemanticSnapshot() const {
        if (!runtime) {
            return "{\n  \"records\": []\n}\n";
        }
        const std::vector<std::uint32_t>& words = runtime->semantic_buffer();
        const std::uint32_t record_count = words.empty() ? 0U : words.front();
        std::size_t index = words.empty() ? 0U : 1U;
        std::ostringstream output{};
        output << "{\n  \"records\": [\n";
        for (std::uint32_t record_index = 0U; record_index < record_count; record_index += 1U) {
            Require(index + 14U <= words.size(), "truncated semantic record");
            const std::uint32_t role = words[index];
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(words[index + 2U]) << 32U) |
                static_cast<std::uint64_t>(words[index + 1U]);
            const float x = WordToFloat(words[index + 3U]);
            const float y = WordToFloat(words[index + 4U]);
            const float width = WordToFloat(words[index + 5U]);
            const float height = WordToFloat(words[index + 6U]);
            const std::uint32_t state_flags = words[index + 7U];
            const std::uint32_t label_length = words[index + 13U];
            index += 14U;
            const std::size_t label_words = (static_cast<std::size_t>(label_length) + 3U) / 4U;
            Require(index + label_words <= words.size(), "truncated semantic label");
            std::string label(label_length, '\0');
            if (label_length > 0U) {
                std::memcpy(label.data(), words.data() + index, label_length);
            }
            index += label_words;

            output << "    {\"role\": " << role
                   << ", \"handle\": " << handle
                   << ", \"bounds\": [" << std::fixed << std::setprecision(1)
                   << x << ", " << y << ", " << width << ", " << height << "]"
                   << ", \"state\": " << state_flags
                   << ", \"label\": \"" << EscapeJson(label) << "\"}";
            if (record_index + 1U < record_count) {
                output << ',';
            }
            output << '\n';
        }
        Require(index == words.size(), "semantic buffer contains trailing words");
        output << "  ]\n}\n";
        return output.str();
    }
};

HeadlessConformanceHost::HeadlessConformanceHost()
    : impl_(std::make_unique<Impl>()) {}

HeadlessConformanceHost::~HeadlessConformanceHost() = default;

void HeadlessConformanceHost::Mount() {
    if (impl_->mounted) {
        return;
    }
    impl_->BuildFixture();
}

void HeadlessConformanceHost::RunInputScenario() {
    Require(impl_->runtime != nullptr && impl_->mounted, "headless fixture is not mounted");
    DrainFrames();

    const std::optional<ui::Rect> editor_bounds = impl_->runtime->GetVisibleBounds(impl_->handles.editor);
    Require(editor_bounds.has_value(), "editor has no visible bounds");
    const float editor_x = editor_bounds->x + editor_bounds->width - 8.0f;
    const float editor_y = editor_bounds->y + 16.0f;
    impl_->runtime->HandlePointerEvent(
        UI_EVENT_POINTER_DOWN, impl_->handles.editor, editor_x, editor_y,
        1, UI_POINTER_TYPE_MOUSE, 0, 1U, 0.5f, 1.0f, 1.0f, 1, 0U);
    impl_->runtime->HandlePointerEvent(
        UI_EVENT_POINTER_UP, impl_->handles.editor, editor_x, editor_y,
        1, UI_POINTER_TYPE_MOUSE, 0, 0U, 0.0f, 1.0f, 1.0f, 1, 0U);
    const ui::UINode* pointer_routed_editor = impl_->runtime->Resolve(impl_->handles.editor);
    impl_->pointer_route_observed =
        pointer_routed_editor != nullptr &&
        pointer_routed_editor->selection_start == 6U &&
        pointer_routed_editor->selection_end == 6U;
    Require(impl_->pointer_route_observed, "pointer route did not move the retained editor caret");

    Require(impl_->runtime->RequestFocus(impl_->handles.editor), "failed to focus editor");
    Require(impl_->runtime->SetTextSelectionRange(impl_->handles.editor, 6U, 6U), "failed to place editor caret");
    Require(impl_->runtime->PasteText(impl_->handles.editor), "failed to request clipboard input");
    static constexpr std::string_view kInsertedText = " headless";
    impl_->runtime->HandlePasteText(
        impl_->handles.editor,
        reinterpret_cast<const std::uint8_t*>(kInsertedText.data()),
        static_cast<std::uint32_t>(kInsertedText.size()));

    static constexpr std::string_view kArrowLeft = "ArrowLeft";
    impl_->keyboard_route_observed = impl_->runtime->HandleKeyEvent(
        UI_KEY_EVENT_DOWN,
        reinterpret_cast<const std::uint8_t*>(kArrowLeft.data()),
        static_cast<std::uint32_t>(kArrowLeft.size()),
        0U);
    Require(impl_->keyboard_route_observed, "editor did not consume the keyboard route");

    const std::optional<ui::Rect> scroll_bounds = impl_->runtime->GetVisibleBounds(impl_->handles.scroll);
    Require(scroll_bounds.has_value(), "scroll view has no visible bounds");
    impl_->runtime->HandlePointerEvent(
        UI_EVENT_POINTER_ENTER, impl_->handles.scroll,
        scroll_bounds->x + 10.0f, scroll_bounds->y + 10.0f,
        1, UI_POINTER_TYPE_MOUSE);
    impl_->runtime->HandleWheelEvent(0.0f, 54.0f);

    Require(impl_->runtime->RequestSemanticAnnouncement(impl_->handles.editor),
        "failed to request semantic announcement");
    Require(impl_->runtime->SetTextSelectionRange(impl_->handles.editor, 0U, 6U),
        "failed to select clipboard text");
    Require(impl_->runtime->CopyTextSelection(impl_->handles.editor), "failed to copy selected text");

    static constexpr std::string_view kTab = "Tab";
    Require(impl_->runtime->HandleKeyEvent(
        UI_KEY_EVENT_DOWN,
        reinterpret_cast<const std::uint8_t*>(kTab.data()),
        static_cast<std::uint32_t>(kTab.size()),
        0U), "tab route was not consumed");

    impl_->runtime->ResizeWindow(360.0f, 260.0f);
    RequestFrame();
    AdvanceTime(kFrameStepMilliseconds);
    DrainFrames();
    impl_->runtime->ResizeWindow(320.0f, 240.0f);
    Require(impl_->runtime->SetTextSelectionRange(impl_->handles.editor, 15U, 15U),
        "failed to collapse final selection");
    RequestFrame();
    AdvanceTime(kFrameStepMilliseconds);
    DrainFrames();
}

void HeadlessConformanceHost::RequestFrame() {
    if (impl_->mounted && impl_->runtime) {
        impl_->frame_pending = true;
    }
}

bool HeadlessConformanceHost::RunNextFrame() {
    if (!impl_->mounted || !impl_->runtime || !impl_->engine || !impl_->surface || !impl_->frame_pending) {
        return false;
    }
    impl_->frame_pending = false;
    impl_->runtime->CommitFrame(impl_->now_ms);
    const std::vector<std::uint32_t>& commands = impl_->runtime->command_buffer();
    impl_->engine->ExecuteCommandBuffer(commands.data(), static_cast<std::uint32_t>(commands.size()));
    impl_->surface->getCanvas()->clear(SK_ColorWHITE);
    impl_->engine->RenderToCanvas(impl_->surface->getCanvas(), impl_->now_ms);
    impl_->frame_count += 1U;
    if (impl_->runtime->NeedsAnimationFrame()) {
        impl_->frame_pending = true;
    }
    return true;
}

void HeadlessConformanceHost::DrainFrames() {
    constexpr std::uint32_t kMaximumFrames = 120U;
    std::uint32_t drained = 0U;
    while (impl_->frame_pending && drained < kMaximumFrames) {
        RunNextFrame();
        AdvanceTime(kFrameStepMilliseconds);
        drained += 1U;
    }
    Require(!impl_->frame_pending, "headless frame queue did not reach quiescence");
}

void HeadlessConformanceHost::AdvanceTime(double milliseconds) {
    Require(std::isfinite(milliseconds) && milliseconds >= 0.0, "invalid deterministic time increment");
    impl_->now_ms += milliseconds;
}

void HeadlessConformanceHost::Dispose() {
    impl_->frame_pending = false;
    impl_->mounted = false;
    impl_->runtime.reset();
    impl_->surface.reset();
    impl_->engine.reset();
    impl_->handles = FixtureHandles{};
}

bool HeadlessConformanceHost::IsMounted() const { return impl_->mounted; }
bool HeadlessConformanceHost::IsIdle() const { return !impl_->frame_pending; }
std::uint64_t HeadlessConformanceHost::FrameCount() const { return impl_->frame_count; }
std::size_t HeadlessConformanceHost::HostCallCount() const { return impl_->platform_host.CallCount(); }

ConformanceState HeadlessConformanceHost::State() const {
    ConformanceState state{};
    if (!impl_->runtime) {
        return state;
    }
    const ui::UINode* editor = impl_->runtime->Resolve(impl_->handles.editor);
    const ui::UINode* scroll = impl_->runtime->Resolve(impl_->handles.scroll);
    if (editor) {
        state.editor_text = editor->text_content;
        state.selection_start = editor->selection_start;
        state.selection_end = editor->selection_end;
    }
    if (scroll) {
        state.scroll_offset_y = scroll->scroll_offset_y;
    }
    state.logical_width = impl_->runtime->window_width();
    state.logical_height = impl_->runtime->window_height();
    state.pointer_route_observed = impl_->pointer_route_observed;
    state.keyboard_route_observed = impl_->keyboard_route_observed;
    return state;
}

std::vector<std::uint8_t> HeadlessConformanceHost::SnapshotRgba() const {
    return impl_->SnapshotRgba();
}

std::string HeadlessConformanceHost::SemanticSnapshot() const {
    return impl_->SemanticSnapshot();
}

std::string HeadlessConformanceHost::HostCallTrace() const {
    return impl_->platform_host.Trace();
}

bool HeadlessConformanceHost::WriteArtifacts(
    const std::filesystem::path& output_dir,
    std::string& error) const {
    if (!impl_->surface) {
        error = "no rendered surface is available";
        return false;
    }
    std::error_code filesystem_error{};
    std::filesystem::create_directories(output_dir, filesystem_error);
    if (filesystem_error) {
        error = "failed to create artifact directory: " + filesystem_error.message();
        return false;
    }

    const sk_sp<SkImage> image = impl_->surface->makeImageSnapshot();
    SkPngEncoder::Options options{};
    const sk_sp<SkData> png = image ? SkPngEncoder::Encode(nullptr, image.get(), options) : nullptr;
    if (!png) {
        error = "failed to encode PNG snapshot";
        return false;
    }
    std::ofstream png_output(output_dir / "headless-conformance.png", std::ios::binary);
    png_output.write(static_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));
    if (!png_output.good() || !WriteText(output_dir / "headless-semantics.json", SemanticSnapshot()) ||
        !WriteText(output_dir / "headless-host-trace.txt", HostCallTrace())) {
        error = "failed to write conformance artifacts";
        return false;
    }
    return true;
}

bool HeadlessConformanceHost::VerifyArtifacts(
    const std::filesystem::path& golden_dir,
    std::string& error) const {
    const std::vector<std::uint8_t> expected_pixels = ReadImageRgba(golden_dir / "headless-conformance.png");
    if (expected_pixels.empty()) {
        error = "failed to read the PNG golden";
        return false;
    }
    if (expected_pixels != SnapshotRgba()) {
        error = "rendered pixels differ from the PNG golden";
        return false;
    }
    const std::string expected_semantics = ReadText(golden_dir / "headless-semantics.json");
    if (expected_semantics.empty() || expected_semantics != SemanticSnapshot()) {
        error = "semantic snapshot differs from its golden";
        return false;
    }
    const std::string expected_trace = ReadText(golden_dir / "headless-host-trace.txt");
    if (expected_trace.empty() || expected_trace != HostCallTrace()) {
        error = "host-call trace differs from its golden";
        return false;
    }
    return true;
}

} // namespace effindom::v2::headless
