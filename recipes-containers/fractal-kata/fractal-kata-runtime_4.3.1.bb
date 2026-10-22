SUMMARY = "Source-built Kata Go runtime and shim with frozen security dependencies"
HOMEPAGE = "https://github.com/kata-containers/kata-containers"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"
SRC_URI = "git://github.com/kata-containers/kata-containers;protocol=https;nobranch=1;destsuffix=kata-source \
           file://kata-vendor.tar.gz;subdir=kata-source/src/runtime"
SRCREV = "03cf2ec06a5d302f23af811744f6bcea49988bfb"
S = "${UNPACKDIR}/kata-source"
inherit fractal-go-source

do_compile() {
    # Go's module/toolchain checks and this layer's exact compiler pin replace
    # upstream's yq installer, which would download a tool during compilation.
    oe_runmake -C ${S}/src/runtime PREFIX=/opt/kata STATIC=yes SKIP_GO_VERSION_CHECK=yes \
        KATA_LDFLAGS="-buildid=" runtime containerd-shim-v2
}

do_install() {
    install -d ${D}/opt/kata/bin
    install -m 0755 ${S}/src/runtime/kata-runtime ${D}/opt/kata/bin/
    install -m 0755 ${S}/src/runtime/containerd-shim-kata-v2 ${D}/opt/kata/bin/
}
FILES:${PN} = "/opt/kata/bin/kata-runtime /opt/kata/bin/containerd-shim-kata-v2"
