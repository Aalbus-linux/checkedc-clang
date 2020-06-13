// RUN: cconv-standalone %s -- | FileCheck -match-full-lines %s

#define NULL 0
extern _Itype_for_any(T) void *calloc(size_t nmemb, size_t size) : itype(_Array_ptr<T>) byte_count(nmemb * size);
extern _Itype_for_any(T) void free(void *pointer : itype(_Array_ptr<T>) byte_count(0));
extern _Itype_for_any(T) void *malloc(size_t size) : itype(_Array_ptr<T>) byte_count(size);
extern _Itype_for_any(T) void *realloc(void *pointer : itype(_Array_ptr<T>) byte_count(1), size_t size) : itype(_Array_ptr<T>) byte_count(size);
extern int printf(const char * restrict format : itype(restrict _Nt_array_ptr<const char>), ...);
extern _Unchecked char *strcpy(char * restrict dest, const char * restrict src : itype(restrict _Nt_array_ptr<const char>));

int *sus(int *, int *);
//CHECK: _Ptr<int> sus(int *x, _Ptr<int> y);

int* foo() {
  int sx = 3, sy = 4, *x = &sx, *y = &sy;
  int *z = (int *) sus(x, y);
  *z = *z + 1;
  return z;
}
//CHECK: _Ptr<int> foo(void) {

int* bar() {
  int sx = 3, sy = 4, *x = &sx, *y = &sy;
  int *z = (int *) (sus(x, y));
  return z;
}
//CHECK: _Ptr<int> bar(void) {

int *sus(int *x, int*y) {
  int *z = malloc(sizeof(int));
  *z = 1;
  x++;
  *x = 2;
  return z;
}
//CHECK: _Ptr<int> sus(int *x, _Ptr<int> y) {
