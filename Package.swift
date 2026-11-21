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
            exclude: ["src/transfer/unicode_tables.inc"],  // included by unicode.cpp
            sources: ["src", "third_party/monocypher/monocypher.c"],
            publicHeadersPath: "include",
            // Optimized in Debug too: unoptimized, a transfer moves 17 MB/s instead of 240.
            cSettings: [
                .headerSearchPath("third_party/monocypher"),
                .unsafeFlags(["-O2"], .when(configuration: .debug)),
            ],
            cxxSettings: [
                .headerSearchPath("src"), .headerSearchPath("third_party/monocypher"),
                .headerSearchPath("third_party/asio/include"),
                .define("ASIO_STANDALONE"), .define("ASIO_NO_DEPRECATED"),
                .unsafeFlags(["-O2"], .when(configuration: .debug)),
            ]
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
