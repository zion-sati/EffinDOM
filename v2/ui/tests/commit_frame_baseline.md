# CommitFrame Baseline

Phase 0 records three stable workload categories through `UiRuntime::TextCommitProfile`:

- initial layout: `layout_stabilization_passes` must be in `1..4`;
- idle frame: `layout_stabilization_passes` must be `0`;
- active scrolling and dirty text: profile timing fields remain observable while retained output updates.

Current local debug baseline, captured during Phase 0 on the development machine:

- initial Yoga layout: `0.027 ms`;
- idle commit: `0.063 ms`;
- scroll commit: `0.069 ms`;
- dirty text commit: `0.209 ms`.

Wall-clock millisecond values are deliberately not asserted: they vary with CPU, compiler, Skia cache state, and debug versus release configuration. To capture a comparable local baseline, run:

```sh
cmake --build build/build-v2-ui --target effindom_v2_ui_tests -j4
build/build-v2-ui/v2/ui/effindom_v2_ui_tests "[commit]" -s
```

The characterization tests cover ordering for deletion, creation, and prepared commands. Existing portal, semantic-scroll, momentum, and text-layout suites cover the remaining CommitFrame phases. Any future subsystem extraction must retain those test categories and compare `TextCommitProfile` work distribution before accepting a performance regression.
