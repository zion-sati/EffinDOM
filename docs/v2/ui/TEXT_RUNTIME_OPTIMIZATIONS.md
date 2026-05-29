# v2 UI text runtime optimizations

This note records the retained text-runtime fast paths that keep `Text`,
`TextInput`, and `TextArea` responsive on large documents. The public text model
is still one logical UTF-8 buffer per node; the optimizations only change how
Tier 2 caches, reshapes, and re-emits layout data.

## Layout cache tiers

The runtime now keeps text data in tiers so the hot path can reuse the cheapest
valid representation instead of starting from raw UTF-8 on every query.

1. Raw line-start index: `text_line_starts` tracks hard lines for byte-to-line
   mapping and paragraph discovery.
2. Logical line shapes: one retained shaped run per hard line, plus shared
   cluster-stop geometry and cached break-candidate data.
3. Non-wrap fragment cache: wide logical lines are partitioned into stable
   horizontal shards for viewport culling and local edit patching.
4. Wrapped visual-line cache: wrapped paragraphs keep per-visual-line metadata
   and lazily materialized glyph payloads above the shared logical-line shape.

The rule is simple: reuse the highest valid tier, and only fall back to a lower
one when an edit invalidates that exact tier.

## Line-start index

`text_line_starts` is the cheapest retained text structure in Tier 2. It exists
to avoid rescanning the entire document for every paragraph discovery, `Home`,
`End`, vertical movement, or multiline edit.

Key invariants:

- newline-bearing edits must bail out of incremental patching and rebuild,
- same-line edits may patch the line-start suffix by shifting downstream starts,
- non-wrapped multiline textboxes may add virtual starts every 16,384 UTF-8
  codepoints for an absurdly long physical line; these starts are layout metadata
  only and must never insert `\n` into the user text buffer,
- direct/programmatic text replacement may leave the cache dirty, but every
  query path must self-heal before using it.

This index is intentionally dumb: it only knows about real hard line starts plus
the virtual long-line guardrail starts above, not wrapping or viewport state.

## Bounded shaping shards

Tier 2 also bounds the raw HarfBuzz input size. `ShapeTextWithFont(...)` splits
very large same-font runs into 16 KiB shards, preferring nearby whitespace and
otherwise falling back to a UTF-8 boundary. The resulting glyph clusters are
rebased into the original string's byte space before callers see them.

This keeps pathological long runs from becoming one unbounded HarfBuzz buffer.
It is a guardrail, not a semantic text split: the public text buffer and callback
text stay unchanged.

## Non-wrap fragment cache

Very wide non-wrapped lines are split into bounded fragments. Each fragment
stores:

- owning visible hard-line index,
- local byte start/end relative to that visible hard line,
- x origin,
- width.

The renderer, hit testing, caret geometry, and selection geometry all resolve a
visible fragment window with small overscan and only shape/emit that window.
This keeps horizontal scrolling and long-line interaction proportional to the
visible span instead of total line length.

## Incremental non-wrap patching

For same-line edits on non-wrapped text, Tier 2 should not throw away the whole
paragraph cache. The patch path instead:

1. computes the changed byte span,
2. finds the touched fragment band plus overscan,
3. reshapes only that local replacement span,
4. rebases replacement fragments to the real line-local byte/x origin,
5. shifts downstream fragment metadata by the byte/width delta,
6. invalidates render-window state without discarding the whole paragraph cache.

If the edit changes newline structure or violates the fast-path assumptions, the
runtime must fall back to full invalidation explicitly.

## Wrapped break metrics

Wrapped layout reuses the logical-line shape instead of probing candidate
substrings from scratch. For ASCII-heavy content the runtime can derive prefix
X metrics from the shared shaped line and use those metrics to measure wrap
candidates cheaply.

Wrapped caches keep:

- logical-line break candidates,
- optional shared candidate X metrics,
- shard-local resume state for large logical lines,
- visual-line metadata and lazy glyph materialization state.

That lets wrapped relayout reuse prefix/suffix work and restart from the
affected neighborhood after edits.

## Maintenance rules

- Do not add a new hot path that reshapes from byte `0` unless there is no
  retained cache tier that can answer it safely.
- Keep fallback explicit. If a cache assumption no longer holds, rebuild rather
  than silently reusing stale retained data.
- When a new optimization changes retained cache invariants, add a focused
  native regression that compares the incremental path against a fresh layout.
- Keep SRP boundaries intact:
  - `UiRuntimeTextLayout.cpp` owns paragraph and logical-line cache work,
  - `UiRuntimeTextNonWrap.cpp` owns non-wrap fragment virtualization and patching,
  - `UiRuntimeWrappedText.cpp` owns wrapped-cache reuse and relayout,
  - `UiRuntimeEditingTextState.cpp` owns raw line-start and post-edit cache coordination.
