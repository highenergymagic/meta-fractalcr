# The supervisor and storage CLI use the native tools, not Python bindings.
# Upstream otherwise adds python3-core to the main btrfs-tools package.
PACKAGECONFIG:remove = "python"
