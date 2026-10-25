# meta-fractalcr

The FractalCR OpenEmbedded layer for x86-64 UEFI, serial console, CIP SLTS,
read-only initramfs and persistent Btrfs data. It includes Docker and pinned
Kata/Cloud Hypervisor, Compose, Buildx and BuildKit inputs.

Build through the [integration repository](https://github.com/highenergymagic/fractalcr).
It pins this repository at `os/meta-fractal`, supplies the shared supervisor and
management sources, embeds the public firmware certificate, and pins compatible
Wrynose upstream layers. This layer alone is not a complete build checkout.

`conf/layer.conf` declares layer compatibility and dependencies.

| Task | Integration guide |
| --- | --- |
| Build the system image | [Building from source](https://github.com/highenergymagic/fractalcr/blob/main/docs/building.md) |
| Update upstream versions | [Dependency maintenance](https://github.com/highenergymagic/fractalcr/blob/main/docs/dependencies.md) |
| Test a signed image | [Firmware qualification](https://github.com/highenergymagic/fractalcr/blob/main/os/docs/cli-and-container-qualification.md) |
| Review security controls and open findings | [Security assessment](https://github.com/highenergymagic/fractalcr/blob/main/docs/security-review.md) |

Upstream licenses remain applicable to included software; this repository's
first-party code is MIT licensed.
