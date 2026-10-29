#!/usr/bin/env bash
# Build Fledge and the plugin in Docker and run the unit tests.
# Usage: .ci/deb/test.sh [base image] [platform]
set -euo pipefail

BASE_IMAGE="${1:-ubuntu:24.04}"
PLATFORM="${2:-linux/amd64}"

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

docker build \
    --file .ci/deb/Dockerfile \
    --target test \
    --platform "$PLATFORM" \
    --build-arg BASE_IMAGE="$BASE_IMAGE" \
    --progress=plain \
    .
