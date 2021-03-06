// RUN: 3c -alltypes -addcr %s -- | FileCheck -match-full-lines -check-prefixes="CHECK_ALL","CHECK" %s
// RUN: 3c -addcr %s -- | FileCheck -match-full-lines -check-prefixes="CHECK_NOALL","CHECK" %s
// RUN: 3c -addcr %s -- | %clang -c -fcheckedc-extension -x c -o /dev/null -
// RUN: 3c -output-postfix=checked -alltypes %s
// RUN: 3c -alltypes %S/b28_structcastimplicit.checked.c -- | count 0
// RUN: rm %S/b28_structcastimplicit.checked.c
#include <stddef.h>
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
	//CHECK: _Ptr<int> x;
    char *y;
	//CHECK: _Ptr<char> y;
};


struct r {
    int data;
    struct r *next;
	//CHECK: struct r *next;
};


struct r *sus(struct r x, struct r y) {
	//CHECK: struct r *sus(struct r x, struct r y) : itype(_Ptr<struct r>) {
  x.next += 1;
  struct r *z = malloc(sizeof(struct r));
	//CHECK: _Ptr<struct r> z =  malloc<struct r>(sizeof(struct r));
  z->data = 1;
  z->next = NULL;
  return z;
}

struct r *foo() {
	//CHECK: _Ptr<struct r> foo(void) {
  struct r x, y;
  x.data = 2;
  y.data = 1;
  x.next = &y;
  y.next = &x;
  struct r *z = (struct r *) sus(x, y);
	//CHECK: _Ptr<struct r> z = (_Ptr<struct r>) sus(x, y);
  return z;
}

struct np *bar() {
	//CHECK: struct np *bar(void) {
  struct r x, y;
  x.data = 2;
  y.data = 1;
  x.next = &y;
  y.next = &x;
  struct np *z = sus(x, y);
	//CHECK: struct np *z = sus(x, y);
  return z;
}
