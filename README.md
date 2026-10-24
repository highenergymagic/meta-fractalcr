# meta-fractalcr

The FractalCR OpenEmbedded layer for x86-64 UEFI, serial console, CIP SLTS,
read-only initramfs and persistent Btrfs data. It includes Docker and pinned
Kata/Cloud Hypervisor, Compose, Buildx and BuildKit inputs.

Build through the [integration repository](https://github.com/highenergymagic/fractalcr).
It pins this repository at `os/meta-fractal`, supplies the shared supervisor and
management sources, embeds the public firmware certificate, and pins compatible
Wrynose upstream layers. This layer alone is not a complete build checkout.

`conf/layer.conf` declares layer compatibility and dependencies. See integration
documentation for build commands, dependency updates, trust policy, qualification
and known security limitations. Upstream licenses remain applicable to included
software; this repository's first-party code is MIT licensed.
