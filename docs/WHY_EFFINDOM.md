# Escaping the Document Web: Why We Built EffinDom (and Why Every Other Canvas Framework Failed)

For 30 years, the software industry has been trapped in a lie. 

We took a document viewer designed for reading academic papers in 1995, and we duct-taped it into an application platform. The DOM was never meant to be a UI framework. It accidentally became the "Big Ball of Mud" legacy database of the internet—a global, mutable, untyped contract that every framework, extension, and CSS file reaches into and corrupts. JavaScript was a scripting language that accidentally became our Operating System. 

I first got the idea for EffinDom around 2017/2018. I'd sit in brown-bag talks listening to colleagues evangelize MobX, then Redux — each sold as the fix, each missing the point. Years later Zustand appeared and the same cycle repeated, same scaffolding, same cracked foundation. None of them were addressing the actual problem. And every time I'd walk out feeling the same quiet sadness: we're not fixing anything. We're just building more elaborate scaffolding around a foundation that was never meant to hold weight. The DOM wasn't designed for applications. JavaScript wasn't designed to run for 40 years. And yet here we were, debating which state-management library would paper over the cracks best.

The past three decades of frontend tech—React, Virtual DOMs, CSS-in-JS—are not cures. They are just progressively more complex bandages trying to hide the fact that we are building on the wrong foundation. 

Eight years ago, when WebAssembly was first announced, the escape hatch finally opened. The realization hit: we didn't have to fix the DOM. We could bypass it entirely. 

But escaping the DOM is brutal. If you look for EffinDom V1, you won't find it. "V1" wasn't a product; it was eight years of bleeding on the bleeding edge. It was a graveyard of failed experiments. We battled WebGL text rendering, fought WASM memory fragmentation, struggled with accessibility, and learned exactly why naive canvas frameworks fail. V1 was the tuition paid to learn the actual physics of UI engineering.

EffinDom V2 is the result. We stopped trying to build a web framework, and we built a **POSIX-style Display Server for WebAssembly**. 

But we aren't the first to try drawing UI to a canvas. So why did everyone else fail, and how does EffinDom actually fix the internet's original sin?

---

### The Canvas Graveyard: Why Existing Frameworks Fail

When you tell people you are building a "Direct Canvas UI," they immediately put you in one of three buckets. All three of those buckets are fundamentally flawed.

#### 1. The Game Engines (Three.js, PixiJS)
People build UIs in Three.js all the time, but they always feel like cheap video game menus. Why? Because game engines do not understand **Typography** or **Accessibility**.
*   **The Flaw:** Game engines rely on Bitmap Fonts or MSDF. These completely fail the moment a user types Arabic (Right-to-Left), Japanese, or an Emoji. Furthermore, a WebGL canvas is a black box. Screen readers and password managers see nothing.
*   **The EffinDom Fix:** EffinDom is built for *Applications*. Our C++ architecture integrates **HarfBuzz** and **ICU** for flawless global typography, line-breaking, and BiDi text flow. For accessibility, our JS Bridge projects a lightweight **Semantic Buffer** (a hidden DOM) perfectly synced to the GPU coordinates, giving screen readers and 1Password exactly what they need without slowing down the render loop.

#### 2. The Monoliths (Flutter Web)
Flutter Web is the closest thing to EffinDom on the market, but it is fundamentally a **mobile framework shoved into a browser tab**. 
*   **The Flaw:** Flutter forces you to write Dart. Worse, it compiles the Skia engine, the Dart runtime, and your app logic into one massive, multi-megabyte monolithic payload. If a user visits two different Flutter apps, they download the engine twice. You cannot lazy-load routes efficiently. It breaks the URL distribution model of the web.
*   **The EffinDom Fix:** EffinDom uses a **Web DLL Architecture**. The heavy C++ render engine (`effindom-core.wasm`), the UI framework (`effindom-ui.wasm`), the `.ttf` fonts, and the ICU data are cached globally on a CDN. Once downloaded, they are reused forever across *any* application built with EffinDom. Your actual application payload is just your business logic—typically around **100KB**. Furthermore, EffinDom is a **C-ABI Display Server**. You don't have to write Dart. You can write your app in AssemblyScript, Rust (coming soon), or C# (planned next, though the AOT-compiled CLR will naturally carry a slightly larger footprint). We make every language a first-class citizen.

#### 3. The Desktop Transplants (Egui, Iced)
The Rust community hates the DOM, so they often turn to native GUI engines like `egui` or `iced` compiled to WASM.
*   **The Flaw:** These are desktop frameworks that treat the browser as a dumb glass panel. They statically link their own text shaping and GPU rasterizers, resulting in **5MB to 15MB** app payloads. When deployed to the web, they completely break mobile text input (IME) because they try to manually calculate keystrokes instead of using the OS.
*   **The EffinDom Fix:** EffinDom treats the browser as a **Hardware Abstraction Layer (HAL)**. Instead of fighting mobile keyboards, our JS Bridge projects a perfectly synced Hidden DOM over the canvas. This tricks iOS and Android into providing their native text selection handles, autocorrect, and IME composition natively. For media, we don't decode JPEGs in WASM; we use the browser's native hardware-accelerated image decoding to pipe pixels directly into VRAM. We don't fight the browser; we orchestrate it.

---

### The EffinDom Architecture: A True Operating System

To achieve 60fps performance, global CDN caching, and multi-language freedom, EffinDom abandons the web's single-threaded model and adopts a strict **3-Tier OS Architecture**:

1. **Tier 1: The Render Engine / Display Server (`effindom-core.wasm`)**
   A blazingly fast C++ WebGL2 rasterizer (with WebGPU planned for the future) and a software fallback. It knows nothing about UI, Flexbox, or strings. It is a dumb, memory-safe engine that accepts a binary Command Buffer of absolute coordinates and Glyph IDs, and blasts them to the screen.
2. **Tier 2: The Window Manager (`effindom-ui.wasm`)**
   A C++ system library that handles the heavy lifting: Flexbox (Yoga), text shaping (HarfBuzz), focus routing, and global typography. We supply a custom-tailored ICU data payload supporting CJK languages and BiDi text, kept to a lean ~3.5MB (and drastically smaller over the wire with Brotli compression). It runs completely isolated from the GPU.
3. **Tier 3: The Userland (Fui SDKs)**
   A strictly-typed, zero-allocation SDK that lets developers build apps in AssemblyScript (`fui-as`), Rust (`fui-rs`), or C# (`fui-dotnet`). Developers use beautiful, declarative code powered by fine-grained Signals. No HTML. No CSS. No Virtual DOM diffing.

### The Endgame

We are not building another JavaScript framework to add to the pile. We are building the infrastructure for the next 20 years of the web. 

In fact, we are making JavaScript an implementation detail. By hiding it entirely behind a strict bridge interface, we are future-proofing the platform. As browser vendors roll out WASI (WebAssembly System Interface), that bridge can be swapped out. In the future, this exact same architecture will compile directly to native desktop, iOS, and Android applications.

By separating the Render Engine from the UI Framework, and the UI Framework from the Business Logic, we have created a system where applications boot in milliseconds, render at 60fps, and never leak memory. 

The DOM was a beautiful experiment for documents. But for applications, its time has passed. Welcome to the Good Timeline.