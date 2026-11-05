// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "DriftglassMacObserver",
    platforms: [.macOS(.v13)],
    products: [
        .executable(name: "driftglass-macos-observer", targets: ["DriftglassMacObserver"])
    ],
    targets: [
        .executableTarget(name: "DriftglassMacObserver"),
        .testTarget(name: "DriftglassMacObserverTests", dependencies: ["DriftglassMacObserver"])
    ]
)
