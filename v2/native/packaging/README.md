# EffinDOM Native Packaging Engine

`effindom-native-packaging` is the SDK-neutral implementation behind EffinDOM
native application packaging. Rust tooling can link the library directly;
other FUI SDKs invoke the precompiled `effindom-native-packager` executable
through its versioned JSON request and response contract.

Application developers normally use their language's FUI tooling rather than
calling this package directly.
