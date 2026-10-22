SUMMARY = "Pinned Compose, Buildx and BuildKit tools for the appliance"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"
COMPATIBLE_HOST = "x86_64.*-linux"
SRC_URI = "https://github.com/docker/compose/releases/download/v5.6.1/docker-compose-linux-x86_64;name=compose;downloadfilename=compose-v5.6.1-amd64;unpack=0 \
    https://github.com/docker/buildx/releases/download/v0.38.1/buildx-v0.38.1.linux-amd64;name=buildx;unpack=0 \
    https://github.com/moby/buildkit/releases/download/v0.34.1/buildkit-v0.34.1.linux-amd64.tar.gz;name=buildkit;subdir=buildkit"
SRC_URI[compose.sha256sum] = "47737f4236e6d3ccea7291995203f8f018058783b4c3a9469119811a0b5db8c0"
SRC_URI[buildx.sha256sum] = "e25d5cd675092bb5d1d03632fdc46cdf3fede7b514dc5f023a105e00d3ce5ad4"
SRC_URI[buildkit.sha256sum] = "a59539daf547fe0958efe2d07e41c0a6944e1112a66d7e519ec1cf5b673ef15b"
S = "${UNPACKDIR}"
do_configure[noexec] = "1"
do_compile[noexec] = "1"
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INSANE_SKIP:${PN} += "already-stripped ldflags buildpaths"
do_install() {
    install -d ${D}${libdir}/docker/cli-plugins ${D}${bindir} ${D}${datadir}/fractal
    install -m 0755 ${UNPACKDIR}/compose-v5.6.1-amd64 ${D}${libdir}/docker/cli-plugins/docker-compose
    install -m 0755 ${UNPACKDIR}/buildx-v0.38.1.linux-amd64 ${D}${libdir}/docker/cli-plugins/docker-buildx
    install -m 0755 ${UNPACKDIR}/buildkit/bin/* ${D}${bindir}/
    printf 'compose=5.6.1\nbuildx=0.38.1\nbuildkit=0.34.1\n' > ${D}${datadir}/fractal/container-tools.versions
}
FILES:${PN} += "${libdir}/docker/cli-plugins ${datadir}/fractal/container-tools.versions"
