import re,sys
base=open('f.base.cir').read().split('\n')
# lines index: find function body bounds
start=[i for i,l in enumerate(base) if '@_Z1fv()' in l][0]
end=[i for i,l in enumerate(base) if i>start and l=='  }'][0]
PA='{llvm.align = 4 : i64, llvm.dereferenceable = 4 : i64, llvm.nonnull, llvm.noundef}'
D=f'cir.call @_ZN1AD1Ev(%S) nothrow : (!cir.ptr<!rec_A> {PA}) -> ()'
V={}
V['n1_direct_arg']=['%1 = cir.call @_Z4makev() : () -> !rec_A','cir.call @_Z4take1A(%1) : (!rec_A) -> ()','cir.return']
V['n2_result_two_uses']=['%0 = cir.alloca "agg.tmp0" align(4) : !cir.ptr<!rec_A>','%5 = cir.alloca "other" align(4) : !cir.ptr<!rec_A>','%1 = cir.call @_Z4makev() : () -> !rec_A','cir.store align(4) %1, %0 : !rec_A, !cir.ptr<!rec_A>','cir.store align(4) %1, %5 : !rec_A, !cir.ptr<!rec_A>','%2 = cir.load align(4) %0 : !cir.ptr<!rec_A>, !rec_A','cir.call @_Z4take1A(%2) : (!rec_A) -> ()','cir.return']
V['n3_discarded']=['%1 = cir.call @_Z4makev() : () -> !rec_A','cir.return']
V['n4_store_via_loaded_ptr']=['%0 = cir.alloca "agg.tmp0" align(4) : !cir.ptr<!rec_A>','%5 = cir.alloca "pp" align(8) : !cir.ptr<!cir.ptr<!rec_A>>','cir.store %0, %5 : !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>','%6 = cir.load %5 : !cir.ptr<!cir.ptr<!rec_A>>, !cir.ptr<!rec_A>','%1 = cir.call @_Z4makev() : () -> !rec_A','cir.store align(4) %1, %6 : !rec_A, !cir.ptr<!rec_A>','%2 = cir.load align(4) %0 : !cir.ptr<!rec_A>, !rec_A','cir.call @_Z4take1A(%2) : (!rec_A) -> ()',D.replace('%S','%0'),'cir.return']
V['n5_load_through_ptr_arg']=['%0 = cir.alloca "agg.tmp0" align(4) : !cir.ptr<!rec_A>','%5 = cir.alloca "pp" align(8) : !cir.ptr<!cir.ptr<!rec_A>>','cir.store %0, %5 : !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>','%1 = cir.call @_Z4makev() : () -> !rec_A','cir.store align(4) %1, %0 : !rec_A, !cir.ptr<!rec_A>','%6 = cir.load %5 : !cir.ptr<!cir.ptr<!rec_A>>, !cir.ptr<!rec_A>','%2 = cir.load align(4) %6 : !cir.ptr<!rec_A>, !rec_A','cir.call @_Z4take1A(%2) : (!rec_A) -> ()',D.replace('%S','%0'),'cir.return']
for name,body in V.items():
    out=base[:start+1]+['    '+l for l in body]+base[end:]
    open(f'f.{name}.cir','w').write('\n'.join(out))
print(' '.join(V))
