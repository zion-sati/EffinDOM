# EffinDom v2 Core Tier 1 Architecture

## Positioning: why this is not a game engine or a mobile-port runtime

EffinDom's v2 architecture is a web-native display-server split, not a game-engine UI layer and not a monolithic mobile runtime in a tab:

- **Not Three.js/Pixi-style UI:** v2 is built for application semantics, global text shaping/layout, and semantic extraction, rather than absolute-coordinate scene scripting.
- **Not Flutter-style monoliths:** Tier 1/2 runtime artifacts are decoupled from Tier 3 app payloads so route/app code can stay small and independently deployable.
- **Browser as HAL, not adversary:** the host bridge integrates browser-native platform behavior where appropriate, while keeping retained rendering/input orchestration in the wasm runtime.
- **Kernel/framework separation:** GPU-facing retained rendering infrastructure (Tier 1) is separate from UI-framework/app policy (Tier 2/3), preserving language/runtime flexibility at the app layer.

### The Nine-Patch Support

If you have only ever done web development, you probably haven't heard the term "Nine-Patch." But you *have* used it: **It is the exact equivalent of CSS `border-image`.**

In native development (Android, iOS, Unity, Unreal Engine), Nine-Patch (or 9-slicing) is the absolute gold standard for UI.
If a designer gives you a PNG of a chat bubble with a little tail pointing to the user, or a button with a complex baked-in drop shadow, you cannot just stretch that PNG to fit the text. The tail will stretch, and the shadow will distort.

Nine-slicing defines 4 insets (Left, Top, Right, Bottom). The 4 corners stay perfectly fixed, the 4 edges stretch in one direction, and the center stretches in both directions.

Because we don't have CSS `border-image`, adding `CMD_SET_IMAGE_NINE` to Tier 1 gives developers the ability to build image-backed UI components with zero layout distortion.

---

## Tier 1 ABI specification

Here is the complete, updated C ABI and command-buffer specification for the v2 Tier 1 core. It includes Nine-Patch images, split visual and hit rectangles, and the decoupled paint-order and scene commits.

### Part 1: Command buffer opcodes

```c
#pragma once
#include <stdint.h>

// ============================================================================
// COMMAND BUFFER OPCODES (Tier 2 -> Tier 1)
// ============================================================================
enum EdCommand {
    // --- LIFECYCLE ---
    CMD_CREATE_NODE = 1, // [CMD, HandleL, HandleU]
    CMD_DELETE_NODE = 2, // [CMD, HandleL, HandleU]

    // --- GEOMETRY & HIT-TESTING ---
    // [CMD_SET_BOUNDS, HandleL, HandleU, VisX, VisY, VisW, VisH, HitX, HitY, HitW, HitH, ClipX, ClipY, ClipW, ClipH, BoundsFlags]
    // Tier 2 calculates the intersected HitRect plus the descendant clip rect.
    // BoundsFlags bit 0 = interactive; bits 1.. = clip mode (raster-safe visual vs strict content).
    CMD_SET_BOUNDS = 10,

    // --- VISUALS ---
    // [CMD_SET_BOX_STYLE, HandleL, HandleU, BgColor, RadTL, RadTR, RadBR, RadBL, BorderW, BorderColor, BorderStyle, DashOn, DashOff]
    CMD_SET_BOX_STYLE = 20,

    // [CMD_SET_LAYER_EFFECT, HandleL, HandleU, Opacity, BlurSigma, BlendModeEnum]
    CMD_SET_LAYER_EFFECT = 21,

    // [CMD_SET_BACKGROUND_BLUR, HandleL, HandleU, BlurSigma]
    CMD_SET_BACKGROUND_BLUR = 23,

    // [CMD_SET_DROP_SHADOW, HandleL, HandleU, Color, OffsetX, OffsetY, BlurSigma, Spread]
    CMD_SET_DROP_SHADOW = 24,

    // [CMD_SET_LINEAR_GRADIENT, HandleL, HandleU, SX, SY, EX, EY, StopCount, (Offset, Color)...]
    CMD_SET_LINEAR_GRADIENT = 22,

    // --- MEDIA & PATHS ---
    // [CMD_SET_IMAGE, HandleL, HandleU, TextureID, ObjectFitEnum]
    CMD_SET_IMAGE = 30,

    // [CMD_SET_IMAGE_NINE, HandleL, HandleU, TextureID, InsetL, InsetT, InsetR, InsetB]
    CMD_SET_IMAGE_NINE = 31,

    // [CMD_SET_PATH, HandleL, HandleU, FillColor, StrokeColor, StrokeW, VerbCount, (VerbEnum, Args...)...]
    CMD_SET_PATH = 32,

    // [CMD_SET_SVG, HandleL, HandleU, SvgID, TintColor]
    CMD_SET_SVG = 33,

    // --- TYPOGRAPHY & TRANSIENT UI ---
    // [CMD_SET_GLYPH_RUN, HandleL, HandleU, FontID, FontSize, Color, GlyphCount, (GlyphID, X, Y, GlyphFontID)...]
    CMD_SET_GLYPH_RUN = 40,

    // [CMD_SET_TEXT_FADE, HandleL, HandleU, FadeEdgeEnum]
    CMD_SET_TEXT_FADE = 41,

    // [CMD_SET_CARET, HandleL, HandleU, X, Y, H, Color, LastInteractionMS]
    CMD_SET_CARET = 42,

    // [CMD_SET_HIGHLIGHTS, HandleL, HandleU, Color, RectCount, (X, Y, W, H)...]
    CMD_SET_HIGHLIGHTS = 43,

    // [CMD_SET_GLYPH_RUN_COLORED, HandleL, HandleU, FontID, FontSize, GlyphCount, (GlyphID, X, Y, GlyphFontID, Color)...]
    CMD_SET_GLYPH_RUN_COLORED = 44,

    // [CMD_SET_HIGHLIGHTS_COLORED, HandleL, HandleU, RectCount, (X, Y, W, H, Color)...]
    // Used by retained find sessions when Ui needs to combine the active match with secondary "highlight all" ranges.
    CMD_SET_HIGHLIGHTS_COLORED = 45,

    // ============================================================================
    // THE DECOUPLED COMMITS
    // ============================================================================

    // [CMD_COMMIT_PAINT_ORDER, NodeCount, (HandleL, HandleU)...]
    // Used exclusively for hit-testing. A flat array of interactive handles in Z-order.
    CMD_COMMIT_PAINT_ORDER = 98,

    // [CMD_COMMIT_SCENE, InstructionCount, (Opcode, HandleL, HandleU)...]
    // Used exclusively for rendering. Drives the Skia state machine.
    CMD_COMMIT_SCENE = 99
};

// Used exclusively inside CMD_COMMIT_SCENE payload. Strict 3-word stride.
enum SceneOpcode {
    OP_DRAW_NODE = 1,
    OP_PUSH_CLIP = 2,  // canvas->save(); canvas->clipRRect(node.vis_rrect, true);
    OP_PUSH_LAYER = 3, // canvas->saveLayer(node.bounds, &paint_with_opacity_and_blendmode);
    OP_POP = 4         // canvas->restore(); handle words are 0 and ignored
};
```

### Part 2: Tier 1 ABI

```c
#ifdef __cplusplus
extern "C" {
#endif

// Boot & Hardware
void ed_init(uint32_t physical_w, uint32_t physical_h, float dpr);
void ed_resize(uint32_t physical_w, uint32_t physical_h, float dpr);

// Asset Registry
void ed_register_font(uint32_t font_id, const uint8_t* bytes, uint32_t len);
void ed_register_svg(uint32_t svg_id, const uint8_t* xml_bytes, uint32_t len);
void ed_register_texture_rgba(uint32_t texture_id, const uint8_t* rgba_bytes, uint32_t w, uint32_t h, uint32_t len);
void ed_unregister_texture(uint32_t texture_id);

// Command Pipeline
void ed_execute_command_buffer(const uint32_t* buffer, uint32_t length);

// Render Loop
void ed_render_frame(double current_time_ms);

// Hit Testing (O(N) scan over the paint-order array)
uint64_t ed_hit_test(float logical_x, float logical_y);

#ifdef __cplusplus
}
#endif
```

### Part 3: Tier 2 ABI

```c
#ifdef __cplusplus
extern "C" {
#endif

// --- SURFACE A: JS Bridge Events ---
void ui_on_pointer_event(uint32_t type, uint64_t handle, float x, float y);
void ui_on_key_event(uint32_t type, uint32_t key_code, uint32_t modifiers);
void ui_on_ime_update(const uint8_t* utf8_str, uint32_t len, uint32_t caret_idx);

// --- SURFACE B: JS Bridge Extraction ---
const uint32_t* ui_get_command_buffer(uint32_t* out_length);
const uint32_t* ui_get_semantic_buffer(uint32_t* out_length);

// --- SURFACE C: Tier 3 SDK (AssemblyScript/Rust) ---
uint32_t ui_arena_alloc(uint32_t size); // Resets to 0 on frame commit

uint64_t ui_create_node(uint32_t type);
void ui_delete_node(uint64_t handle);

void ui_set_flex_direction(uint64_t handle, uint32_t dir);
void ui_set_padding(uint64_t handle, float l, float t, float r, float b);
void ui_set_margin(uint64_t handle, float l, float t, float r, float b);
void ui_set_bg_color(uint64_t handle, uint32_t color);
void ui_set_layer_effect(uint64_t handle, float opacity, float blur_sigma, uint32_t blend_mode_enum);
void ui_set_background_blur(uint64_t handle, float blur_sigma);
void ui_set_drop_shadow(uint64_t handle, uint32_t color, float offset_x, float offset_y, float blur_sigma, float spread);
void ui_set_visibility(uint64_t handle, uint32_t visibility);
uint32_t ui_push_semantic_scope(uint64_t handle);
void ui_remove_semantic_scope(uint32_t token);

void ui_set_text(uint64_t handle, const uint8_t* utf8_str, uint32_t len);
void ui_set_font(uint64_t handle, uint32_t font_id, float size);
void ui_register_font_fallback(uint32_t font_id, uint32_t fallback_font_id);
void ui_set_text_limits(uint64_t handle, int32_t max_chars, int32_t max_lines);
void ui_set_text_overflow_fade(uint64_t handle, bool horizontal, bool vertical);

// Triggers Yoga, HarfBuzz, and command-buffer generation
void ui_commit_frame();

#ifdef __cplusplus
}
#endif
```

---

## Implementation notes

### Tier 2 box model and clipping contract

Tier 2 now treats a node's explicit `width` / `height` as its **outer border box**, which is the closest retained-UI analogue to WPF-style layout sizing.

- border lives inside that outer size and participates in Yoga layout through `YGNodeStyleSetBorder(...)`,
- padding also lives inside that outer size,
- margin lives outside that outer size and only affects external layout spacing,
- the content box is `outer size - border - padding`,
- generic `clipToBounds` clips descendants to the **inner content / client box**,
- scroll views use that same **inner content viewport** as their scrollable clip / viewport rectangle, and may optionally override their reported scroll-content extents per axis through `ui_set_scroll_content_size(...)` when the retained child subtree is intentionally smaller than the logical scroll range.

This split matters because child layout, text local coordinates, scroll metrics, and descendant clipping all use the content box, while the node's own border/background still paint across the full outer border edge. In other words: the box sent to Yoga is the component's real size, not a content box that later has to "make room" for borders by shrinking inward.

Scroll offset notification now also carries an explicit re-entrancy guard inside Tier 2: nested app-driven scroll mutations on the same surface are deferred into a follow-up notification instead of recursively re-entering `ApplyScrollOffset(...)` on the callback stack.

### Tier 2 text overflow fade contract

Tier 2 text overflow fade is now separate from glyph shaping. Directional fade
state travels as retained node state and Tier 1 applies it as a post-text mask,
so changing fade axes does not require rebuilding glyph geometry.

Tier 2 also tags each emitted clip with a **clip mode**:

- **strict content clip** — used for scroll-view viewport clipping, where Tier 1 must honor the content box exactly;
- **raster-safe visual clip** — used for ordinary `clipToBounds`, where Tier 1 may relax only flush bottom/right max edges by one device pixel to avoid shaving anti-aliased rounded-border fringes.

That extra intent prevents the old "fix one, break one" behavior: scroll/content padding clips stay exact, while non-scroll rounded clips that land flush on a visual edge can avoid the missing-bottom-border artifact.

### Tier 2 retained visibility contract

Tier 2 exposes WPF-style visibility through `ui_set_visibility(...)`:

- `Normal` renders and participates in hit/focus/semantics normally.
- `Hidden` keeps layout footprint but is omitted from paint, hit-testing, focus traversal, and semantics.
- `Collapsed` is omitted from those surfaces and removed from layout participation.

This keeps visibility policy in Tier 2 while preserving the retained layout/semantic guarantees expected by Tier 3 SDK controls.

### Tier 2 clipboard copy contract

Tier 2 copy is no longer modeled as a plain string-only browser callback.

- The retained runtime always computes a `plainText` payload for copy.
- When the selected content carries RichText style runs, Tier 2 also emits a
  structured rich payload describing the selected attributed fragments.
- The browser bridge is responsible for turning that runtime payload into real
  browser clipboard formats:
  - `text/plain`
  - `text/html`
  - `web application/x-effindom-richtext+json`

This keeps selection extraction and attributed slicing inside Tier 2, while the
browser-facing MIME policy stays in the bridge.

### Semantic extraction

`ui_get_semantic_buffer(...)` normally mirrors full retained paint order, but modal surfaces can narrow that export with a tokenized semantic-scope stack. Tier 3 pushes the active dialog or context-menu subtree when it opens and removes that token when the modal closes, so the browser/a11y bridge only sees the topmost active modal surface.

### Text shaping and font fallback

Tier 2 still stores one logical `font_id` per text node, but fallback faces can now be chained onto that primary ID with `ui_register_font_fallback(...)`. During shaping, Tier 2 resolves the effective face per glyph and emits that resolved face in each `CMD_SET_GLYPH_RUN` tuple as `GlyphFontID`. Tier 1 keeps the run-level `FontID` as the node's primary face, then uses the per-glyph override when a retained glyph run mixes multiple fonts.

When the current chain still cannot cover a visible segment, Ui now reports both the coarse coverage bucket and the missing UTF-8 sample text back to the host. The shipped browser bridge uses that sample text to fetch Google-hosted subset shards on demand, Ui expands those WOFF2 bytes back to SFNT through a pinned Google `woff2` decoder before HarfBuzz registration, and the bridge then registers each shard under a fresh fallback font ID and calls `ui_font_loaded(primaryFontId)` so the retained text node reshapes against the expanded chain. Phase 4 also adds the inverse ABI needed for bounded shard caches: the host can now detach old fallback links through `ui_unregister_font_fallback(...)`, drop unused shard registrations with `ui_unregister_font(...)` / `ed_unregister_font(...)`, and then re-trigger shaping with `ui_font_loaded(...)` when eviction trims the runtime font set.

### Media registration in the shipped browser path

SVG and raster assets currently take different Tier 1 registration paths on purpose:

- `ed_register_svg(...)` still accepts raw SVG bytes. The browser bridge fetches the SVG text, copies it into WASM once, and Skia parses/stores the retained DOM/picture on the CPU side.
- The browser bridge's current raster path decodes the image in the browser, extracts RGBA pixels, and calls `ed_register_texture_rgba(...)`. That matches the renderer path that is live today.
- There is no parallel texture-view registration ABI in the shipped v2 surface anymore; the retained raster image path is the single live registration path.

### Hit testing

Because paint order is now committed separately, Tier 1 hit testing is a simple reverse scan over `CMD_COMMIT_PAINT_ORDER`. It checks only the hit rect, not the visual bounds.

```cpp
uint64_t ed_hit_test(float x, float y) {
    for (int i = paint_order_array.size() - 1; i >= 0; i--) {
        uint64_t handle = paint_order_array[i];
        const auto& node = get_node(handle);

        if (x >= node.hit_x && x <= node.hit_x + node.hit_w &&
            y >= node.hit_y && y <= node.hit_y + node.hit_h) {
            return handle;
        }
    }
    return 0;
}
```
