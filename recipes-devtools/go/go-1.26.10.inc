# Keep the pinned Wrynose compiler integration and its upstream patches.
require recipes-devtools/go/go-1.26.9.inc
FILESEXTRAPATHS:prepend := "${COREBASE}/meta/recipes-devtools/go/go:"
SRC_URI[main.sha256sum] = "fd63e883ba2f2bfca93c1646b31ebef63e2b1e4d2b317c9789d976a68d81f36f"
