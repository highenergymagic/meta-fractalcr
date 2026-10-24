# Native formatter service; no Python bindings or runtime formatter utility.
PACKAGECONFIG:remove = "python"
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append:class-target = " file://fractal-storage.c file://fractal-storage.mk"

do_compile:append:class-target() {
    cp ${UNPACKDIR}/fractal-storage.c ${S}/fractal-storage.c
    cp ${UNPACKDIR}/fractal-storage.mk ${S}/fractal-storage.mk
    oe_runmake -f Makefile -f fractal-storage.mk fractal-storage
}
do_install:append:class-target() {
    install -d ${D}${libexecdir}
    install -m 0755 ${B}/fractal-storage ${D}${libexecdir}/fractal-storage
}
PACKAGES =+ "fractal-storage"
FILES:fractal-storage = "${libexecdir}/fractal-storage"
