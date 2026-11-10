#!/bin/bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"

docker build -t gemfield/zlmediakit:20.04-runtime-ubuntu18.04 -f "${script_dir}/ubuntu18.04/Dockerfile.runtime" "${repo_root}"
docker build -t gemfield/zlmediakit:20.04-devel-ubuntu18.04 -f "${script_dir}/ubuntu18.04/Dockerfile.devel" "${repo_root}"
docker build -t gemfield/zlmediakit:20.04-runtime-ubuntu16.04 -f "${script_dir}/ubuntu16.04/Dockerfile.runtime" "${repo_root}"
docker build -t gemfield/zlmediakit:20.04-devel-ubuntu16.04 -f "${script_dir}/ubuntu16.04/Dockerfile.devel" "${repo_root}"
docker build -t gemfield/zlmediakit:centos7-runtime -f "${script_dir}/centos7/Dockerfile.runtime" "${repo_root}"
