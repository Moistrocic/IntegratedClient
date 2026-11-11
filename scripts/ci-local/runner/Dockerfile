# Local GitHub-Ubuntu-runner approximation for scripts/ci-local.
#
# The community act image intentionally contains only the bare minimum needed to
# bootstrap actions. GitHub-hosted Ubuntu runners additionally preinstall tools
# that some workflows assume without installing them (Rust/rustup, CMake, Ninja,
# clang, pip). This image adds those on top of the act image so the real
# workflow steps can run unchanged.
#
# Build (the helper script does this for you):
#   docker build -f scripts/ci-local/runner/Dockerfile \
#     --build-arg BASE_IMAGE=catthehacker/ubuntu:act-latest \
#     -t ic-ci-runner:latest scripts/ci-local/runner
#
# Use the matching base for the other Ubuntu versions:
#   catthehacker/ubuntu:act-22.04 -> ic-ci-runner:22.04
#   catthehacker/ubuntu:act-24.04 -> ic-ci-runner:24.04

ARG BASE_IMAGE=catthehacker/ubuntu:act-latest
FROM ${BASE_IMAGE}

ARG DEBIAN_FRONTEND=noninteractive

# Tools that GitHub-hosted runners preinstall and workflows use directly.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        cmake \
        ninja-build \
        clang \
        lld \
        python3-pip \
        sudo \
    && rm -rf /var/lib/apt/lists/*

# Rust stable with the default profile (rustc/cargo/rustfmt/clippy), like the
# GitHub-hosted Ubuntu runner image.
ENV RUSTUP_HOME=/usr/local/rustup \
    CARGO_HOME=/usr/local/cargo \
    PATH=/usr/local/cargo/bin:${PATH}
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
        | sh -s -- -y --profile default --default-toolchain stable \
    && rustc --version \
    && cargo --version \
    && rustfmt --version \
    && cargo clippy --version
