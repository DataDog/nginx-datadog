#if defined(__linux__) && !defined(__GLIBC__)

#ifdef __x86_64__
float ceilf(float x) {
    float result;
    __asm__(
        "roundss $0x0A, %[x], %[result]"
        : [result] "=x" (result)
        : [x] "x" (x)
    );
    return result;
}
double ceil(double x) {
    double result;
    __asm__(
        "roundsd $0x0A, %[x], %[result]"
        : [result] "=x" (result)
        : [x] "x" (x)
    );
    return result;
}
#endif

#ifdef __aarch64__
float ceilf(float x) {
    float result;
    __asm__(
        "frintp %s0, %s1\n"
        : "=w" (result)
        : "w" (x)
    );
    return result;
}
double ceil(double x) {
    double result;
    __asm__(
        "frintp %d0, %d1\n"
        : "=w" (result)
        : "w" (x)
    );
    return result;
}
#endif

int stat(const char *restrict path, void *restrict buf) {
    int __xstat(int, const char *restrict, void *restrict);
    return __xstat(3, path, buf);
}

int fstat(int fd, void *buf) {
    int __fxstat(int, int, void *);
    return __fxstat(3, fd, buf);
}

// glibc doesn't define pthread_atfork on aarch64. We need to delegate to
// glibc's __register_atfork() instead. __register_atfork() takes an extra
// argument, __dso_handle, which is a pointer to the DSO that is registering the
// fork handlers. This is used to ensure that the handlers are not called after
// the DSO is unloaded. glibc on amd64 also implements pthread_atfork() in terms
// of __register_atfork().  (musl never unloads modules so that potential
// problem doesn't exist)

// On amd64, even though pthread_atfork is exported by glibc, it should not be
// used. Code that uses pthread_atfork will compile to an import to
// __register_atfork(), but here we're compiling against musl, resulting in an
// an import to pthread_atfork. This will cause a runtime error after the test
// that unloads our module. The reason is that when we call pthread_atfork in
// glibc, __register_atfork() is called with the __dso_handle of libc6.so, not
// the __dso_handle of our module. So the fork handler is not unregistered when
// our module is unloaded.

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

extern void *__dso_handle __attribute__((weak));
int __register_atfork(void (*prepare)(void), void (*parent)(void),
                      void (*child)(void), void *__dso_handle)
    __attribute__((weak));

int pthread_atfork(void (*prepare)(void), void (*parent)(void),
                   void (*child)(void)) {
    // glibc
    if (__dso_handle && __register_atfork) {
      return __register_atfork(prepare, parent, child, __dso_handle);
    }

    static int (*real_atfork)(void (*)(void), void (*)(void), void (*)(void));

    if (!real_atfork) {
      // dlopen musl
#ifdef __aarch64__
      void *handle = dlopen("ld-musl-aarch64.so.1", RTLD_LAZY);
      if (!handle) {
        fprintf(stderr, "dlopen of ld-musl-aarch64.so.1 failed: %s\n",
                dlerror());
        abort();
      }
#else
      void *handle = dlopen("libc.musl-x86_64.so.1", RTLD_LAZY);
      if (!handle) {
        fprintf(stderr, "dlopen of libc.musl-x86_64.so.1 failed: %s\n",
                dlerror());
        abort();
      }
#endif
      real_atfork = dlsym(handle, "pthread_atfork");
      if (!real_atfork) {
        fprintf(stderr, "dlsym of pthread_atfork failed: %s\n", dlerror());
        abort();
      }
    }

    return real_atfork(prepare, parent, child);
}

// the symbol strerror_r in glibc is not the POSIX version; it returns char *
// __xpg_sterror_r is exported by both glibc and musl
int strerror_r(int errnum, char *buf, size_t buflen) {
    int __xpg_strerror_r(int, char *, size_t);
    return __xpg_strerror_r(errnum, buf, buflen);
}

#endif
