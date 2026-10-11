#!/bin/bash
# Inside cir-env: rebuild the front end (cpfe-cir) and the library.
ninja -C build/gcc-cir cpfe-cir 2>&1 | tail -1
ninja -C build/cir 2>&1 | tail -1
