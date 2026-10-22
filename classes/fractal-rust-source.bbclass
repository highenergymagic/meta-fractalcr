# Source builds use an exact compiler and the reviewed Cargo.lock/vendor tree.
# No registry access, implicit rustup download or dependency update is allowed.
inherit pkgconfig
# OE's base class supplies virtual/cross-cc, compilerlibs and libc.
DEPENDS += "fractal-rust-native"
COMPATIBLE_HOST = "x86_64.*-linux"
export CARGO_HOME = "${B}/cargo-home"
export CARGO_NET_OFFLINE = "true"
export CARGO_INCREMENTAL = "0"
export CARGO_PROFILE_RELEASE_STRIP = "false"
export CARGO_BUILD_JOBS = "2"
export CARGO_TARGET_DIR = "${B}/cargo-target"
export RUSTC = "${STAGING_LIBDIR_NATIVE}/fractal-rust/bin/rustc"
export RUSTDOC = "${STAGING_LIBDIR_NATIVE}/fractal-rust/bin/rustdoc"
export RUSTFLAGS = "--remap-path-prefix=${WORKDIR}=/usr/src/debug/${PN}/${PV} -C debuginfo=0 -C link-arg=-Wl,--build-id=none"
export PKG_CONFIG_ALLOW_CROSS = "1"
FRACTAL_CARGO = "${STAGING_LIBDIR_NATIVE}/fractal-rust/bin/cargo"
FRACTAL_CARGO_ARGS = ""

do_configure() {
    test "$(${RUSTC} --version | cut -d' ' -f2)" = "1.99.1" || bbfatal "Unexpected Rust compiler"
    install -d ${S}/.cargo ${CARGO_HOME}
    cp ${S}/Cargo-vendor.toml ${S}/.cargo/config.toml
    cat >> ${S}/.cargo/config.toml <<EOF

[target.x86_64-unknown-linux-gnu]
linker = "${B}/target-linker"
EOF
    cat > ${B}/target-linker <<'EOF'
#!/bin/sh
# Cargo uses this linker for build scripts too when host and target triples
# coincide. Keep native build helpers on the builder's libc/ELF interpreter.
output=
previous=
for argument in "$@"; do
    if test "$previous" = -o; then output="$argument"; break; fi
    previous="$argument"
done
case "$output" in
    ${CARGO_TARGET_DIR}/x86_64-unknown-linux-gnu/*)
        exec ${CC} "$@" ${LDFLAGS} ;;
    ${CARGO_TARGET_DIR}/release/*)
        exec ${BUILD_CC} "$@" ${BUILD_LDFLAGS} ;;
    *) echo "Unexpected Cargo linker output: $output" >&2; exit 1 ;;
esac
EOF
    chmod 0755 ${B}/target-linker
}

do_compile() {
    cd ${S}
    ${FRACTAL_CARGO} build --frozen --offline --release \
        --target x86_64-unknown-linux-gnu ${FRACTAL_CARGO_ARGS}
    ${FRACTAL_CARGO} tree --frozen --offline --target x86_64-unknown-linux-gnu \
        -e normal,build ${FRACTAL_CARGO_TREE_ARGS} > ${B}/dependencies.txt
    sed -i 's|${WORKDIR}|/usr/src/debug/${PN}/${PV}|g' ${B}/dependencies.txt
}
FRACTAL_CARGO_TREE_ARGS = ""

do_install:append() {
    install -d ${D}${datadir}/fractalcr/source-inventories/${PN}
    install -m 0644 ${S}/Cargo.lock ${B}/dependencies.txt ${D}${datadir}/fractalcr/source-inventories/${PN}/
}
FILES:${PN} += "${datadir}/fractalcr/source-inventories/${PN}"
