# Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
# Exceptions.
# See https://edgcpp.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# This script is executed by build-edg-env before creation of the Docker
# image.  It populates ./llvm-cir (the install prefix COPYed by the
# Dockerfile) by running dev_tools/bin/edg-build-llvm-cir inside
# edgcpp/dev-env with bind mounts.
#
# Environment:
#   EDG_LLVM_CIR_WORKDIR      Persistent work area holding llvm-project/,
#                             build/ and install/ (default
#                             ~/.cache/edg/llvm-cir).  Reusing it makes
#                             rebuilds incremental.
#   EDG_LLVM_CIR_INSTALL_DIR  Use an existing installed prefix as-is and skip
#                             the build.
#   EDG_LLVM_CIR_CHECK        If set, also run ninja check-clang-cir.
#   EDG_LLVM_CIR_JOBS         Parallel compile jobs (default: all CPUs, which
#                             is too many on a shared host such as clang4;
#                             the project limit there is 64).
#
# The build and the strip containers always run under `nice -n 10` (the
# project hosts are shared), with or without EDG_LLVM_CIR_JOBS.

import os
import shutil
import subprocess

import edgtools

from edgdocker import tag_dev_env_for_cir
from pathlib import Path

LLVM_GIT_URL = 'https://github.com/llvm/llvm-project.git'
NICE = ['nice', '-n', '10']
REPO_DIR = edgtools.get_tools_dir().parent
LLVM_REVISION_FILE = REPO_DIR / 'cir_src' / 'LLVM_REVISION'
BUILD_SCRIPT = edgtools.get_tools_dir() / 'bin' / 'edg-build-llvm-cir'

def llvm_revision() -> str:
  '''Return the pinned llvm-project commit SHA.'''
  return LLVM_REVISION_FILE.read_text().strip()

def fetch_llvm_project(dest: Path) -> None:
  '''Shallow-fetch llvm-project at the pinned commit into ``dest``.'''
  commit = llvm_revision()
  dest.mkdir(parents = True, exist_ok = True)
  if not (dest / '.git').exists():
    subprocess.run(['git', 'init'], cwd = dest, check = True)
    subprocess.run(
      ['git', 'remote', 'add', 'origin', LLVM_GIT_URL],
      cwd = dest,
      check = True
    )
  head = subprocess.run(
    ['git', 'rev-parse', 'HEAD'],
    cwd = dest,
    stdout = subprocess.PIPE,
    stderr = subprocess.DEVNULL,
    text = True
  )
  if head.returncode == 0 and head.stdout.strip() == commit:
    return
  subprocess.run(
    ['git', 'fetch', '--depth', '1', 'origin', commit],
    cwd = dest,
    check = True
  )
  subprocess.run(['git', 'checkout', 'FETCH_HEAD'], cwd = dest, check = True)

def build_toolchain(work_dir: Path, install_dir: Path, image: str) -> None:
  '''Build and install the toolchain inside the dev-env image `image`.'''
  src_dir = work_dir / 'llvm-project'
  build_dir = work_dir / 'build'
  fetch_llvm_project(src_dir)
  build_dir.mkdir(exist_ok = True)
  install_dir.mkdir(exist_ok = True)
  jobs = os.environ.get('EDG_LLVM_CIR_JOBS')
  command = [
    'docker', 'run', '--rm', '--name', f"cir-config-llvm-build-{os.getpid()}",
    '--user', f"{os.getuid()}:{os.getgid()}",
    '-e', 'HOME=/tmp',
    '-v', f"{src_dir}:/src/llvm-project:ro",
    '-v', f"{build_dir}:/build",
    '-v', f"{install_dir}:/edg/llvm-cir",
    '-v', f"{BUILD_SCRIPT}:/usr/local/bin/edg-build-llvm-cir:ro",
    image,
    *NICE,
    'edg-build-llvm-cir'
  ]
  if jobs:
    command.extend(['--jobs', jobs])
  if 'EDG_LLVM_CIR_CHECK' in os.environ:
    command.append('--check')
  command.extend(['/src/llvm-project', '/build', '/edg/llvm-cir'])
  subprocess.run(command, check = True)

def strip_executables(prefix: Path, image: str) -> None:
  '''Strip debug info from the ELF executables in <prefix>/bin.

  Every tool is statically linked with line tables, so bin/ is ~12 GB.  The
  files must stay (the installed *Exports*.cmake/*Targets*.cmake files check
  that every imported target exists, so find_package(LLVM/Clang/MLIR) fails
  if any is removed); libraries keep their line tables.
  '''
  # strip leaves non-ELF files (scripts) alone and fails on them; ignore that.
  script = (
    'for f in /prefix/bin/*; do '
    '[ -f "$f" ] && [ ! -L "$f" ] && strip --strip-debug "$f" 2>/dev/null; '
    'done; true'
  )
  subprocess.run(
    [
      'docker', 'run', '--rm', '--name',
      f"cir-config-llvm-strip-{os.getpid()}",
      '--user', f"{os.getuid()}:{os.getgid()}",
      '-v', f"{prefix}:/prefix",
      image,
      *NICE,
      'sh', '-c', script
    ],
    check = True
  )

def stage_prefix() -> None:
  '''Copy the installed prefix into ./llvm-cir for the Dockerfile.'''
  # The build and the strip run in the CIR provenance tag of dev-env (which
  # does not move), whatever :latest is on this shared host.
  image = tag_dev_env_for_cir()
  if 'EDG_LLVM_CIR_INSTALL_DIR' in os.environ:
    install_dir = Path(os.environ['EDG_LLVM_CIR_INSTALL_DIR'])
  else:
    work_dir = Path(
      os.environ.get('EDG_LLVM_CIR_WORKDIR',
                     Path.home() / '.cache' / 'edg' / 'llvm-cir')
    )
    install_dir = work_dir / 'install'
    build_toolchain(work_dir, install_dir, image)
  staged = Path(os.getcwd()) / 'llvm-cir'
  shutil.copytree(install_dir, staged, symlinks = True)
  strip_executables(staged, image)

def main() -> None:
  stage_prefix()

main()
