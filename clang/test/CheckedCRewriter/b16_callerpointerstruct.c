// RUN: cconv-standalone -alltypes %s -- | FileCheck -match-full-lines -check-prefixes="CHECK_ALL" %s
//RUN: cconv-standalone %s -- | FileCheck -match-full-lines -check-prefixes="CHECK_NOALL" %s
//RUN: cconv-standalone -output-postfix=checkedNOALL %s
//RUN: %clang -c %S/b16_callerpointerstruct.checkedNOALL.c
//RUN: rm %S/b16_callerpointerstruct.checkedNOALL.c

typedef unsigned long size_t;
#define NULL 0
extern _Itype_for_any(T) void *calloc(size_t nmemb, size_t size) : itype(_Array_ptr<T>) byte_count(nmemb * size);
extern _Itype_for_any(T) void free(void *pointer : itype(_Array_ptr<T>) byte_count(0));
extern _Itype_for_any(T) void *malloc(size_t size) : itype(_Array_ptr<T>) byte_count(size);
extern _Itype_for_any(T) void *realloc(void *pointer : itype(_Array_ptr<T>) byte_count(1), size_t size) : itype(_Array_ptr<T>) byte_count(size);
extern int printf(const char * restrict format : itype(restrict _Nt_array_ptr<const char>), ...);
extern _Unchecked char *strcpy(char * restrict dest, const char * restrict src : itype(restrict _Nt_array_ptr<const char>));

struct np {
  int x;
  int y;
};

struct p {
  int *x;
  char *y;
};

struct r {
  int data;
  struct r *next;
};

struct p *sus(struct p *x, struct p *y) {
  x->y += 1;
  struct p *z = malloc(sizeof(struct p));
  return z;
}
//CHECK_NOALL: struct p *sus(_Ptr<struct p> x, _Ptr<struct p> y) : itype(_Ptr<struct p>) {
//CHECK_NOALL:   _Ptr<struct p> z =  malloc<struct p>(sizeof(struct p));
//CHECK_ALL: struct p * sus(_Ptr<struct p> x, _Ptr<struct p> y) {
//CHECK_ALL:   struct p *z = malloc<struct p>(sizeof(struct p));


struct p *foo() {
  int ex1 = 2, ex2 = 3;
  struct p *x, *y;
  x->x = &ex1;
  y->x = &ex2;
  x->y = &ex2;
  y->y = &ex1;
  struct p *z = (struct p *) sus(x, y);
  return z;
}
//CHECK_NOALL: _Ptr<struct p> foo(void) {
//CHECK_NOALL:  _Ptr<struct p> x = ((void *)0);
//CHECK_NOALL: _Ptr<struct p> y = ((void *)0);
//CHECK_NOALL:   _Ptr<struct p> z =  (struct p *) sus(x, y);
//CHECK_ALL: struct p *foo() {
//CHECK_ALL:  _Ptr<struct p> x = ((void *)0);
//CHECK_ALL: _Ptr<struct p> y = ((void *)0);
//CHECK_ALL:   struct p *z = (struct p *) sus(x, y);


struct p *bar() {
  int ex1 = 2, ex2 = 3;
  struct p *x, *y;
  x->x = &ex1;
  y->x = &ex2;
  x->y = &ex2;
  y->y = &ex1;
  struct p *z = (struct p *) sus(x, y);
  z += 2;
  return z;
}
//CHECK_NOALL: struct p *bar() {
//CHECK_NOALL:  _Ptr<struct p> x = ((void *)0);
//CHECK_NOALL: _Ptr<struct p> y = ((void *)0);
//CHECK_NOALL:   struct p *z = (struct p *) sus(x, y);
//CHECK_ALL: struct p *bar() {
//CHECK_ALL:  _Ptr<struct p> x = ((void *)0);
//CHECK_ALL: _Ptr<struct p> y = ((void *)0);
//CHECK_ALL:   struct p *z = (struct p *) sus(x, y);
