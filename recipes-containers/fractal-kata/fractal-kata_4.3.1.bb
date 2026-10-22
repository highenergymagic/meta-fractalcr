SUMMARY = "Pinned Mariner guest for the source-built Kata runtime and VMM"
HOMEPAGE = "https://github.com/kata-containers/kata-containers"
# Upstream release binaries are used as immutable build inputs. Their source
# versions are retained in /opt/kata/versions.yaml; guest notices live in its FS.
LICENSE = "Apache-2.0 & GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10 \
                   file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"
SRC_URI = "https://github.com/kata-containers/kata-containers/releases/download/${PV}/kata-go-static-${PV}-amd64.tar.zst;subdir=kata"
SRC_URI[sha256sum] = "66b35b06f6b41b56f8606ddf3ed3e5660b0971d1e309d903457c4056a0b9eff0"
S = "${UNPACKDIR}/kata/opt/kata"
SOURCE_DATE_EPOCH = "1792454400"
COMPATIBLE_HOST = "x86_64.*-linux"
RDEPENDS:${PN} += "fractal-kata-runtime fractal-cloud-hypervisor fractal-virtiofsd"
do_configure[noexec] = "1"
do_compile[noexec] = "1"
do_install() {
    install -d ${D}/opt/kata/bin ${D}/opt/kata/libexec \
        ${D}/opt/kata/share/kata-containers ${D}/opt/kata/share/defaults/kata-containers
    install -m 0644 ${S}/VERSION ${S}/versions.yaml ${D}/opt/kata/
    install -m 0644 ${S}/share/kata-containers/vmlinux-6.18.56-208 \
        ${D}/opt/kata/share/kata-containers/vmlinux.container
    install -m 0644 ${S}/share/kata-containers/kata-cbl-mariner-3.0-mariner.image \
        ${D}/opt/kata/share/kata-containers/kata-containers.img
    install -m 0644 ${S}/share/defaults/kata-containers/configuration-clh.toml \
        ${D}/opt/kata/share/defaults/kata-containers/
}
FILES:${PN} = "/opt/kata"
