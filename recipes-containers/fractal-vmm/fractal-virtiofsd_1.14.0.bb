SUMMARY = "Virtiofsd built from pinned source and reviewed Rust dependencies"
HOMEPAGE = "https://gitlab.com/virtio-fs/virtiofsd"
CVE_PRODUCT = "virtiofsd"
LICENSE = "Apache-2.0 & BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE-APACHE;md5=3b83ef96387f14655fc854ddc3c6bd57 \
                   file://LICENSE-BSD-3-Clause;md5=b1ed361f9fc790c1054d81a7ef041a34"
SRC_URI = "git://gitlab.com/virtio-fs/virtiofsd.git;protocol=https;nobranch=1;destsuffix=virtiofsd \
           file://virtiofsd-vendor.tar.gz;subdir=virtiofsd \
           file://virtiofsd-security.patch.gz"
SRCREV = "c2540f8db14caba81c1e37fba23fc7bf2cd7f0dd"
S = "${UNPACKDIR}/virtiofsd"
inherit fractal-rust-source
DEPENDS += "libcap-ng libseccomp"
export LIBSECCOMP_LIB_PATH = "${STAGING_LIBDIR}"
export LIBCAPNG_LIB_PATH = "${STAGING_LIBDIR}"
FRACTAL_CARGO_ARGS = "--bin virtiofsd"

do_install() {
    install -d ${D}/opt/kata/libexec
    install -m 0755 ${CARGO_TARGET_DIR}/x86_64-unknown-linux-gnu/release/virtiofsd ${D}/opt/kata/libexec/
}
FILES:${PN} += "/opt/kata/libexec/virtiofsd"
