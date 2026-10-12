#!/usr/bin/env python3
"""Print facts of the front end's unlowered IL from an --il_display dump.

Used for the M4a research (research/il_to_ecil_map_m4.md).  The dump is made
by the debug cpfe-cir (see the note, section 0), for example

  il_query.py probe.il inits              constructor/destructor init lists
  il_query.py probe.il handlers throws    catch parameters, throw operands
  il_query.py probe.il specs 'f[0-9]|~S'  exception specifications
  il_query.py probe.il lifetimes          destruction lists and their EH flags
  il_query.py probe.il newdel             new/delete expressions
  il_query.py probe.il opkinds N          operator kinds of nodes after seq N
  il_query.py probe.il fn NAME [special]  statement tree of a function

The parser is the one of dev_tools/bin/edg-cir-fe-facts.
"""

import importlib.machinery
import importlib.util
import os
import re
import sys

S = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..',
                 '..', 'dev_tools', 'bin', 'edg-cir-fe-facts')
l=importlib.machinery.SourceFileLoader('fef',S); sp=importlib.util.spec_from_loader('fef',l); m=importlib.util.module_from_spec(sp); l.exec_module(m)
def load(f): return m.parse_il(open(f).read())
def lab(v):
    p=m.pointer(v); 
    return (p[2] or p[1]) if p else v
def routines(es, name):
    return [e for e in es.values() if e.kind=='routine' and re.fullmatch(name, (e.get('source_corresp.name') or ''))]
def show(es, e, keys=None):
    print(e.kind, e.addr)
    for k,v in e.fields.items():
        if keys is None or any(re.fullmatch(x,k) for x in keys): print('  ',k,'=',v)
def ctor_inits(es, rname, which=0):
    rs=[e for e in es.values() if e.kind=='routine' and e.get('source_corresp.name')==rname and e.get('special_kind') in('sfk_constructor','sfk_destructor')]
    return rs

def ptr(es, v):
    p=m.pointer(v)
    return es.get(p[1]) if p else None

def inits(es, user_only=True):
    for e in es.values():
        if e.kind=='scope' and e.get('kind')=='sck_function':
            r=ptr(es,e.get('routine.ptr'))
            if r is None: continue
            sk=r.get('special_kind')
            nm=lab(e.get('routine.ptr'))
            ci=ptr(es,e.get('constructor_inits'))
            if ci is None: continue
            print('==',nm,sk, 'spec' if False else '')
            while ci is not None:
                k=ci.get('kind'); d=ptr(es,ci.get('initializer'))
                desc=lab(ci.get('base_class')) if 'base' in k else lab(ci.get('field')) if k=='cik_field' else ''
                dk=d.get('kind') if d else None
                rt=lab(d.get('routine')) if d and d.get('routine') else ''
                dt=lab(d.get('destructor')) if d and d.get('destructor') else ''
                print('   ',k,desc,'cg='+ci.get('compiler_generated'),'init=',dk,rt,'dtor=',dt)
                ci=ptr(es,ci.get('next'))

def typ(es, v):
    p=m.pointer(v)
    return (p[2] or p[1]) if p else v

def handlers(es):
    for e in es.values():
        if e.kind=='statement' and e.get('kind')=='stmk_try_block':
            print('TRY', e.addr, 'ftb='+e.get('is_function_try_block'), 'line', e.get('position.seq'))
            h=ptr(es,e.get('handlers'))
            while h is not None:
                prm=ptr(es,h.get('parameter')); di=ptr(es,h.get('dynamic_init'))
                pt=typ(es,prm.get('type')) if prm else '...'
                pn=prm.get('source_corresp.name') if prm else ''
                s='   catch(%s %s)'%(pt,pn)
                if di:
                    s+=' init=%s'%di.get('kind')
                    for k in ('routine','destructor','is_copy_constructor_with_implied_source','is_implicit_copy_for_copy_initialization','args','variable'):
                        if di.get(k) and di.get(k) not in('NULL','FALSE'): s+=' %s=%s'%(k,typ(es,di.get(k)))
                    for k in di.fields:
                        if k not in ('next','kind','routine','destructor','variable','args') and di.get(k) not in('NULL','FALSE') and not k.startswith('is_copy') and not k.startswith('is_impl'): s+=' %s=%s'%(k,di.get(k)[:40])
                print(s); h=ptr(es,h.get('next'))

def throws(es):
    for e in es.values():
        if e.kind=='expr-node' and e.get('kind')=='enk_throw':
            di=ptr(es,e.get('dynamic_init'))
            s='THROW type=%s dtor=%s'%(typ(es,e.get('type')) if e.get('type') else 'NULL', typ(es,e.get('destructor')) if e.get('destructor') else 'NULL')
            if di:
                s+=' init=%s'%di.get('kind')
                for k in di.fields:
                    if k not in ('next',) and di.get(k) not in('NULL','FALSE'): s+=' %s=%s'%(k,typ(es,di.get(k))[:50])
            print(s)

def lifetimes(es, only_user=True):
    # map lifetime -> owning function name via scope.lifetime? print each lifetime with destructions
    for e in es.values():
        if e.kind=='object-lifetime':
            d=ptr(es,e.get('destructions'))
            if d is None: continue
            print('LIFETIME', e.addr, e.get('kind'), 'entity=', typ(es,e.get('entity')) if e.get('entity') else '')
            while d is not None:
                fl=[k for k in ('is_freeing_of_storage_on_exception','is_array_freeing','destruction_is_for_partially_constructed_aggregate','overlaps_temps_in_inner_lifetime','inside_conditional_expression','unordered','has_temporary_lifetime','is_partially_initialized','is_constructor_init','static_temp') if d.get(k)=='TRUE']
                print('    DI', d.addr, d.get('kind'), 'dtor=', typ(es,d.get('destructor')) if d.get('destructor') and d.get('destructor')!='NULL' else '-', 'var=', typ(es,d.get('variable')) if d.get('variable') and d.get('variable')!='NULL' else '-', fl)
                d=ptr(es,d.get('next_in_destruction_list'))

def specs(es, names=None):
    for r in es.values():
        if r.kind!='routine': continue
        nm=lab(r.get('source_corresp.name')).strip('"') if r.get('source_corresp.name') else ''
        if names and not re.fullmatch(names,nm or ''): continue
        t=ptr(es,r.get('type'))
        if t is None: continue
        esp=ptr(es,t.get('exception_specification'))
        s='%s defined=%s never_throws=%s trivdef=%s trivcopy=%s special=%s'%(nm,r.get('defined'),r.get('never_throws'),r.get('is_trivial_default_constructor'),r.get('is_trivial_copy_function'),r.get('special_kind'))
        if esp:
            s+=' ESP['+' '.join('%s=%s'%(k,(typ(es,v) if m.pointer(v) else v)) for k,v in esp.fields.items() if v not in('FALSE','NULL','0'))+']'
            lst=ptr(es,esp.get('variant.exception_specification_type_list')) if esp.get('variant.exception_specification_type_list') else None
        else: s+=' ESP=none'
        print(s)

def stmt_tree(es, st, depth=0, maxd=6):
    pad='  '*depth
    k=st.get('kind')
    extra=''
    if k=='stmk_try_block': extra=' ftb=%s'%st.get('is_function_try_block')
    print(pad+k+extra, st.addr)
    if depth>=maxd: return
    for f in ('statements','statement','then_statement','else_statement'):
        c=ptr(es,st.get(f)) if st.get(f) else None
        while c is not None:
            stmt_tree(es,c,depth+1,maxd)
            c=ptr(es,c.get('next')) if f=='statements' else None
    if k=='stmk_try_block':
        h=ptr(es,st.get('handlers'))
        while h is not None:
            print(pad+'  handler')
            b=ptr(es,h.get('statement'))
            if b is not None: stmt_tree(es,b,depth+2,maxd)
            h=ptr(es,h.get('next'))

def fn_stmts(es, name, special=None, maxd=6):
    for e in es.values():
        if e.kind=='scope' and e.get('kind')=='sck_function':
            r=ptr(es,e.get('routine.ptr'))
            if r is None: continue
            if lab(e.get('routine.ptr'))==name and (special is None or r.get('special_kind')==special):
                print('FUNCTION',name,r.get('special_kind'),'ci=',e.get('constructor_inits'))
                b=ptr(es,e.get('assoc_block'))
                if b: stmt_tree(es,b,1,maxd)

def newdel(es):
    for e in es.values():
        if e.kind=='expr-node' and e.get('kind')=='enk_new_delete':
            di=ptr(es,e.get('dynamic_init')) if e.get('dynamic_init') else None
            fr=ptr(es,e.get('freeing_of_storage_on_exception')) if e.get('freeing_of_storage_on_exception') else None
            fl=[k for k in ('is_new','placement_new','aligned_version','array_delete','global_new_or_delete','has_new_initializer','new_initializer_is_brace_enclosed') if e.get(k)=='TRUE']
            print('NEWDEL', typ(es,e.get('type')) if e.get('type') else '', fl, 'routine=',typ(es,e.get('routine')) if e.get('routine') and e.get('routine')!='NULL' else '-', 'di=',(di.get('kind'), typ(es,di.get('destructor')) if di.get('destructor')!='NULL' else '-', typ(es,di.get('routine')) if di.get('routine') else '') if di else '-', 'free=', (typ(es,fr.get('destructor')), fr.get('is_array_freeing')) if fr else '-')

def opkinds(es, minseq=8900):
    c={}
    for e in es.values():
        if e.kind=='expr-node' and e.get('kind')=='enk_operation':
            try: s=int(e.get('position.seq','0'))
            except: s=0
            if s>=minseq:
                k=e.get('operation.kind'); c[k]=c.get(k,0)+1
    return c


def main():
  if len(sys.argv) < 3:
    print(__doc__)
    return 2
  es = load(sys.argv[1])
  args = sys.argv[2:]
  i = 0
  while i < len(args):
    a = args[i]
    if a == 'inits':
      inits(es)
    elif a == 'handlers':
      handlers(es)
    elif a == 'throws':
      throws(es)
    elif a == 'lifetimes':
      lifetimes(es)
    elif a == 'newdel':
      newdel(es)
    elif a == 'specs':
      specs(es, args[i + 1] if i + 1 < len(args) else None)
      i += 1
    elif a == 'opkinds':
      print(opkinds(es, int(args[i + 1])))
      i += 1
    elif a == 'fn':
      fn_stmts(es, args[i + 1], args[i + 2] if i + 2 < len(args) else None)
      i += 2
    i += 1
  return 0


if __name__ == '__main__':
  sys.exit(main())
