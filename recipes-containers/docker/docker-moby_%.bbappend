# The platform service mounts cgroup v2; do not install cgroup-v1 init glue.
RDEPENDS:${PN}:remove = "cgroup-lite"
RRECOMMENDS:${PN} = ""
# Production services are owned by FractalCR's supervisor, not SysV runlevels.
INITSCRIPT_PACKAGES = ""
do_install:append() {
    rm -f ${D}${sysconfdir}/init.d/docker.init
}
