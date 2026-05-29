High-performance Testing Pyramid for Tier 1. We are going to test it exactly like a AAA game engine.

---

### The Secret Weapon: The Native Compile

The biggest mistake web-assembly developers make is only compiling to `.wasm`. 

Tier 1 is standard C++17. **You must configure your `CMakeLists.txt` to compile Tier 1 as a native desktop executable (macOS/Linux/Windows) for testing.** 

When you run tests, you run a native C++ binary. It executes in milliseconds. No V8 engine, no Emscripten, no browser.

---

### Level 1: Pure Math Unit Tests (The Base)
**Tool:** Catch2 or GoogleTest (Native C++)
**Speed:** 10,000 tests per second.

You test the logic of the engine without drawing a single pixel. You allocate the `memory_pool`, feed it a fake Command Buffer array, and assert the state.

**What to test:**
1. **Command Parsing:** Feed `[CMD_SET_BOUNDS, 42, 10, 10, 100, 100]`. Assert that `memory_pool[42].x == 10`.
2. **Hit-Testing (Crucial):** 
   * Feed a Command Buffer that creates 3 overlapping boxes.
   * Call `ed_hit_test(15, 15)`. 
   * Assert that it returns the Handle of the top-most box.
3. **Z-Index / Paint Order:** Feed a `CMD_COMMIT_SCENE` with `OP_PUSH_CLIP` and `OP_POP`. Assert that the internal C++ display list matches the expected hierarchy.
4. **Out-of-Bounds / Malformed Data:** Feed a Command Buffer with `Handle = 9999999` (out of bounds). Assert that the engine ignores it and doesn't segfault.

---

### Level 2: Headless Skia Snapshot Tests (The Middle)
**Tool:** Catch2 + Skia CPU Rasterizer (Native C++)
**Speed:** 100 tests per second.

How do you test that `CMD_SET_LINEAR_GRADIENT` actually draws a gradient, without using Playwright? 

**You use Skia's CPU Backend.**
Skia does not require WebGPU or a monitor to draw. It can draw directly into a block of system RAM.

**The Test Lifecycle:**
1. In your test setup, initialize an `SkSurface::MakeRasterN32Premul(800, 600)`. (This creates a headless, CPU-only canvas).
2. Feed a hardcoded Command Buffer into Tier 1 (e.g., draw a red box with a blue border).
3. Call `ed_render_frame(current_time_ms)`, passing the current frame timestamp while rendering into the CPU canvas instead of the WebGPU canvas.
4. Extract the pixels from the canvas: `surface->makeImageSnapshot()->encodeToData()`.
5. **The Assertion:** Hash the resulting pixel data (e.g., MD5 or SHA-256) and compare it against a known "Golden Hash", or save it as a `.png` and do a pixel-diff against a "Golden Image" stored in your repo.

*Verdict:* This replaces 90% of your Playwright tests. You are testing the exact Skia drawing code that will run in the browser, but you are doing it natively in C++ in milliseconds.

---

### Level 3: Fuzz Testing (The Shield)
**Tool:** libFuzzer or AFL++ (Native C++)
**Speed:** Millions of iterations per minute.

Because Tier 1 accepts a raw `u32` array from the JS Bridge, it is vulnerable to malicious or corrupted data. If Tier 2 has a bug and sends a malformed Command Buffer, Tier 1 must not crash.

You write a Fuzzer that feeds completely random garbage bytes into `ed_execute_command_buffer()`. You let it run on a CI server for an hour. If Tier 1 segfaults, you fix the bounds-checking. If it survives, your Kernel is bulletproof.

---

### Level 4: Playwright Smoke Tests (The Tip of the Pyramid)
**Tool:** Playwright + Headless Chromium
**Speed:** ~2-5 seconds per test.

You only use Playwright to test the things that *cannot* be tested natively in C++. You keep these to an absolute minimum (maybe 5 to 10 tests total).

**What to test:**
1. **WebGPU Initialization:** Does the JS Bridge successfully request the `GPUAdapter` and boot the WASM module without throwing a JS error?
2. **The WASM Boundary:** If JS calls `ed_create_node()`, does it return a valid Handle?
3. **The Canvas Output:** Boot the app, send a basic Command Buffer via JS, and use Playwright's `page.screenshot()` to ensure the WebGPU canvas actually displays pixels in the browser.
4. **Event Routing:** Use Playwright's `page.mouse.click()`. Assert that the JS Bridge catches it and successfully routes it to the WASM module.

---

### The Rubber Duck Verdict

If you rely on Playwright to test if a button is red, you will hate working on this project. 

By leveraging the fact that Tier 1 is just a C++ library:
1. **Unit Tests:** Verify the math and memory safety.
2. **Snapshot Tests (CPU Skia):** Verify the pixel output.
3. **Fuzz Tests:** Verify the ABI security.
4. **Playwright:** Verify the Browser/WebGPU glue.

This is exactly how Google tests Chrome (Blink/Skia) and how Epic Games tests Unreal Engine. You build the engine natively, test it natively, and treat the browser as just another "compile target." 
