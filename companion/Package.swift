// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "tab5-companion",
    platforms: [.macOS(.v13)],
    targets: [
        .target(name: "CompanionCore"),
        .executableTarget(
            name: "tab5-companion",
            dependencies: ["CompanionCore"],
            exclude: ["Info.plist"],
            linkerSettings: [.unsafeFlags(["-Xlinker", "-sectcreate", "-Xlinker", "__TEXT", "-Xlinker", "__info_plist",
                                           "-Xlinker", "Sources/tab5-companion/Info.plist"])]
        ),
        .testTarget(name: "CompanionCoreTests", dependencies: ["CompanionCore"]),
    ]
)
