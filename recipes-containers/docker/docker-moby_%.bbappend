# The platform service mounts cgroup v2; do not install cgroup-v1 init glue.
SRCREV_moby = "0367cbeb49b9d98cb0b4f51e95f7b54b4803e76c"
SRCREV_cli = "a82e528901260c8df6073311379eac65f92db204"
DOCKER_VERSION = "v29.9.2"
SRC_URI:remove = "file://CVE-2026-41568.patch;patchdir=src/import \
                  file://CVE-2026-42306_p1.patch;patchdir=src/import \
                  file://CVE-2026-42306_p2.patch;patchdir=src/import"
RDEPENDS:${PN}:remove = "cgroup-lite"
RRECOMMENDS:${PN} = ""
# Production services are owned by FractalCR's supervisor, not SysV runlevels.
INITSCRIPT_PACKAGES = ""

do_compile() {
    export GOARCH="${TARGET_GOARCH}" CGO_ENABLED=1
    export GOROOT="${STAGING_DIR_NATIVE}/${nonarch_libdir}/${HOST_SYS}/go"
    export CGO_CFLAGS="${CFLAGS}" CGO_LDFLAGS="${LDFLAGS}"
    export DOCKER_BUILDTAGS='${BUILD_TAGS} ${PACKAGECONFIG_CONFARGS}'
    export GO111MODULE=on GOTOOLCHAIN=local GOPROXY=off GOSUMDB=off GOWORK=off
    export GOFLAGS="-mod=vendor -trimpath -buildvcs=false"
    export DISABLE_WARN_OUTSIDE_CONTAINER=1 BUILDFLAGS="-trimpath"
    cd ${S}/src/import
    VERSION="${DOCKER_VERSION}" DOCKER_GITCOMMIT="${SRCREV_moby}" ./hack/make.sh dynbinary
    cd ${S}/cli
    # Upstream keeps its locked module graph as vendor.mod/vendor.sum.
    cp vendor.mod go.mod
    cp vendor.sum go.sum
    export CFLAGS="" LDFLAGS="" DOCKER_VERSION="${DOCKER_VERSION}"
    VERSION="${DOCKER_VERSION}" DOCKER_GITCOMMIT="${SRCREV_cli}" oe_runmake GO111MODULE=on dynbinary
}
do_install:append() {
    rm -f ${D}${sysconfdir}/init.d/docker.init
}

# The appliance uses linked API clients, not the separately installed CLI.
DOCKER_UNIFIED_PACKAGE = "False"
