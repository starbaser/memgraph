#!/bin/bash

function print_help() {
    echo "Usage: $0 <container_name> [--conan-remote <conan_remote>] [--version <version>]"
    exit 1
}

CONTAINER_NAME=$1
if [ -z "$CONTAINER_NAME" ]; then
    print_help
fi
shift 1
CONAN_REMOTE=""
VERSION="3.5.4"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --conan-remote)
            if [[ $# -lt 2 || "$2" == --* ]]; then
                echo "Error: --conan-remote requires a value"
                exit 1
            fi
            CONAN_REMOTE=$2
            shift 2
        ;;
        --version)
            if [[ $# -lt 2 || "$2" == --* ]]; then
                echo "Error: --version requires a value"
                exit 1
            fi
            VERSION=$2
            shift 2
        ;;
        *)
            print_help
            exit 1
        ;;
    esac
done

# check if memgraph directory exists in the container
exists=$(docker exec -u mg "$CONTAINER_NAME" bash -c "if [ -d /home/mg/memgraph ]; then echo 'true'; else echo 'false'; fi")
if [ "$exists" = "false" ]; then
    echo "Copying memgraph directory to the container"
    docker cp . "$CONTAINER_NAME:/home/mg/memgraph"
    docker exec -u root "$CONTAINER_NAME" bash -c "chown -R mg:mg /home/mg/memgraph"
fi

build_args=(--version "$VERSION")
if [[ -n "$CONAN_REMOTE" ]]; then
    build_args+=(--conan-remote "$CONAN_REMOTE")
fi

printf -v build_command " %q" "${build_args[@]}"
docker exec -u mg "$CONTAINER_NAME" bash -c "cd /home/mg/memgraph && ./tools/ci/openssl/build.sh${build_command}"
docker exec -u mg "$CONTAINER_NAME" bash -c "cd /home/mg/memgraph && ./tools/ci/openssl/build-openssl-deb.sh $VERSION"
docker exec -u mg "$CONTAINER_NAME" bash -c "cd /home/mg/memgraph && ./tools/ci/openssl/build-libssl3-deb.sh $VERSION"

ARCH="$(dpkg --print-architecture)"
docker cp "$CONTAINER_NAME:/home/mg/memgraph/build/openssl_${VERSION}-0ubuntu0custom1_${ARCH}.deb" build/openssl_${VERSION}-0ubuntu0custom1_${ARCH}.deb
docker cp "$CONTAINER_NAME:/home/mg/memgraph/build/libssl3t64_${VERSION}-0ubuntu0custom1_${ARCH}.deb" build/libssl3t64_${VERSION}-0ubuntu0custom1_${ARCH}.deb
