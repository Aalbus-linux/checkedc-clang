; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc < %s -mtriple=i386-unknown -mattr=+sse2 | FileCheck %s --check-prefix=SSE
; RUN: llc < %s -mtriple=i386-unknown -mattr=+avx | FileCheck %s --check-prefix=AVX

define void @t(<4 x float> %A) {
; SSE-LABEL: t:
; SSE:       # %bb.0:
; SSE-NEXT:    xorps {{\.LCPI.*}}, %xmm0
; SSE-NEXT:    movaps %xmm0, 0
; SSE-NEXT:    retl
;
; AVX-LABEL: t:
; AVX:       # %bb.0:
; AVX-NEXT:    vxorps {{\.LCPI.*}}, %xmm0, %xmm0
; AVX-NEXT:    vmovaps %xmm0, 0
; AVX-NEXT:    retl
  %tmp1277 = fsub <4 x float> < float -0.000000e+00, float -0.000000e+00, float -0.000000e+00, float -0.000000e+00 >, %A
  store <4 x float> %tmp1277, <4 x float>* null
  ret void
}

define <4 x float> @t1(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: t1:
; SSE:       # %bb.0: # %entry
; SSE-NEXT:    xorps %xmm1, %xmm0
; SSE-NEXT:    retl
;
; AVX-LABEL: t1:
; AVX:       # %bb.0: # %entry
; AVX-NEXT:    vxorps %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retl
entry:
  %tmp9 = bitcast <4 x float> %a to <4 x i32>
  %tmp10 = bitcast <4 x float> %b to <4 x i32>
  %tmp11 = xor <4 x i32> %tmp9, %tmp10
  %tmp13 = bitcast <4 x i32> %tmp11 to <4 x float>
  ret <4 x float> %tmp13
}

define <2 x double> @t2(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: t2:
; SSE:       # %bb.0: # %entry
; SSE-NEXT:    andps %xmm1, %xmm0
; SSE-NEXT:    retl
;
; AVX-LABEL: t2:
; AVX:       # %bb.0: # %entry
; AVX-NEXT:    vandps %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retl
entry:
  %tmp9 = bitcast <2 x double> %a to <2 x i64>
  %tmp10 = bitcast <2 x double> %b to <2 x i64>
  %tmp11 = and <2 x i64> %tmp9, %tmp10
  %tmp13 = bitcast <2 x i64> %tmp11 to <2 x double>
  ret <2 x double> %tmp13
}

define void @t3(<4 x float> %a, <4 x float> %b, <4 x float>* %c, <4 x float>* %d) {
; SSE-LABEL: t3:
; SSE:       # %bb.0: # %entry
; SSE-NEXT:    movl {{[0-9]+}}(%esp), %eax
; SSE-NEXT:    movl {{[0-9]+}}(%esp), %ecx
; SSE-NEXT:    andnps %xmm1, %xmm0
; SSE-NEXT:    orps (%ecx), %xmm0
; SSE-NEXT:    movaps %xmm0, (%eax)
; SSE-NEXT:    retl
;
; AVX-LABEL: t3:
; AVX:       # %bb.0: # %entry
; AVX-NEXT:    movl {{[0-9]+}}(%esp), %eax
; AVX-NEXT:    movl {{[0-9]+}}(%esp), %ecx
; AVX-NEXT:    vandnps %xmm1, %xmm0, %xmm0
; AVX-NEXT:    vorps (%ecx), %xmm0, %xmm0
; AVX-NEXT:    vmovaps %xmm0, (%eax)
; AVX-NEXT:    retl
entry:
  %tmp3 = load <4 x float>, <4 x float>* %c
  %tmp11 = bitcast <4 x float> %a to <4 x i32>
  %tmp12 = bitcast <4 x float> %b to <4 x i32>
  %tmp13 = xor <4 x i32> %tmp11, < i32 -1, i32 -1, i32 -1, i32 -1 >
  %tmp14 = and <4 x i32> %tmp12, %tmp13
  %tmp27 = bitcast <4 x float> %tmp3 to <4 x i32>
  %tmp28 = or <4 x i32> %tmp14, %tmp27
  %tmp30 = bitcast <4 x i32> %tmp28 to <4 x float>
  store <4 x float> %tmp30, <4 x float>* %d
  ret void
}

define <2 x i64> @andn_double_xor(<2 x i64> %a, <2 x i64> %b, <2 x i64> %c) {
; SSE-LABEL: andn_double_xor:
; SSE:       # %bb.0:
; SSE-NEXT:    xorps %xmm2, %xmm1
; SSE-NEXT:    andnps %xmm1, %xmm0
; SSE-NEXT:    retl
;
; AVX-LABEL: andn_double_xor:
; AVX:       # %bb.0:
; AVX-NEXT:    vxorps %xmm2, %xmm1, %xmm1
; AVX-NEXT:    vandnps %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retl
  %1 = xor <2 x i64> %a, <i64 -1, i64 -1>
  %2 = xor <2 x i64> %b, %c
  %3 = and <2 x i64> %1, %2
  ret <2 x i64> %3
}

