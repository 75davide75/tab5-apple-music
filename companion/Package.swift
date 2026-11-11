// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "tab5-companion",
    platforms: [.macOS(.v13)],
    targets: [
        .target(name: "CompanionCore"),
        .executableTarget(name: "tab5-companion", dependencies: ["CompanionCore"]),
        .testTarget(name: "CompanionCoreTests", dependencies: ["CompanionCore"]),
    ]
)
