SUMMARY = "Pinned Kata Go shim, Cloud Hypervisor and Mariner guest"
HOMEPAGE = "https://github.com/kata-containers/kata-containers"
# Upstream release binaries are used as immutable build inputs. Their source
# versions are retained in /opt/kata/versions.yaml; guest notices live in its FS.
LICENSE = "Apache-2.0 & GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10 \
                   file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"
SRC_URI = "https://github.com/kata-containers/kata-containers/releases/download/${PV}/kata-go-static-${PV}-amd64.tar.zst;subdir=kata"
SRC_URI[sha256sum] = "7dda31ca54b397cbf8165f620d6041872d3c45b7a77383ce0e86f76a06e103d0"
S = "${UNPACKDIR}/kata/opt/kata"
SOURCE_DATE_EPOCH = "1792454400"
COMPATIBLE_HOST = "x86_64.*-linux"
do_configure[noexec] = "1"
do_compile[noexec] = "1"
# Cloud Hypervisor is stripped in the upstream release, not by this recipe.
INSANE_SKIP:${PN} += "already-stripped"
do_install() {
    install -d ${D}/opt/kata/bin ${D}/opt/kata/libexec \
        ${D}/opt/kata/share/kata-containers ${D}/opt/kata/share/defaults/kata-containers
    for program in containerd-shim-kata-v2 cloud-hypervisor kata-runtime; do
        install -m 0755 ${S}/bin/$program ${D}/opt/kata/bin/
    done
    install -m 0755 ${S}/libexec/virtiofsd ${D}/opt/kata/libexec/
    install -m 0644 ${S}/VERSION ${S}/versions.yaml ${D}/opt/kata/
    install -m 0644 ${S}/share/kata-containers/vmlinux-6.18.35-202 \
        ${D}/opt/kata/share/kata-containers/vmlinux.container
    install -m 0644 ${S}/share/kata-containers/kata-cbl-mariner-3.0-mariner.image \
        ${D}/opt/kata/share/kata-containers/kata-containers.img
    install -m 0644 ${S}/share/defaults/kata-containers/configuration-clh.toml \
        ${D}/opt/kata/share/defaults/kata-containers/
}
FILES:${PN} = "/opt/kata"
