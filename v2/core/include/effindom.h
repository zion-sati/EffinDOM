#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t ed_handle_t;
typedef uintptr_t ed_ptr_t;

enum {
    ED_INVALID_HANDLE = 0
};

typedef enum EdCommand {
    CMD_CREATE_NODE = 1,
    CMD_DELETE_NODE = 2,

    CMD_SET_BOUNDS = 10,

    CMD_SET_BOX_STYLE = 20,
    CMD_SET_LAYER_EFFECT = 21,
    CMD_SET_LINEAR_GRADIENT = 22,
    CMD_SET_BACKGROUND_BLUR = 23,
    CMD_SET_DROP_SHADOW = 24,

    CMD_SET_IMAGE = 30,
    CMD_SET_IMAGE_NINE = 31,
    CMD_SET_PATH = 32,
    CMD_SET_SVG = 33,

    CMD_SET_GLYPH_RUN = 40,
    CMD_SET_TEXT_FADE = 41,
    CMD_SET_CARET = 42,
    CMD_SET_HIGHLIGHTS = 43,
    CMD_SET_GLYPH_RUN_COLORED = 44,
    CMD_SET_HIGHLIGHTS_COLORED = 45,

    CMD_COMMIT_PAINT_ORDER = 98,
    CMD_COMMIT_SCENE = 99
} EdCommand;

typedef enum SceneOpcode {
    OP_DRAW_NODE = 1,
    OP_PUSH_CLIP = 2,
    OP_PUSH_LAYER = 3,
    OP_POP = 4,
    OP_PUSH_TRANSLATE = 5
} SceneOpcode;

typedef enum EdClipMode {
    ED_CLIP_MODE_RASTER_SAFE_VISUAL = 0,
    ED_CLIP_MODE_STRICT_CONTENT = 1
} EdClipMode;

enum {
    ED_BOUNDS_FLAG_INTERACTIVE = 1 << 0,
    ED_BOUNDS_CLIP_MODE_SHIFT = 1,
    ED_BOUNDS_CLIP_MODE_MASK = 0x3 << ED_BOUNDS_CLIP_MODE_SHIFT
};

typedef enum EdBorderStyle {
    ED_BORDER_SOLID = 0,
    ED_BORDER_DASHED = 1,
    ED_BORDER_DOTTED = 2
} EdBorderStyle;

typedef enum EdObjectFit {
    ED_OBJECT_FIT_FILL = 0,
    ED_OBJECT_FIT_CONTAIN = 1,
    ED_OBJECT_FIT_COVER = 2,
    ED_OBJECT_FIT_NONE = 3,
    ED_OBJECT_FIT_SCALE_DOWN = 4
} EdObjectFit;

typedef enum EdBlendMode {
    ED_BLEND_SRC_OVER = 0,
    ED_BLEND_MULTIPLY = 1,
    ED_BLEND_SCREEN = 2,
    ED_BLEND_OVERLAY = 3,
    ED_BLEND_DARKEN = 4,
    ED_BLEND_LIGHTEN = 5
} EdBlendMode;

typedef enum EdPathVerb {
    ED_PATH_MOVE_TO = 0,
    ED_PATH_LINE_TO = 1,
    ED_PATH_QUAD_TO = 2,
    ED_PATH_CUBIC_TO = 3,
    ED_PATH_CLOSE = 4
} EdPathVerb;

typedef enum EdFadeEdge {
    ED_FADE_NONE = 0,
    ED_FADE_LEFT = 1 << 0,
    ED_FADE_TOP = 1 << 1,
    ED_FADE_RIGHT = 1 << 2,
    ED_FADE_BOTTOM = 1 << 3
} EdFadeEdge;

enum {
    ED_FADE_ALL_MASK = ED_FADE_LEFT | ED_FADE_TOP | ED_FADE_RIGHT | ED_FADE_BOTTOM
};

typedef enum EdBackendType {
    ED_BACKEND_NONE = 0,
    ED_BACKEND_WEBGPU = 1,
    ED_BACKEND_WEBGL2 = 2,
    ED_BACKEND_CPU = 3
} EdBackendType;

typedef enum EdDeviceState {
    ED_DEVICE_OK = 0,
    ED_DEVICE_LOST = 1,
    ED_DEVICE_RECOVERING = 2
} EdDeviceState;

void ed_init(uint32_t physical_w, uint32_t physical_h, float dpr);
void ed_init_webgl(uint32_t physical_w, uint32_t physical_h, float dpr);
void ed_init_sw(uint32_t physical_w, uint32_t physical_h, float dpr);
void ed_resize(uint32_t physical_w, uint32_t physical_h, float dpr);

void ed_register_font(uint32_t font_id, const uint8_t* bytes, uint32_t len);
void ed_unregister_font(uint32_t font_id);
void ed_register_svg(uint32_t svg_id, const uint8_t* bytes, uint32_t len);
void ed_register_texture_rgba(uint32_t texture_id, const uint8_t* rgba, uint32_t w, uint32_t h, uint32_t byte_length);
void ed_unregister_texture(uint32_t texture_id);

void ed_execute_command_buffer(const uint32_t* buffer, uint32_t length);
void ed_render_frame(double current_time_ms);
void ed_recover_device(void);

uint64_t ed_hit_test(float logical_x, float logical_y);
ed_ptr_t ed_get_sw_framebuffer(void);
EdBackendType ed_get_backend_type(void);
EdDeviceState ed_get_device_state(void);

/* Debug / test only – simulates a device loss without destroying the GPU context. */
void ed_debug_simulate_device_lost(void);

#ifdef __cplusplus
}
#endif
