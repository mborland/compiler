# Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
# Exceptions.
# See https://edgcpp.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# This script is executed by build-edg-env before creation of the Docker
# image to build the llvm-cir image's dependencies.

from edgdocker import has_image_tagged, tag_dev_env_for_cir

def main() -> None:
  # exec_build_pipeline and build_options are implicitly available (and
  # provided by the calling build-edg-env instance).
  #
  # The Dockerfile starts FROM ghcr.io/edgcpp/dev-env:latest: give the (pinned)
  # dev-env image that tag and the CIR provenance tag first.
  print(f"dev-env for the CIR images: {tag_dev_env_for_cir()}")
  if not has_image_tagged('llvm-cir') or build_options.no_cache:
    exec_build_pipeline('llvm-cir', build_options)

main()
