// Tests for Checked C rewriter tool.
//
// Checks very simple inference properties for local variables.
//
// RUN: checked-c-convert %s -- | FileCheck -match-full-lines %s
// RUN: checked-c-convert %s -- | %clang_cc1 -verify -fcheckedc-extension -x c -
// expected-no-diagnostics
#include <stdarg.h>

int doStuff(unsigned int tag, va_list arg) {
  return 0;
}
//CHECK: int doStuff(unsigned int tag, va_list arg) {

int *id(int *a) {
  return a;
}
//CHECK: _Ptr<int> id(_Ptr<int> a) {
