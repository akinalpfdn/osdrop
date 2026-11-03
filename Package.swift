// swift-tools-version: 6.0
import PackageDescription

// The shared C++ core and its Swift wrapper. Only the C header in core/include is public C API;
// the CMake build in core/ is the reference build and must list the same sources.
let package = Package(
    name: "OsdropCore",
    platforms: [.macOS(.v14)],
    products: [
        .library(name: "CoreBridge", targets: ["CoreBridge"])
    ],
    targets: [
        .target(
            name: "OsdropCore",
            path: "core",
            sources: ["src", "third_party/monocypher/monocypher.c"],
            publicHeadersPath: "include",
            cSettings: [.headerSearchPath("third_party/monocypher")],
            cxxSettings: [.headerSearchPath("src"), .headerSearchPath("third_party/monocypher")]
        ),
        .target(
            name: "CoreBridge",
            dependencies: ["OsdropCore"],
            path: "macos/CoreBridge"
        ),
        .testTarget(
            name: "CoreBridgeTests",
            dependencies: ["CoreBridge", "OsdropCore"],
            path: "macos/CoreBridgeTests"
        ),
    ],
    cxxLanguageStandard: .cxx20
)
