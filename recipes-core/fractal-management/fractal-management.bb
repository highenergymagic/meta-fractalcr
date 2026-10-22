SUMMARY = "FractalCR appliance management service"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://source"
S = "${UNPACKDIR}/source"
SOURCE_DATE_EPOCH = "1792454400"
DEPENDS = "go-native"
do_compile() {
    export GOOS=linux GOARCH=amd64 CGO_ENABLED=0
    export GOTOOLCHAIN=local GOPROXY=off GOSUMDB=off
    export GOCACHE="${B}/go-cache" GOPATH="${B}/go-path"
    cd ${S}
    go test -mod=vendor ./internal/cli ./internal/config ./internal/appliance
    go build -mod=vendor -trimpath -buildvcs=false -ldflags="-buildid=" \
        -o ${B}/fractal-management ./cmd/fractal-management
}
do_install() {
    install -d ${D}${libexecdir}
    install -m 0755 ${B}/fractal-management ${D}${libexecdir}/fractal-management
}
FILES:${PN} = "${libexecdir}/fractal-management"
