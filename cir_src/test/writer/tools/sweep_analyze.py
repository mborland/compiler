import sys,re,collections,os,json
d=sys.argv[1]
enums=json.load(open(sys.argv[2])) if len(sys.argv)>2 else None
def nice(x):
    m=re.match(r'the (operator|expression kind|constant kind) (\d+)$',x)
    if m and enums:
        key={'operator':'an_expr_operator_kind','expression kind':'an_expr_node_kind','constant kind':'a_constant_repr_kind'}[m.group(1)]
        return enums[key][int(m.group(2))]
    return x
rows=[l.rstrip('\n').split('\t') for l in open(d+'/results.tsv')]
b=collections.Counter(); recon=collections.Counter(); ex=collections.defaultdict(list)
for r in rows:
    f,rc1,rc2,same,dr=r[:5]; v=r[5] if len(r)>5 else ''
    n=f.replace('/','_')
    err=open(f'{d}/{n}.1.err',errors='replace').read()
    flat=re.sub(r'\s+',' ',err)
    reasons=re.findall(r'the (?:body|initializer) of "[^"]*" uses (.*?), which the CIR back end',flat)
    if rc1 in('139','134','124','136','132') or int(rc1)>=124 and int(rc1)!=0 and False: cat='crash'
    elif dr=='1': cat='invalid'
    elif same=='0': cat='nondeterministic'
    elif rc1!='0':
        cat='fe_error'
    elif reasons: cat='unsupported'
    else: cat='clean'
    b[cat]+=1; ex[cat].append(f)
    for x in set(reasons): recon[nice(x)]+=1
print(dict(b))
print('--- unsupported reasons (files affected)')
for k,v in recon.most_common(40): print(v,k)
for c in ('crash','invalid','nondeterministic'):
    print('---',c,ex[c][:15])
# fe_error: classify by first error text
ec=collections.Counter()
for f in ex['fe_error']:
    n=f.replace('/','_'); err=open(f'{d}/{n}.1.err',errors='replace').read()
    m=re.search(r'(error|Assertion|internal error|Segmentation|Aborted|catastrophic)[^\n]*',err)
    ec[(m.group(0)[:90] if m else 'rc'+str(rows[[r[0] for r in rows].index(f)][1]))]+=1
print('--- fe_error top'); 
for k,v in ec.most_common(15): print(v,k)
