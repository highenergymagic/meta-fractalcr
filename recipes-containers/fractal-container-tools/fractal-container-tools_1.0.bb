SUMMARY = "Source-built Compose, Buildx, BuildKit and CNI tools"
FCR_COMPOSE_VERSION = "5.6.1"
FCR_BUILDX_VERSION = "0.38.1"
FCR_BUILDKIT_VERSION = "0.34.1"
FCR_CNI_VERSION = "1.9.1"
LICENSE = "Apache-2.0 & GPL-2.0-only"
LIC_FILES_CHKSUM = "file://compose/LICENSE;md5=175792518e4ac015ab6696d16c4f607e \
                    file://buildx/LICENSE;md5=3b83ef96387f14655fc854ddc3c6bd57 \
                    file://buildkit/LICENSE;md5=86d3f3a95c324c9479bd8986968f4327 \
                    file://cni/LICENSE;md5=e3fc50a88d0a364313df4b21ef20c29e \
                    file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"
SRC_URI = "git://github.com/docker/compose;protocol=https;nobranch=1;name=compose;destsuffix=compose \
           git://github.com/docker/buildx;protocol=https;nobranch=1;name=buildx;destsuffix=buildx \
           git://github.com/moby/buildkit;protocol=https;nobranch=1;name=buildkit;destsuffix=buildkit \
           git://github.com/containernetworking/plugins;protocol=https;nobranch=1;name=cni;destsuffix=cni \
           file://compose-security.patch.gz;patchdir=compose \
           file://compose-vendor.tar.gz;subdir=compose \
           file://buildx-security.patch.gz;patchdir=buildx \
           file://buildkit-security.patch.gz;patchdir=buildkit \
           https://github.com/moby/buildkit/releases/download/v0.34.1/buildkit-v0.34.1.linux-amd64.tar.gz;name=qemu;subdir=qemu"
# Retain cross-architecture emulators while replacing every Go executable.
# Reproducing the upstream QEMU build is tracked separately in the release audit.
SRC_URI[qemu.sha256sum] = "a59539daf547fe0958efe2d07e41c0a6944e1112a66d7e519ec1cf5b673ef15b"
SRCREV_compose = "6cbc19a3fd59e305fb814dca4d854a4fb67d2941"
SRCREV_buildx = "0dbe87f36ed472fe36b6796c3f70b4210a731fc5"
SRCREV_buildkit = "f5b59a1f2fb25a0cb7072c398cab9aed7835da4e"
SRCREV_cni = "adc3e6b5b581638afbd194cf2e9319ecbb0151a1"
SRCREV_FORMAT = "compose_buildx_buildkit_cni"
S = "${UNPACKDIR}"
inherit fractal-go-source
RDEPENDS:${PN} += "runc"
INSANE_SKIP:${PN} += "already-stripped"

do_compile() {
    install -d ${B}/bin
    cd ${S}/buildkit
    for program in buildkitd; do
        go build -ldflags="-buildid= -X github.com/moby/buildkit/version.Version=v0.34.1 -X github.com/moby/buildkit/version.Revision=${SRCREV_buildkit}" \
            -o ${B}/bin/$program ./cmd/$program
    done
    cd ${S}/cni
    for plugin in main/bridge main/loopback ipam/host-local meta/firewall; do
        go build -ldflags="-buildid=" -o ${B}/bin/buildkit-cni-$(basename $plugin) ./plugins/$plugin
    done
}

do_install() {
    install -d ${D}${libdir}/docker/cli-plugins ${D}${bindir} ${D}${datadir}/fractal
    install -m 0755 ${B}/bin/buildkitd ${B}/bin/buildkit-cni-* ${D}${bindir}/
    install -m 0755 ${S}/qemu/bin/buildkit-qemu-* ${D}${bindir}/
    ln -s runc ${D}${bindir}/buildkit-runc
    printf 'compose=5.6.1\nbuildx=0.38.1\nbuildkit=0.34.1\ncni=1.9.1\n' > ${D}${datadir}/fractal/container-tools.versions
}
FILES:${PN} += "${libdir}/docker/cli-plugins ${datadir}/fractal/container-tools.versions"
