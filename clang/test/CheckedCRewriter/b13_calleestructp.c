// RUN: cconv-standalone -alltypes %s -- | FileCheck -match-full-lines -check-prefixes="CHECK_ALL","CHECK" %s
//RUN: cconv-standalone %s -- | FileCheck -match-full-lines -check-prefixes="CHECK_NOALL","CHECK" %s
// RUN: cconv-standalone %s -- | %clang -c -fcheckedc-extension -x c -o /dev/null -
#include <stddef.h>
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
	//CHECK_NOALL: int *x;
	//CHECK_ALL:     _Array_ptr<int> x;
    char *y;
	//CHECK: _Ptr<char> y;
};


struct r {
    int data;
    struct r *next;
	//CHECK: _Ptr<struct r> next;
};


struct p sus(struct p x) {
  x.x += 1;
  struct p *n = malloc(sizeof(struct p));
	//CHECK: _Ptr<struct p> n =  malloc<struct p>(sizeof(struct p));
  return *n;
}

struct p foo(void) {
  struct p x;
  struct p z = sus(x);
  return z;
}

struct p bar(void) {
  struct p x;
  struct p z = sus(x);
  return z;
}