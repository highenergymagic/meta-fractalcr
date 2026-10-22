SUMMARY = "CIP SLTS kernel for the FractalCR embedded-root appliance"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=6bc538ed5bd9a7fc9398086aedcd7e46"
inherit kernel
CVE_PRODUCT = "linux:linux_kernel"
# CIP's release suffix is not an upstream kernel version in advisory feeds.
# Retain the full source version in SPDX and use its stable base for matching.
CVE_VERSION = "${@d.getVar('PV').split('-cip')[0]}"
SOURCE_DATE_EPOCH = "1792454400"
SRC_URI = "https://cdn.kernel.org/pub/linux/kernel/projects/cip/6.12/linux-cip-${PV}.tar.xz \
           file://defconfig file://firmware.crt \
           file://patch-6.12.112-113.patch.gz \
           file://patch-6.12.113-114.patch.gz \
           file://kernel-security.json"
SRC_URI[sha256sum] = "522caf8dd5a07904705691ba07440d8a557d74eab39a938b95b0c69ad27eed3a"
S = "${UNPACKDIR}/linux-cip-${PV}"
COMPATIBLE_MACHINE = "fractal-x86-64"
KERNEL_VERSION_SANITY_SKIP = "1"
KERNEL_LOCALVERSION = "-security1-fractalcr"
KERNEL_FEATURES = ""
KERNEL_EXTRA_ARGS += "LOADADDR="
ERROR_QA:append = " patch-fuzz"
FCR_KERNEL_SECURITY_DIR := "${THISDIR}/files"

python do_unpack:append() {
    import gzip, hashlib, json
    from pathlib import Path
    root = Path(d.getVar('UNPACKDIR'))
    manifest = json.loads((root/'kernel-security.json').read_text())
    if manifest['base'] != d.getVar('PV'):
        bb.fatal('Kernel security updates do not match CIP source version')
    for entry in manifest['patches']:
        if Path(entry['file']).name != entry['file']:
            bb.fatal('Invalid kernel security patch filename')
        # OE decompresses local .gz inputs during unpack. Verify the exact
        # decompressed patch against the original layer file's authenticated hash.
        packed = Path(d.getVar('FCR_KERNEL_SECURITY_DIR'))/entry['file']
        data = packed.read_bytes()
        if hashlib.sha256(data).hexdigest() != entry['sha256']:
            bb.fatal('Kernel security patch checksum mismatch')
        if (root/entry['file'].removesuffix('.gz')).read_bytes() != gzip.decompress(data):
            bb.fatal('Unpacked kernel security patch differs from reviewed input')
}
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
    grep -qx 'CONFIG_X86_64=y' ${B}/.config || bbfatal "Security backport exclusions require x86-64"
    for feature in CC_OPTIMIZE_FOR_PERFORMANCE CGROUPS CGROUP_BPF MEMCG SWAP BINFMT_MISC NAMESPACES USER_NS SECCOMP_FILTER \
        OVERLAY_FS BTRFS_FS VETH BRIDGE NETFILTER EFI_STUB KVM VHOST_VSOCK \
        TUN NET_SCH_INGRESS NET_CLS_U32 NET_ACT_MIRRED MACVTAP VXLAN IP_VS \
        IP_VS_RR IP_VS_PROTO_TCP IP_VS_PROTO_UDP XFRM_USER INET_ESP NETFILTER_XT_MARK NETFILTER_XT_MATCH_BPF \
        IP6_NF_FILTER IP6_NF_NAT IP6_NF_MANGLE IP6_NF_RAW IP6_NF_TARGET_MASQUERADE \
        SLAB_FREELIST_RANDOM SLAB_FREELIST_HARDENED HARDENED_USERCOPY FORTIFY_SOURCE \
        INIT_ON_ALLOC_DEFAULT_ON LIST_HARDENED SECURITY_DMESG_RESTRICT BPF_UNPRIV_DEFAULT_OFF; do
        grep -qx "CONFIG_$feature=y" ${B}/.config || bbfatal "Missing kernel feature: $feature"
    done
}
