SVG support in Tier 1 is built on Skia’s `SkSVGDOM`, which parses XML into a retained, scalable display list. Because our ABI passes raw UTF‑8 bytes, we can stream them directly into Skia’s parser. The key is to do all the heavy work *once* at registration time, not in the render loop.

---

## 1. Registration (`ed_register_svg`)

```cpp
#include "include/svg/SkSVGDOM.h"
#include "include/core/SkStream.h"
#include "include/core/SkPictureRecorder.h"

struct SvgAsset {
    sk_sp<SkSVGDOM> dom;
    sk_sp<SkPicture> picture;  // pre‑recorded for fast replay
    float intrinsicWidth, intrinsicHeight;
};

std::unordered_map<uint32_t, SvgAsset> g_svg_registry;

void ed_register_svg(uint32_t svg_id, const uint8_t* xml_bytes, uint32_t length) {
    // 1. Wrap UTF-8 bytes in a stream (no copying, just a reference)
    SkMemoryStream stream(xml_bytes, length, /*copyData=*/false);

    // 2. Parse into an SkSVGDOM (this is the expensive part)
    sk_sp<SkSVGDOM> dom = SkSVGDOM::MakeFromStream(stream);
    if (!dom) {
        // log error, skip registration
        return;
    }

    // 3. Record the SVG into an SkPicture for instant replay
    SkPictureRecorder recorder;
    SkCanvas* canvas = recorder.beginRecording(
        SkRect::MakeWH(dom->containerSize().width(), dom->containerSize().height()));

    dom->render(canvas);
    sk_sp<SkPicture> picture = recorder.finishRecordingAsPicture();

    // 4. Store both the DOM (for potential future queries) and the picture
    g_svg_registry[svg_id] = { std::move(dom), std::move(picture),
                               dom->containerSize().width(), dom->containerSize().height() };
}
```

**Important**: `SkMemoryStream::MakeCopy` is safer if you can’t guarantee `xml_bytes` outlives the call. In WASM, the JS Bridge may free the ArrayBuffer after this function returns, so use `MakeCopy` to let Skia own a copy of the data.

---

## 2. Rendering (`CMD_SET_SVG`)

When the command buffer contains `[CMD_SET_SVG, handle, svg_id, tint_color]`, the display node’s draw routine does:

```cpp
void draw_svg_node(const DisplayNode& node, SkCanvas* canvas) {
    auto it = g_svg_registry.find(node.svg_id);
    if (it == g_svg_registry.end()) return;  // missing asset → draw nothing

    const SvgAsset& asset = it->second;

    // Scale the picture to fit the node’s visual rect
    canvas->save();
    SkRect target = SkRect::MakeXYWH(node.vis_x, node.vis_y, node.vis_w, node.vis_h);
    SkRect source = SkRect::MakeWH(asset.intrinsicWidth, asset.intrinsicHeight);
    SkMatrix m = SkMatrix::RectToRect(source, target);
    canvas->concat(m);

    // Apply tint if color is not transparent black
    if (node.svg_tint_color != 0x00000000) {
        SkPaint tintPaint;
        tintPaint.setBlendMode(SkBlendMode::kSrcIn);
        tintPaint.setColor(node.svg_tint_color);
        // SrcIn: use the source shape's alpha, but recolor it
        asset.picture->playback(canvas, &tintPaint);
    } else {
        asset.picture->playback(canvas);
    }

    canvas->restore();
}
```

This is extremely fast – the `SkPicture::playback` just replays a list of pre‑recorded Skia drawing commands. No XML parsing, no DOM queries.

---

## 3. Why this is safe in the Render Worker

- `ed_register_svg` is called from the JS Bridge (same thread as the render loop) and populates the registry before any command buffer referencing that `svg_id` is executed. The single‑threaded nature of a Web Worker eliminates races.
- The `SkPicture` is immutable and thread‑safe by design, but we never leave the single thread anyway.

---
