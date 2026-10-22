SUMMARY = "Checksum-pinned upstream Rust build tools for appliance VMM sources"
HOMEPAGE = "https://www.rust-lang.org/"
LICENSE = "MIT & Apache-2.0"
LIC_FILES_CHKSUM = "file://rustc-${PV}-x86_64-unknown-linux-gnu/LICENSE-MIT;md5=8a95ea02f2dd8c2953432d0ddd7cdd91 \
                   file://rustc-${PV}-x86_64-unknown-linux-gnu/LICENSE-APACHE;md5=22a53954e4e0ec258dfce4391e905dac"
inherit native
COMPATIBLE_HOST = "x86_64.*-linux"
SRC_URI = "https://static.rust-lang.org/dist/2026-10-15/rustc-${PV}-x86_64-unknown-linux-gnu.tar.xz;name=compiler \
           https://static.rust-lang.org/dist/2026-10-15/cargo-${PV}-x86_64-unknown-linux-gnu.tar.xz;name=cargo \
           https://static.rust-lang.org/dist/2026-10-15/rust-std-${PV}-x86_64-unknown-linux-gnu.tar.xz;name=stdlib"
SRC_URI[compiler.sha256sum] = "2ff05ff9c08a90861e7f6e4065b40a8b87463b4576186fd613871c3e37f2091f"
SRC_URI[cargo.sha256sum] = "53f3521949404d595efb06a55c12c5658d4ed1c75566f33d3d514eaebe70bd8a"
SRC_URI[stdlib.sha256sum] = "109b074225bf5b9ea3820890876bfdf4ed161adbfaf94b2b4b5984c85ee5d958"
S = "${UNPACKDIR}"
do_configure[noexec] = "1"
do_compile[noexec] = "1"
INHIBIT_SYSROOT_STRIP = "1"

do_install() {
    for component in rustc cargo rust-std; do
        sh ${S}/$component-${PV}-x86_64-unknown-linux-gnu/install.sh \
            --prefix=${D}${libdir}/fractal-rust --disable-ldconfig
    done
}
