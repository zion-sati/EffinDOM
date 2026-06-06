# Escaping the Document Web: Why I Built EffinDom (and Why Every Other Canvas Framework Failed)

For 30 years, the software industry has been trapped in a lie. 

We took a document viewer designed for reading academic papers in 1995, and we duct-taped it into an application platform. The DOM was never meant to be a UI framework. It accidentally became the "Big Ball of Mud" legacy database of the internet—a global, mutable, untyped contract that every framework, extension, and CSS file reaches into and corrupts. JavaScript was a scripting language that accidentally became our Operating System. 

I first got the idea for EffinDom around 2017/2018. I'd sit in brown-bag talks listening to colleagues evangelize MobX, then Redux — each sold as the fix, each missing the point. Years later Zustand appeared and the same cycle repeated, same scaffolding, same cracked foundation. None of them were addressing the actual problem. And every time I'd walk out feeling the same quiet sadness: we're not fixing anything. We're just building more elaborate scaffolding around a foundation that was never meant to hold weight. The DOM wasn't designed for applications. JavaScript wasn't designed to run for 40 years. And yet here we were, debating which state-management library would paper over the cracks best.

The past three decades of frontend tech—React, Virtual DOMs, CSS-in-JS—are not cures. They are just progressively more complex bandages trying to hide the fact that we are building on the wrong foundation. 

Eight years ago, when WebAssembly was first announced, the escape hatch finally opened. The realization hit: I didn't have to fix the DOM. I could bypass it entirely. 

But escaping the DOM is brutal. If you look for EffinDom V1, you won't find it. "V1" wasn't a product; it was eight years of bleeding on the bleeding edge. It was a graveyard of failed experiments. I battled WebGL text rendering, fought WASM memory fragmentation, struggled with accessibility, and learned exactly why naive canvas frameworks fail. V1 was the tuition paid to learn the actual physics of UI engineering.

EffinDom V2 is the result. I stopped trying to build a web framework, and I built a **POSIX-style Display Server for WebAssembly**. 

But I'm not the first to try drawing UI to a canvas. So why did everyone else fail, and how does EffinDom actually fix the internet's original sin?

Here's the pattern: Flutter started on mobile. Compose/Skiko started on desktop. egui and Iced started as native GUI frameworks. Three.js started as a 3D scene graph. Every single one of them arrived on the web as a port — a compile target, not a design target.

EffinDom is the only one that was born on the web, and that changes everything.

**Web-native means the architecture exploits the browser's actual physics instead of fighting them.** The Tier 1 and Tier 2 runtimes are immutable WebAssembly modules, content-hashed and served from a CDN. Once a browser downloads `effindom-core.wasm` and `effindom-ui.wasm`, they're cached locally forever — the same engine DLLs power every EffinDom app the user visits. The ICU data, the fonts, the HarfBuzz shaper — all cached, all shared.

Flutter and Compose can't do this. They're monolithic: every app bundles the engine. Visit two Flutter apps, download Skia twice. EffinDom's Web DLL split means the multi-megabyte engine lives on the CDN and in the browser cache, while your app payload is just your business logic (the hello-world scaffold is ~128 KB; real apps land in the low hundreds). That's web-native architecture — designed for the URL distribution model, the CDN, and the browser cache from the start.

---

### The Canvas Graveyard: Why Existing Frameworks Fail

When you tell people you are building a "Direct Canvas UI," they immediately put you in one of three buckets. All of them are fundamentally flawed — and none of them were built for the web first.

#### 1. The Game Engines (Three.js, PixiJS)
People build UIs in Three.js all the time, but they always feel like cheap video game menus. Why? Because game engines do not understand **Typography** or **Accessibility**.
*   **The Flaw:** Game engines rely on Bitmap Fonts or MSDF. These completely fail the moment a user types Arabic (Right-to-Left), Japanese, or an Emoji. Furthermore, a WebGL canvas is a black box. Screen readers and password managers see nothing.
*   **The EffinDom Fix:** EffinDom is built for *Applications*. Its C++ architecture integrates **HarfBuzz** and **ICU** for flawless global typography, line-breaking, and BiDi text flow. For accessibility, its JS Bridge projects a lightweight **Semantic Buffer** (a hidden DOM) perfectly synced to the GPU coordinates, giving screen readers and 1Password exactly what they need without slowing down the render loop.

#### 2. The Monoliths (Flutter Web)
Flutter Web is the closest thing to EffinDom on the market, but it is fundamentally a **mobile framework shoved into a browser tab**. Flutter was built for iOS and Android first; web support was retrofitted years later. The web is a compile target, not the design target.
*   **The Flaw:** Flutter forces you to write Dart. It compiles the Skia engine, the Dart runtime, and your app into one massive, multi-megabyte monolithic payload. Visit two Flutter apps, download the engine twice. You cannot lazy-load routes efficiently. It breaks the URL distribution model of the web.
*   **The EffinDom Fix:** The Web DLL split (explained above) means the engine is cached once and shared across every app. Plus, EffinDom is a **C-ABI Display Server** — you're not locked into Dart. Write AssemblyScript, Rust, or Kotlin (FUI-KT, coming soon). Every language a first-class citizen.

#### 3. The Desktop Transplants (Egui, Iced)
The Rust community hates the DOM, so they often turn to native GUI engines like `egui` or `iced` compiled to WASM.
*   **The Flaw:** These are desktop frameworks that treat the browser as a dumb glass panel. They statically link their own text shaping and GPU rasterizers, producing **5MB to 15MB** app payloads. When deployed to the web, they completely break mobile text input (IME) because they try to manually calculate keystrokes instead of using the OS.
*   **The EffinDom Fix:** EffinDom treats the browser as a **Hardware Abstraction Layer (HAL)**. Instead of fighting mobile keyboards, its JS Bridge projects a perfectly synced Hidden DOM over the canvas, letting iOS and Android provide native text selection handles, autocorrect, and IME composition. For media, it doesn't decode JPEGs in WASM; it uses the browser's native hardware-accelerated image decoding to pipe pixels directly into VRAM. It doesn't fight the browser; it orchestrates it.

#### 4. Compose Multiplatform / Skiko
JetBrains built Compose Multiplatform on top of Skiko — their Skia bindings for Kotlin. Skiko was created for Compose Desktop (JVM) first; WASM support came later as a port. It's the most credible Kotlin-to-GPU story out there, but like the others it started somewhere else and arrived on the web as an afterthought.
*   **The Flaw:** Desktop-first architecture ported to the browser. Compose ties you to the Gradle/JVM ecosystem and a Compose-specific component model. Targeting WASM means shipping the Kotlin/Wasm runtime plus Skiko's Skia bindings, inflating payload sizes and coupling your app to JetBrains' rendering pipeline.
*   **The EffinDom Fix:** EffinDom was born on the web. FUI-KT will render Kotlin directly through EffinDom's Tier 1/2 C-ABI — the same pipeline FUI-AS and FUI-RS use, purpose-built for the browser from day one. No Gradle, no Compose component model, no Skiko dependency. Write Kotlin, target the same GPU backend, ship a lean WASM payload. The runtime is already cached. Your app is just your app.

---

### The EffinDom Architecture: A True Operating System

To achieve 60fps performance, global CDN caching, and multi-language freedom, EffinDom abandons the web's single-threaded model and adopts a strict **3-Tier OS Architecture**:

1. **Tier 1: The Render Engine / Display Server (`effindom-core.wasm`)**
   A blazingly fast C++ WebGL2 rasterizer (with WebGPU planned for the future) and a software fallback. It knows nothing about UI, Flexbox, or strings. It is a dumb, memory-safe engine that accepts a binary Command Buffer of absolute coordinates and Glyph IDs, and blasts them to the screen.
2. **Tier 2: The Window Manager (`effindom-ui.wasm`)**
   A C++ system library that handles the heavy lifting: Flexbox (Yoga), text shaping (HarfBuzz), focus routing, and global typography. I supply a custom-tailored ICU data payload supporting CJK languages and BiDi text, kept to a lean ~3.5MB (and drastically smaller over the wire with Brotli compression). It runs completely isolated from the GPU.
3. **Tier 3: The Userland (Fui SDKs)**
   A strictly-typed, zero-allocation SDK that lets developers build apps in AssemblyScript (`fui-as`), Rust (`fui-rs`), or Kotlin (`fui-kt` — coming soon, rendering directly through Tier 1/2, no Skiko dependency). Developers use beautiful, declarative code powered by fine-grained Signals. No HTML. No CSS. No Virtual DOM diffing.

### The Endgame

I am not building another JavaScript framework to add to the pile. I am building the infrastructure for the next 20 years of the web. Web-native, not web-ported.

In fact, I am making JavaScript an implementation detail. By hiding it entirely behind a strict bridge interface, I am future-proofing the platform. As browser vendors roll out WASI (WebAssembly System Interface), that bridge can be swapped out. In the future, this exact same architecture will compile directly to native desktop, iOS, and Android applications.

By separating the Render Engine from the UI Framework, and the UI Framework from the Business Logic, I have created a system where applications boot in milliseconds, render at 60fps, and never leak memory. 

The DOM was a beautiful experiment for documents. But for applications, its time has passed. Welcome to the Good Timeline.
