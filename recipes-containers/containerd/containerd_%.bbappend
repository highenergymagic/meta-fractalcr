# Remain on the pinned OE integration's 2.2 maintenance branch.
SRCREV = "991ca6a8ca58b47346a42093210099e090bc6c1f"
CONTAINERD_VERSION = "2.2.10"
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
# These backports are already contained in the reviewed upstream release.
SRC_URI:remove = "file://CVE-2026-46680.patch file://CVE-2026-47262.patch file://CVE-2026-53488.patch"
SRC_URI += "file://containerd-security.patch.gz"
export GOPROXY = "off"
export GOSUMDB = "off"
export GOTOOLCHAIN = "local"
export GOFLAGS = "-mod=vendor -buildvcs=false"
EXTRA_OEMAKE += "VERSION=v${CONTAINERD_VERSION} REVISION=${SRCREV}"
