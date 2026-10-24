SUMMARY = "FractalCR platform supervisor and container bring-up diagnostics"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://release.h file://platform.c file://console.c file://platform-info.h file://line-editor.c file://line-editor.h file://buildkit-runc.c file://virtiofsd.c file://syslog.c file://supervisor.c file://supervisor.h file://manager.c \
           file://graph.c file://graph.h file://control.c file://control.h file://logging.c file://probe.c file://build-id"
S = "${UNPACKDIR}"
SOURCE_DATE_EPOCH = "1792454400"
DEPENDS = "json-c util-linux"
RDEPENDS:${PN} = "docker-moby fractal-management fractal-storage"
inherit pkgconfig
do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -Wall -Wextra -Werror manager.c supervisor.c graph.c -o fractal-init
    ${CC} ${CFLAGS} ${LDFLAGS} -Wall -Wextra -Werror platform.c console.c line-editor.c syslog.c supervisor.c control.c logging.c -o fractal-platform \
        $(pkg-config --cflags --libs json-c blkid) -lutil
    ${CC} ${CFLAGS} -Wall -Wextra -Werror -static -Os probe.c -o probe
    ${CC} ${CFLAGS} ${LDFLAGS} -Wall -Wextra -Werror buildkit-runc.c -o fractal-buildkit-runc
    ${CC} ${CFLAGS} ${LDFLAGS} -Wall -Wextra -Werror virtiofsd.c -o fractal-virtiofsd
}
do_install() {
    install -d ${D}${base_sbindir} ${D}${libexecdir} ${D}${datadir}/fractal ${D}${sysconfdir}/fractal
    install -m 0755 fractal-init ${D}${base_sbindir}/fractal-init
    install -m 0755 fractal-platform ${D}${libexecdir}/fractal-platform
    install -m 0755 fractal-buildkit-runc ${D}${libexecdir}/fractal-buildkit-runc
    install -m 0755 fractal-virtiofsd ${D}${libexecdir}/fractal-virtiofsd
    ln -s sbin/fractal-init ${D}/init
    install -m 0755 probe ${D}${libexecdir}/fractal-probe
    install -d ${B}/probe-root/state ${B}/probe-root/proc ${B}/probe-root/sys ${B}/probe-root/dev
    install -m 0755 probe ${B}/probe-root/probe
    tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
        -C ${B}/probe-root -cf ${D}${datadir}/fractal/probe.tar .
    install -m 0644 release.h ${D}${sysconfdir}/fractal/release.h
    install -m 0644 build-id ${D}${sysconfdir}/fractal/build-id
}
FILES:${PN} += "/init ${libexecdir}/fractal-platform ${libexecdir}/fractal-buildkit-runc ${libexecdir}/fractal-virtiofsd ${libexecdir}/fractal-probe ${datadir}/fractal/probe.tar"
