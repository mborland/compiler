#!/usr/bin/env python3
"""Call-graph check of the ECIL writer: reports the cycles (strongly connected
components with a call cycle, including a function that calls itself) among
the functions of src/ecil_*.c and src/cir_be.c, which must not recurse on the
depth of the input (design: explicit heap work stacks).

Run inside the cir-env container from the repository root, after building a
CIR front end in BUILD (default build/gcc-cir, the unoptimized debug build:
an optimized build inlines callees and hides call cycles, so a release
build is not a check; the default is the one to quote):
  callgraph_check.py [BUILD...]
With several BUILDs each is checked in turn (the exit status is the sum).
With CG_SRC_DIR set, the six files (and their headers ecil_write.h,
cir_be.h) are taken from that directory instead of src/.
It takes the compile commands of the writer files from `ninja -t commands`,
compiles each with -fcallgraph-info (GCC 10 or later) and reads the .ci
files.  Indirect calls are not edges.  Prints the number of functions and
the cycles; the exit status is the number of cycles."""
import os, re, subprocess, sys, tempfile

if len(sys.argv) > 2:
    status = 0
    for each in sys.argv[1:]:
        print('build: ' + each)
        sys.stdout.flush()
        status += subprocess.run([sys.executable, sys.argv[0],
                                  each]).returncode
    sys.exit(status)
build = sys.argv[1] if len(sys.argv) > 1 else 'build/gcc-cir'
files = ['ecil_write', 'ecil_type', 'ecil_const', 'ecil_code', 'ecil_sink',
         'cir_be']
cmds = subprocess.run(['ninja', '-C', build, '-t', 'commands', 'bin/cpfe-cir'],
                      capture_output=True, text=True, check=True).stdout
edges = {}
nodes = set()
tmp = tempfile.mkdtemp(prefix='cgcheck.')
for name in files:
    cmd = [l for l in cmds.splitlines()
           if ('/' + name + '.c.o') in l and ' -c ' in l]
    if len(cmd) != 1:
        sys.exit('no unique compile command for ' + name)
    line = cmd[0]
    line = re.sub(r'-o \S+', '-o %s/%s.o' % (tmp, name), line)
    if os.environ.get('CG_SRC_DIR'):
        # Check other sources (e.g. an older commit) with the same flags.
        line = re.sub(r'\S*src/%s\.c' % name, '%s/%s.c' % (
                      os.path.abspath(os.environ['CG_SRC_DIR']), name), line)
        line += ' -I' + os.path.abspath('src')
    line = re.sub(r'-MD -MT \S+ -MF \S+', '', line)
    line = re.sub(r'-fsanitize=\S+', '', line)
    done = subprocess.run('cd %s && %s -fcallgraph-info' % (build, line),
                          shell=True, capture_output=True, text=True)
    if done.returncode != 0:
        sys.exit('compiling %s failed:\n%s' % (name, done.stderr[-2000:]))
    ci = '%s/%s.ci' % (tmp, name)
    if not os.path.exists(ci):
        ci = '%s/%s.o.ci' % (tmp, name)
    text = open(ci).read()
    for m in re.finditer(r'node: \{ title: "([^"]*)"', text):
        nodes.add(m.group(1))
    for m in re.finditer(r'edge: \{ sourcename: "([^"]*)" targetname: "([^"]*)"',
                         text):
        edges.setdefault(m.group(1), set()).add(m.group(2))
# Only functions defined in these files can be part of a cycle.
graph = {n: [t for t in edges.get(n, ()) if t in nodes] for n in nodes}
# Tarjan (iterative).
index = {}
low = {}
onstack = set()
stack = []
cycles = []
counter = [0]
for root in sorted(graph):
    if root in index:
        continue
    work = [(root, iter(graph[root]))]
    index[root] = low[root] = counter[0]
    counter[0] += 1
    stack.append(root)
    onstack.add(root)
    while work:
        v, it = work[-1]
        advanced = False
        for w in it:
            if w not in index:
                index[w] = low[w] = counter[0]
                counter[0] += 1
                stack.append(w)
                onstack.add(w)
                work.append((w, iter(graph[w])))
                advanced = True
                break
            elif w in onstack:
                low[v] = min(low[v], index[w])
        if advanced:
            continue
        work.pop()
        if work:
            low[work[-1][0]] = min(low[work[-1][0]], low[v])
        if low[v] == index[v]:
            comp = []
            while True:
                w = stack.pop()
                onstack.discard(w)
                comp.append(w)
                if w == v:
                    break
            if len(comp) > 1 or v in graph[v]:
                cycles.append(sorted(comp))
print('functions: %d, call edges: %d, cycles: %d' %
      (len(nodes), sum(len(v) for v in graph.values()), len(cycles)))
for c in cycles:
    print('cycle:', ' -> '.join(c))
sys.exit(len(cycles))
