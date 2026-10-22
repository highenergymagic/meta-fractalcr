#!/bin/sh
set -eu
echo "Testing platform and Docker with the embedded offline probe image..."
test "$(docker info --format '{{.Driver}}|{{.CgroupVersion}}')" = 'overlay2|2'
test "$(docker info --format '{{.SwapLimit}}')" = true
if touch /.fractal-root-write-test 2>/dev/null; then
    rm -f /.fractal-root-write-test
    echo "System root is unexpectedly writable" >&2
    exit 1
fi
touch /run/fractal-write-test
rm /run/fractal-write-test
echo HOST_ROOT_READONLY_OK
hash=$(sha256sum /usr/share/fractal/probe.tar)
hash=${hash%% *}
image="fractal/probe:$hash"
if ! docker image inspect "$image" >/dev/null 2>&1; then
    docker import --change 'ENTRYPOINT ["/probe"]' /usr/share/fractal/probe.tar "$image"
fi
docker run --rm --runtime runc --network none --read-only --cap-drop ALL \
    --security-opt no-new-privileges --memory 32m --memory-swap 48m --pids-limit 32 \
    "$image" swap-limit

docker run --rm --network none --read-only --cap-drop ALL \
    --security-opt no-new-privileges --memory 32m --pids-limit 32 \
    --mount type=bind,source=/datastore/data0/selftest,target=/state \
    "$image"
docker run --rm --network bridge --read-only --cap-drop ALL \
    --security-opt no-new-privileges --memory 32m --pids-limit 32 \
    "$image" network
server=""
cleanup() {
    if test -n "$server"; then docker rm -f "$server" >/dev/null 2>&1 || true; fi
}
trap cleanup EXIT HUP INT TERM
server=$(docker run -d --read-only --cap-drop ALL \
    --security-opt no-new-privileges --memory 32m --pids-limit 32 \
    --publish 127.0.0.1:18080:8080 "$image" serve)
/usr/libexec/fractal-probe request
echo "Docker storage, isolation, persistent bind mount, bridge and port publishing passed."
