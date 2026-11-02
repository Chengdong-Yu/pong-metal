# Agent instructions

## Responsibility

Maintain PongMetal, the Pong variant of the UniXNU `PongMetal` component: Pong 的 Adreno 730 Metal 前端

## Required reading

1. Read this repository's README and the [component catalog](https://unixnu.com/reference/components).
2. Read the [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong) device repository: `board.toml`, `dts/pong.dts`, and the matching `porting.toml` areas.
3. Read the relevant implementation and its build entry point before editing.
4. For kernel work, read XNU sources, the selected patch series, and the [patch reference](https://unixnu.com/xnu/patch-reference).
5. For image assembly, read the [packaging](https://unixnu.com/zh/porting/packaging) and [UEFI firmware](https://unixnu.com/porting/uefi-firmware) pages.

## Scope

This repository holds only this driver's sources. Board inputs live in the device repository:
`configs/`, `dts/`, `board.toml`, and the build scripts under `scripts/`.

The device repository checks this tree out at `drivers/PongMetal`. Sibling drivers are reached with
relative includes such as `../PongPower/PongPlatformResources.h`, so directory names must not change.
No firmware, restore images, private binaries, or credentials may be committed here.

## Build and verification

Build from the device repository, which owns the toolchain and the kernel preparation:

- `scripts/build-pong-format2.py --unixnu <checkout> --out <dir>`
- or the narrower `scripts/build-*` entry point named in the README.

Report the commands, results, and source revisions. Report hardware observations separately from
build results, together with the device and OS profile. A successful link is not evidence that the
hardware works.

## Documentation

Maintain this README. State the hardware the driver targets and the interfaces it publishes.
Use concise language describing current behavior, and never claim a device result that a log does
not show.
