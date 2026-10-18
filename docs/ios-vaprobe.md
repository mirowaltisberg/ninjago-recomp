# iOS address-space probe (Task 1a)

Probe app: `probe/ios-vaprobe/` (xcodegen; two targets, `VAProbe` without and `VAProbeEVA` with
`com.apple.developer.kernel.extended-virtual-addressing`). Team `43DHLG947A`.

## Findings

- **iOS 27.1 SDK requires the UIScene life cycle.** An app without a scene delegate fails to launch with
  "UIScene life cycle is required for apps built with this SDK". The M3 game shell must use a
  `UIWindowSceneDelegate`.
- Signing: automatic provisioning works for both targets; the extended-virtual-addressing
  entitlement is granted to the team's development profile.

## Results

| Run | 4 GiB reserve | 0x00100000 | 0x02000000 | 0x80000000 | 0xE0000000 | 0xF0000000 | 0xFFFF0000 | commit 512 MiB |
|---|---|---|---|---|---|---|---|---|
| Simulator (iPhone 17, iOS 27.1), plain | ok | ok | ok | ok | ok | ok | ok | ok |
| Simulator, EVA | ok | ok | ok | ok | ok | ok | ok | ok |
| iPhone 17 (iOS 27.0.1), plain | ok (at 0x7000000000) | ok | ok | ok | ok | ok | ok | ok (3370 → 2858 MiB available) |
| iPhone 17, EVA | ok (at 0x7000000000) | ok | ok | ok | ok | ok | ok | ok (3370 → 2858 MiB available) |

Device runs 2026-10-19 via `xcrun devicectl device process launch --console`. The 4 GiB reservation works on
the iPhone 17 even without the extended-virtual-addressing entitlement; the memory-map constants stay as they
are. The game app may still carry the entitlement for headroom.
