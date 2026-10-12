# M4a probes

Small C++ programs for the M4 research (`cir_src/doc/research/il_to_ecil_map_m4.md`,
section 12). Each compiles and runs with g++; the first line is `//options:` where a
language mode is needed. `edg-cir-fe-facts facts` and `cir_src/test/writer/tools/il_query.py`
print the front end's facts from an `--il_display` dump of the debug `cpfe-cir`
(commands in section 0 of the note). `allp.sh`-style runs of `probes/*.cpp` do not
include this directory: none of these compiles with the CIR back end before M4b.
