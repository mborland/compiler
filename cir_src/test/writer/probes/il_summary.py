import sys, re
txt = open(sys.argv[1], errors='replace').read()
blocks = re.split(r'\n\n(?=(?:file-scope|func-scope|other-scope)\s)', txt)
def short(v):
    m = re.match(r'(?:file-scope|func-scope)\s+([a-z\-]+)@([0-9a-f]+)(?::\s*(.*))?', v)
    if m:
        return "%s#%s%s" % (m.group(1), m.group(2)[-5:], (":" + m.group(3)) if m.group(3) else "")
    return v
out = []
for b in blocks:
    lines = b.split('\n')
    head = lines[0]
    m = re.match(r'(file-scope|func-scope)\s+([a-z\-]+)@([0-9a-f]+)', head)
    if not m: continue
    kind = m.group(2)
    if kind not in ('expr-node','statement','dynamic-init','constant','variable','routine','field','type','label','scope','param-type'):
        continue
    f = {}
    for l in lines[1:]:
        mm = re.match(r'^([A-Za-z_\.]+):\s*(.*)$', l)
        if mm: f[mm.group(1)] = mm.group(2)
    ident = m.group(3)[-5:]
    if kind == 'expr-node':
        k = f.get('kind','?')
        op = f.get('operation.kind', '')
        flags = [x for x in ('is_lvalue','is_xvalue','result_is_not_used','compiler_generated','is_parenthesized') if f.get(x)=='TRUE' or x in f]
        s = "E#%s %s%s ty=%s" % (ident, k, ('/'+op) if op else '', short(f.get('type','?')))
        if 'orig_lvalue_type' in f: s += " olt=" + short(f['orig_lvalue_type'])
        s += " [" + ",".join(flags) + "]"
        for key in ('variable','routine','field','constant','operands','dynamic_init'):
            if key in f: s += " %s=%s" % (key, short(f[key]))
        if 'next' in f and f['next']!='NULL': s += " next=" + short(f['next'])
        out.append(s)
    elif kind == 'statement':
        s = "S#%s %s" % (ident, f.get('kind','?'))
        for key in ('expr','then_statement','else_statement','statements','next','statement','loop_statement','entities','dynamic_init','label','initialization','increment','extra_info'):
            if key in f and f[key] != 'NULL': s += " %s=%s" % (key, short(f[key]))
        out.append(s)
    elif kind == 'dynamic-init':
        s = "D#%s %s" % (ident, f.get('kind','?'))
        for key in ('variable','expression','constant','next'):
            if key in f and f[key] != 'NULL': s += " %s=%s" % (key, short(f[key]))
        out.append(s)
    elif kind == 'constant':
        s = "C#%s %s ty=%s" % (ident, f.get('kind','?'), short(f.get('type','?')))
        for k2,v in f.items():
            if k2 in ('integer_value','float_value','string','first_constant','last_constant','address.kind','variable','routine','offset','next','value','length','field','array_element','is_field_designator','subobject_path') or k2.startswith('address') or k2.startswith('string'):
                s += " %s=%s" % (k2, short(v))
        out.append(s)
    elif kind == 'variable':
        s = "V#%s %s ty=%s sc=%s init=%s link=%s" % (ident, (f.get('name','?').split('"')[1] if '"' in f.get('name','') else f.get('name','?')), short(f.get('type','?')), f.get('storage_class'), f.get('init_kind'), f.get('name_linkage'))
        out.append(s)
    elif kind == 'label':
        out.append("L#%s %s" % (ident, f.get('name','')))
print("\n".join(out))
