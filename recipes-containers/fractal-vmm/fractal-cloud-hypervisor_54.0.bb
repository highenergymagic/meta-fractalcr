SUMMARY = "Cloud Hypervisor built from pinned source and reviewed Rust dependencies"
HOMEPAGE = "https://github.com/cloud-hypervisor/cloud-hypervisor"
CVE_PRODUCT = "cloud-hypervisor cloud_hypervisor"
LICENSE = "Apache-2.0 & BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSES/Apache-2.0.txt;md5=3b83ef96387f14655fc854ddc3c6bd57 \
                   file://LICENSES/BSD-3-Clause.txt;md5=b1ed361f9fc790c1054d81a7ef041a34"
SRC_URI = "git://github.com/cloud-hypervisor/cloud-hypervisor;protocol=https;nobranch=1;destsuffix=cloud-hypervisor \
           file://cloud-hypervisor-vendor.tar.gz;subdir=cloud-hypervisor \
           file://cloud-hypervisor-security.patch.gz \
           file://deterministic-version.patch"
SRCREV = "038ce5a72d761380a02f714a4eb4ca63e0ea073d"
S = "${UNPACKDIR}/cloud-hypervisor"
inherit fractal-rust-source
export CH_EXTRA_VERSION = "fractalcr-1"
# The appliance exposes KVM through Kata. D-Bus is not part of this build.
FRACTAL_CARGO_ARGS = "-p cloud-hypervisor --bin cloud-hypervisor --no-default-features --features kvm,io_uring"
FRACTAL_CARGO_TREE_ARGS = "-p cloud-hypervisor --no-default-features --features kvm,io_uring"

do_install() {
    install -d ${D}/opt/kata/bin
    install -m 0755 ${CARGO_TARGET_DIR}/x86_64-unknown-linux-gnu/release/cloud-hypervisor ${D}/opt/kata/bin/
}
FILES:${PN} += "/opt/kata/bin/cloud-hypervisor"
