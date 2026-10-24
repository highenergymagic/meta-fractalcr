SUMMARY = "Reproducible sparse ext4 seed for block-backed container snapshots"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://pack-sparse.py"
S = "${UNPACKDIR}"
DEPENDS = "e2fsprogs-native python3-native"
inherit python3native
SOURCE_DATE_EPOCH = "1302044400"

do_compile() {
    truncate -s 8G ${B}/scratch.ext4
    E2FSPROGS_FAKE_TIME=${SOURCE_DATE_EPOCH} mkfs.ext4 -q -F -U 3129b877-67d6-4a6a-b051-ae2fd00204a1 \
        -E lazy_itable_init=0,lazy_journal_init=0,hash_seed=3129b877-67d6-4a6a-b051-ae2fd00204a1,root_owner=0:0 ${B}/scratch.ext4
    ${PYTHON} ${UNPACKDIR}/pack-sparse.py ${B}/scratch.ext4 ${B}/scratch.fcr.gz
}
do_install() {
    install -d ${D}${datadir}/fractal
    install -m 0444 ${B}/scratch.fcr.gz ${D}${datadir}/fractal/blockfile-seed.gz
}
FILES:${PN} = "${datadir}/fractal/blockfile-seed.gz"
