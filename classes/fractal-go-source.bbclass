# Source-built x86-64 Go tools. All modules must already be in SRC_URI/vendor.
DEPENDS += "go-native"
COMPATIBLE_HOST = "x86_64.*-linux"
export GOOS = "linux"
export GOARCH = "amd64"
export CGO_ENABLED = "0"
export GOTOOLCHAIN = "local"
export GOPROXY = "off"
export GOSUMDB = "off"
export GOFLAGS = "-mod=vendor -trimpath -buildvcs=false"
export GOWORK = "off"
export GOCACHE = "${B}/go-cache"
export GOPATH = "${B}/go-path"
do_configure[noexec] = "1"
do_compile:prepend() {
    test "$(go env GOVERSION)" = "go1.26.10" || bbfatal "Unexpected Go compiler for pinned source build"
}
