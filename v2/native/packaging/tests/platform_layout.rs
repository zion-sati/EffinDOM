use effindom_native_packaging::{NativeLibrarySearch, NativePackageLayout, NativePlatform};
use std::path::Path;

#[test]
fn macos_layout_uses_app_frameworks_and_loader_relative_search() {
    let layout = NativePackageLayout::for_application("sample", NativePlatform::MacOs);
    assert_eq!(layout.root, Path::new("sample.app"));
    assert_eq!(
        layout.executable,
        Path::new("sample.app/Contents/MacOS/sample")
    );
    assert_eq!(
        layout.runtime_libraries,
        Path::new("sample.app/Contents/Frameworks")
    );
    assert_eq!(
        layout.library_search,
        NativeLibrarySearch::LoaderRelative("@loader_path/../Frameworks".to_owned())
    );
}

#[test]
fn windows_layout_places_runtime_libraries_beside_executable() {
    let layout = NativePackageLayout::for_application("sample", NativePlatform::Windows);
    assert_eq!(layout.executable, Path::new("sample/sample.exe"));
    assert_eq!(layout.runtime_libraries, Path::new("sample"));
    assert_eq!(
        layout.library_search,
        NativeLibrarySearch::ExecutableDirectory
    );
}

#[test]
fn linux_layout_uses_sibling_lib_and_origin_relative_search() {
    let layout = NativePackageLayout::for_application("sample", NativePlatform::Linux);
    assert_eq!(layout.executable, Path::new("sample/bin/sample"));
    assert_eq!(layout.runtime_libraries, Path::new("sample/lib"));
    assert_eq!(
        layout.library_search,
        NativeLibrarySearch::LoaderRelative("$ORIGIN/../lib".to_owned())
    );
}
