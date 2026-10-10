#!/bin/bash
cd build/cir && ctest -R "format" --output-on-failure 2>&1 | grep -v "^$" | head -${LINES_MAX:-50}
