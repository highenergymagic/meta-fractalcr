SUMMARY = "FractalCR appliance management service"
LICENSE = "MIT & Apache-2.0 & BSD-3-Clause & BSD-2-Clause & ISC & MPL-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://source"
S = "${UNPACKDIR}/source"
SOURCE_DATE_EPOCH = "1792454400"
DEPENDS = "go-native"
do_compile() {
    export GOOS=linux GOARCH=amd64 CGO_ENABLED=0
    export GOTOOLCHAIN=local GOPROXY=off GOSUMDB=off
    # Source unpack and rm_work remove B. Keep Go's content-addressed cache in
    # the build cache so a CLI edit does not recompile every vendored client.
    export GOCACHE="${TOPDIR}/cache/fractal-management-go" GOPATH="${B}/go-path"
    cd ${S}
    go test -mod=vendor ./internal/cli ./internal/config ./internal/appliance ./internal/nativeguard ./pkg/... ./cmd/fractal-workload
    go build -mod=vendor -trimpath -buildvcs=false -ldflags="-buildid=" \
        -o ${B}/fractal-management ./cmd/fractal-management
    go build -mod=vendor -trimpath -buildvcs=false -ldflags="-buildid= -X github.com/docker/buildx/version.Version=v0.38.1 -X github.com/docker/compose/v5/internal.Version=v5.6.1 -X github.com/moby/buildkit/version.Version=v0.34.1" \
        -o ${B}/fractal-workload ./cmd/fractal-workload
    ${B}/fractal-workload catalog > ${B}/workload-catalog.json
    ${B}/fractal-workload build-catalog > ${B}/buildkit-catalog.json
    cmp internal/cli/workload-catalog.json ${B}/workload-catalog.json
    cmp internal/cli/buildkit-catalog.json ${B}/buildkit-catalog.json
}
do_install() {
    install -d ${D}${libexecdir}
    install -m 0755 ${B}/fractal-management ${D}${libexecdir}/fractal-management
    install -m 0755 ${B}/fractal-workload ${D}${libexecdir}/fractal-workload
}
FILES:${PN} = "${libexecdir}/fractal-management ${libexecdir}/fractal-workload"
