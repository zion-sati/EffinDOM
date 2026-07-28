# Accessibility Text Baseline

Phase N7E.0 preserves two separate text paths:

- the semantic buffer carries a bounded summary for every textbox; its default
  limit is 1,000 Unicode code points plus an ellipsis when truncated;
- the retained text document and browser editor session remain authoritative for
  complete text, caret, selection, IME, and range geometry.

The semantic summary is UTF-8 safe. Increasing a textbox from 4,096 to 16,384
ASCII characters leaves its serialized semantic record at 266 words. The focused
`[text-cap]` Tier 2 tests lock the code-point boundary, multibyte behavior, and
serialized-size invariant without relying on wall-clock timing.

Existing repeatable work-distribution baselines provide the remaining N7E.0
characterization:

- `scroll-only semantic projection does not read or rewrite unchanged large editor values`
  instruments the browser editor value and proves parent scrolling performs no
  full-value read or assignment;
- `wheel scrolling defers large editor semantic geometry until scroll idle`
  proves active scrolling does not repeatedly project editable geometry;
- `v2 ui measures viewport-bounded Tier 2 glyph traffic by mutation cause`
  proves idle and selection-only commits emit no glyph work and scrolling emits
  viewport-bounded work;
- `v2 ui retains multiline textbox work across ancestor viewport re-entry`
  proves retained line data is reused when a large textbox leaves and re-enters
  an ancestor viewport;
- the text editing, selection, IME, password, CJK, emoji, combining-mark, bidi,
  and UTF-8 suites characterize the current authoritative editor behavior.

Wall-clock thresholds are intentionally excluded because architecture, compiler,
Skia caches, browser engine, and debug/release mode dominate absolute timings.
N7E uses structural counters, intercepted reads/writes, serialized sizes, and
work-distribution profiles as deterministic performance gates. A future lazy
accessibility text provider must not weaken any of these invariants.

Phase N7E.5 adds a deterministic lazy-query profile. A one-million-character
textbox commit and parent-scroll cycle records zero accessibility metadata,
range, geometry, or materialization work while accessibility is inactive. An
explicit three-character request records one metadata query, two bounded range
queries for the length/copy ABI, six requested characters in total, and ten
materialized UTF-8 bytes. No full document copy is attributed to accessibility.
