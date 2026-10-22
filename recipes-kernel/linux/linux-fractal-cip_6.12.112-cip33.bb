SUMMARY = "CIP SLTS kernel for the FractalCR embedded-root appliance"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=6bc538ed5bd9a7fc9398086aedcd7e46"
inherit kernel
SOURCE_DATE_EPOCH = "1792454400"
SRC_URI = "https://cdn.kernel.org/pub/linux/kernel/projects/cip/6.12/linux-cip-${PV}.tar.xz \
           file://defconfig file://firmware.crt"
SRC_URI[sha256sum] = "522caf8dd5a07904705691ba07440d8a557d74eab39a938b95b0c69ad27eed3a"
S = "${UNPACKDIR}/linux-cip-${PV}"
COMPATIBLE_MACHINE = "fractal-x86-64"
KERNEL_VERSION_SANITY_SKIP = "1"
KERNEL_LOCALVERSION = "-fractalcr"
KERNEL_FEATURES = ""
KERNEL_EXTRA_ARGS += "LOADADDR="
do_configure:prepend() {
    # Kbuild resolves this prerequisite in its build directory. Keep the
    # public certificate path relative so the packaged config is relocatable.
    install -m 0644 ${UNPACKDIR}/firmware.crt ${B}/fractal-firmware.crt
    cp ${UNPACKDIR}/defconfig ${B}/.config
    ${S}/scripts/config --file ${B}/.config \
        --set-str SYSTEM_TRUSTED_KEYS "fractal-firmware.crt" \
        --set-str LOCALVERSION ""
}
do_configure:append() {
    for feature in CC_OPTIMIZE_FOR_PERFORMANCE CGROUPS CGROUP_BPF MEMCG SWAP BINFMT_MISC NAMESPACES USER_NS SECCOMP_FILTER \
        OVERLAY_FS BTRFS_FS VETH BRIDGE NETFILTER EFI_STUB KVM VHOST_VSOCK \
        TUN NET_SCH_INGRESS NET_CLS_U32 NET_ACT_MIRRED MACVTAP VXLAN IP_VS \
        IP_VS_RR IP_VS_PROTO_TCP IP_VS_PROTO_UDP XFRM_USER INET_ESP NETFILTER_XT_MARK NETFILTER_XT_MATCH_BPF \
        IP6_NF_FILTER IP6_NF_NAT IP6_NF_MANGLE IP6_NF_RAW IP6_NF_TARGET_MASQUERADE; do
        grep -qx "CONFIG_$feature=y" ${B}/.config || bbfatal "Missing kernel feature: $feature"
    done
}
