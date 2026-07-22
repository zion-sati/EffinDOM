#include "effindom_ui.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(void, js_on_focus_changed, (uint64_t handle, int is_focused), {
  if (window.__effindomCallbacks && window.__effindomCallbacks.onFocusChanged) {
    window.__effindomCallbacks.onFocusChanged(handle, Number(is_focused) !== 0);
  }
});

EM_JS(int, js_on_pointer_event, (uint64_t handle, int event_type), {
  if (window.__effindomCallbacks && window.__effindomCallbacks.onPointerEvent) {
    window.__effindomCallbacks.onPointerEvent(handle, event_type);
  }
  return 0;
});

EM_JS(void, js_on_text_changed, (uint64_t handle, const uint8_t* utf8_str, uint32_t len), {
  if (window.__effindomCallbacks && window.__effindomCallbacks.onTextChanged) {
    const textPtr = Number(utf8_str);
    window.__effindomCallbacks.onTextChanged(handle, textPtr === 0 ? "" : UTF8ToString(textPtr, len));
  }
});

EM_JS(void, js_on_text_replaced, (
    uint64_t handle,
    uint32_t start_idx,
    uint32_t end_idx,
    const uint8_t* utf8_str,
    uint32_t len), {
  if (window.__effindomCallbacks && window.__effindomCallbacks.onTextReplaced) {
    const textPtr = Number(utf8_str);
    window.__effindomCallbacks.onTextReplaced(
      handle,
      start_idx >>> 0,
      end_idx >>> 0,
      textPtr === 0 ? "" : UTF8ToString(textPtr, len),
    );
  }
});

EM_JS(void, js_on_scroll, (uint64_t handle, float offset_x, float offset_y, float content_width, float content_height, float viewport_width, float viewport_height), {
  if (window.__effindomCallbacks && window.__effindomCallbacks.onScroll) {
    window.__effindomCallbacks.onScroll(handle, offset_x, offset_y, content_width, content_height, viewport_width, viewport_height);
  }
});

EM_JS(void, js_on_selection_changed, (uint64_t handle, uint32_t start_idx, uint32_t end_idx), {
  if (window.__effindomCallbacks && window.__effindomCallbacks.onSelectionChanged) {
    window.__effindomCallbacks.onSelectionChanged(handle, start_idx, end_idx);
  }
});

EM_JS(void, js_on_cross_selection_changed, (uint64_t area_handle, const uint8_t* utf8_str, uint32_t len), {
  if (window.__effindomCallbacks && window.__effindomCallbacks.onCrossSelectionChanged) {
    const textPtr = Number(utf8_str);
    window.__effindomCallbacks.onCrossSelectionChanged(area_handle, textPtr === 0 ? "" : UTF8ToString(textPtr, len));
  }
});

EM_JS(void, js_on_clipboard_write, (
    const uint8_t* utf8_plain_text,
    uint32_t plain_text_len,
    const uint8_t* utf8_rich_json,
    uint32_t rich_json_len), {
  if (window.__effindomCallbacks && window.__effindomCallbacks.onClipboardWrite) {
    const plainTextPtr = Number(utf8_plain_text);
    const richJsonPtr = Number(utf8_rich_json);
    const plainText = plainTextPtr === 0 ? "" : UTF8ToString(plainTextPtr, plain_text_len);
    let richText = undefined;
    if (richJsonPtr !== 0 && rich_json_len > 0) {
      try {
        richText = JSON.parse(UTF8ToString(richJsonPtr, rich_json_len));
      } catch {
        richText = undefined;
      }
    }
    window.__effindomCallbacks.onClipboardWrite({
      plainText,
      ...(richText === undefined ? {} : { richText }),
    });
  }
});

EM_JS(void, js_on_request_clipboard_read, (uint64_t handle), {
  if (window.__effindomCallbacks && window.__effindomCallbacks.onClipboardRead) {
    window.__effindomCallbacks.onClipboardRead(handle);
  }
});

EM_JS(void, js_on_request_font_load, (uint32_t font_id, const uint8_t* utf8_url, uint32_t len), {
  if (window.__effindomCallbacks && window.__effindomCallbacks.onRequestFontLoad) {
    const urlPtr = Number(utf8_url);
    window.__effindomCallbacks.onRequestFontLoad(font_id, urlPtr === 0 ? "" : UTF8ToString(urlPtr, len));
  }
});

EM_JS(void, js_on_missing_font_coverage, (uint32_t font_id, uint32_t coverage_kind, const uint8_t* utf8_sample, uint32_t len), {
  if (window.__effindomCallbacks && window.__effindomCallbacks.onMissingFontCoverage) {
    const textPtr = Number(utf8_sample);
    window.__effindomCallbacks.onMissingFontCoverage(
      font_id >>> 0,
      coverage_kind >>> 0,
      textPtr === 0 ? "" : UTF8ToString(textPtr, len),
    );
  }
});

EM_JS(void, js_on_request_semantic_announcement, (uint64_t handle), {
  if (window.__effindomCallbacks && window.__effindomCallbacks.onRequestSemanticAnnouncement) {
    window.__effindomCallbacks.onRequestSemanticAnnouncement(handle);
  }
});

extern "C" {

EMSCRIPTEN_KEEPALIVE void as_on_focus_changed(ui_handle_t handle, bool is_focused) {
    js_on_focus_changed(handle, is_focused ? 1 : 0);
}

EMSCRIPTEN_KEEPALIVE bool as_on_pointer_event(ui_handle_t handle, UiEvent event_enum) {
    return js_on_pointer_event(handle, static_cast<int>(event_enum)) != 0;
}

EMSCRIPTEN_KEEPALIVE void as_on_text_changed(ui_handle_t handle, const uint8_t* utf8_str, uint32_t len) {
    js_on_text_changed(handle, utf8_str, len);
}

EMSCRIPTEN_KEEPALIVE void as_on_text_replaced(
    ui_handle_t handle,
    uint32_t start_idx,
    uint32_t end_idx,
    const uint8_t* utf8_str,
    uint32_t len) {
    js_on_text_replaced(handle, start_idx, end_idx, utf8_str, len);
}

EMSCRIPTEN_KEEPALIVE void as_on_scroll(
    ui_handle_t handle,
    float offset_x,
    float offset_y,
    float content_width,
    float content_height,
    float viewport_width,
    float viewport_height) {
    js_on_scroll(handle, offset_x, offset_y, content_width, content_height, viewport_width, viewport_height);
}

EMSCRIPTEN_KEEPALIVE void as_on_selection_changed(ui_handle_t handle, uint32_t start_idx, uint32_t end_idx) {
    js_on_selection_changed(handle, start_idx, end_idx);
}

EMSCRIPTEN_KEEPALIVE void as_on_cross_selection_changed(ui_handle_t area_handle, const uint8_t* utf8_str, uint32_t len) {
    js_on_cross_selection_changed(area_handle, utf8_str, len);
}

EMSCRIPTEN_KEEPALIVE void as_on_clipboard_write(
    const uint8_t* utf8_plain_text,
    uint32_t plain_text_len,
    const uint8_t* utf8_rich_json,
    uint32_t rich_json_len) {
    js_on_clipboard_write(utf8_plain_text, plain_text_len, utf8_rich_json, rich_json_len);
}

EMSCRIPTEN_KEEPALIVE void as_on_request_clipboard_read(ui_handle_t handle) {
    js_on_request_clipboard_read(handle);
}

EMSCRIPTEN_KEEPALIVE void as_on_request_font_load(uint32_t font_id, const uint8_t* utf8_url, uint32_t len) {
    js_on_request_font_load(font_id, utf8_url, len);
}

EMSCRIPTEN_KEEPALIVE void as_on_missing_font_coverage(uint32_t font_id, uint32_t coverage_kind, const uint8_t* utf8_sample, uint32_t len) {
    js_on_missing_font_coverage(font_id, coverage_kind, utf8_sample, len);
}

EMSCRIPTEN_KEEPALIVE void as_on_request_semantic_announcement(ui_handle_t handle) {
    js_on_request_semantic_announcement(handle);
}

} // extern "C"

#else

#if defined(_MSC_VER)
#define EFFINDOM_WEAK_CALLBACK
static UiHostCallbacks g_host_callbacks{};
#else
#define EFFINDOM_WEAK_CALLBACK __attribute__((weak))
#endif

extern "C" {

void ui_set_host_callbacks(const UiHostCallbacks* callbacks) {
#if defined(_MSC_VER)
    g_host_callbacks = callbacks == nullptr ? UiHostCallbacks{} : *callbacks;
#else
    (void)callbacks;
#endif
}

EFFINDOM_WEAK_CALLBACK void as_on_focus_changed(ui_handle_t handle, bool is_focused) {
#if defined(_MSC_VER)
    if (g_host_callbacks.on_focus_changed != nullptr) {
        g_host_callbacks.on_focus_changed(handle, is_focused);
        return;
    }
#endif
    (void)handle;
    (void)is_focused;
}

EFFINDOM_WEAK_CALLBACK bool as_on_pointer_event(ui_handle_t handle, UiEvent event_enum) {
#if defined(_MSC_VER)
    if (g_host_callbacks.on_pointer_event != nullptr) {
        return g_host_callbacks.on_pointer_event(handle, event_enum);
    }
#endif
    (void)handle;
    (void)event_enum;
    return false;
}

EFFINDOM_WEAK_CALLBACK void as_on_text_changed(ui_handle_t handle, const uint8_t* utf8_str, uint32_t len) {
#if defined(_MSC_VER)
    if (g_host_callbacks.on_text_changed != nullptr) {
        g_host_callbacks.on_text_changed(handle, utf8_str, len);
        return;
    }
#endif
    (void)handle;
    (void)utf8_str;
    (void)len;
}

EFFINDOM_WEAK_CALLBACK void as_on_text_replaced(
    ui_handle_t handle,
    uint32_t start_idx,
    uint32_t end_idx,
    const uint8_t* utf8_str,
    uint32_t len) {
#if defined(_MSC_VER)
    if (g_host_callbacks.on_text_replaced != nullptr) {
        g_host_callbacks.on_text_replaced(handle, start_idx, end_idx, utf8_str, len);
        return;
    }
#endif
    (void)handle;
    (void)start_idx;
    (void)end_idx;
    (void)utf8_str;
    (void)len;
}

EFFINDOM_WEAK_CALLBACK void as_on_scroll(
    ui_handle_t handle,
    float offset_x,
    float offset_y,
    float content_width,
    float content_height,
    float viewport_width,
    float viewport_height) {
#if defined(_MSC_VER)
    if (g_host_callbacks.on_scroll != nullptr) {
        g_host_callbacks.on_scroll(handle, offset_x, offset_y, content_width, content_height, viewport_width, viewport_height);
        return;
    }
#endif
    (void)handle;
    (void)offset_x;
    (void)offset_y;
    (void)content_width;
    (void)content_height;
    (void)viewport_width;
    (void)viewport_height;
}

EFFINDOM_WEAK_CALLBACK void as_on_selection_changed(ui_handle_t handle, uint32_t start_idx, uint32_t end_idx) {
#if defined(_MSC_VER)
    if (g_host_callbacks.on_selection_changed != nullptr) {
        g_host_callbacks.on_selection_changed(handle, start_idx, end_idx);
        return;
    }
#endif
    (void)handle;
    (void)start_idx;
    (void)end_idx;
}

EFFINDOM_WEAK_CALLBACK void as_on_cross_selection_changed(ui_handle_t area_handle, const uint8_t* utf8_str, uint32_t len) {
#if defined(_MSC_VER)
    if (g_host_callbacks.on_cross_selection_changed != nullptr) {
        g_host_callbacks.on_cross_selection_changed(area_handle, utf8_str, len);
        return;
    }
#endif
    (void)area_handle;
    (void)utf8_str;
    (void)len;
}

EFFINDOM_WEAK_CALLBACK void as_on_clipboard_write(
    const uint8_t* utf8_plain_text,
    uint32_t plain_text_len,
    const uint8_t* utf8_rich_json,
    uint32_t rich_json_len) {
#if defined(_MSC_VER)
    if (g_host_callbacks.on_clipboard_write != nullptr) {
        g_host_callbacks.on_clipboard_write(utf8_plain_text, plain_text_len, utf8_rich_json, rich_json_len);
        return;
    }
#endif
    (void)utf8_plain_text;
    (void)plain_text_len;
    (void)utf8_rich_json;
    (void)rich_json_len;
}

EFFINDOM_WEAK_CALLBACK void as_on_request_clipboard_read(ui_handle_t handle) {
#if defined(_MSC_VER)
    if (g_host_callbacks.on_request_clipboard_read != nullptr) {
        g_host_callbacks.on_request_clipboard_read(handle);
        return;
    }
#endif
    (void)handle;
}

EFFINDOM_WEAK_CALLBACK void as_on_request_font_load(uint32_t font_id, const uint8_t* utf8_url, uint32_t len) {
#if defined(_MSC_VER)
    if (g_host_callbacks.on_request_font_load != nullptr) {
        g_host_callbacks.on_request_font_load(font_id, utf8_url, len);
        return;
    }
#endif
    (void)font_id;
    (void)utf8_url;
    (void)len;
}

EFFINDOM_WEAK_CALLBACK void as_on_missing_font_coverage(uint32_t font_id, uint32_t coverage_kind, const uint8_t* utf8_sample, uint32_t len) {
#if defined(_MSC_VER)
    if (g_host_callbacks.on_missing_font_coverage != nullptr) {
        g_host_callbacks.on_missing_font_coverage(font_id, coverage_kind, utf8_sample, len);
        return;
    }
#endif
    (void)font_id;
    (void)coverage_kind;
    (void)utf8_sample;
    (void)len;
}

EFFINDOM_WEAK_CALLBACK void as_on_request_semantic_announcement(ui_handle_t handle) {
#if defined(_MSC_VER)
    if (g_host_callbacks.on_request_semantic_announcement != nullptr) {
        g_host_callbacks.on_request_semantic_announcement(handle);
        return;
    }
#endif
    (void)handle;
}

} // extern "C"
#undef EFFINDOM_WEAK_CALLBACK
#endif
