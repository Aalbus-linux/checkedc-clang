// RUN: cconv-standalone %s -- | FileCheck -match-full-lines %s

#define NULL ((void*)0)
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

struct r *sus(struct r *, struct r *);
//CHECK: _Ptr<struct r> sus(struct r *x : itype(_Ptr<struct r>), struct r *y : itype(_Ptr<struct r>));

struct r *foo() {
  struct r *x, *y;
  x->data = 2;
  y->data = 1;
  // This will make y wild because next is WILD
  x->next = &y;
  // Similar to the above this will make x WILD
  y->next = &x;
  struct r *z = (struct r *) sus(x, y);
  return z;
}
//CHECK: _Ptr<struct r> foo(void) {

struct r *bar() {
  struct r *x, *y;
  x->data = 2;
  y->data = 1;
  x->next = &y;
  y->next = &x;
  struct r *z = sus(x, y);
  return z;
}
//CHECK: _Ptr<struct r> bar(void) {
//CHECK: _Ptr<struct r> z =  sus(x, y);

struct r *sus(struct r *x, struct r *y) {
  // This will make next pointer WILD	
  x->next += 1;
  struct r *z = malloc(sizeof(struct r));
  z->data = 1;
  z->next = 0;
  return z;
}
//CHECK: _Ptr<struct r> sus(struct r *x : itype(_Ptr<struct r>), struct r *y : itype(_Ptr<struct r>)) {
