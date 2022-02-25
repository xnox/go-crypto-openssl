// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//go:build linux && !android
// +build linux,!android

#include "goopenssl.h"

#include <dlfcn.h>
#include <stdio.h>

static unsigned long
version_num(void* handle)
{
    unsigned long (*fn)(void);
    // OPENSSL_version_num is defined in OpenSSL 1.1.0 and 1.1.1.
    fn = (unsigned long (*)(void))dlsym(handle, "OpenSSL_version_num");
    if (fn != NULL)
        return fn();

    // SSLeay is defined in OpenSSL 1.0.2.
    fn = (unsigned long (*)(void))dlsym(handle, "SSLeay");
    if (fn != NULL)
        return fn();

    return 0;
} 

int
go_openssl_version_major(void* handle)
{
    unsigned int (*fn)(void);
    // OPENSSL_version_major is supported since OpenSSL 3.
    fn = (unsigned int (*)(void))dlsym(handle, "OPENSSL_version_major");
    if (fn != NULL)
        return (int)fn();

    // If OPENSSL_version_major is not defined, try with OpenSSL 1 functions.
    unsigned long num = version_num(handle);
    if (num < 0x10000000L || num >= 0x20000000L)
        return -1;

    return 1;
}

int
go_openssl_version_minor(void* handle)
{
    unsigned int (*fn)(void);
    // OPENSSL_version_major is supported since OpenSSL 3.
    fn = (unsigned int (*)(void))dlsym(handle, "OPENSSL_version_minor");
    if (fn != NULL)
        return (int)fn();

    // If OPENSSL_version_major is not defined, try with OpenSSL 1 functions.
    unsigned long num = version_num(handle);
    // We only support minor version 0 and 1.
    if (num < 0x10000000L || num >= 0x10200000L)
        return -1;

    if (num >= 0x10100000L)
        return 1;
    
    return 0;
}

// Approach taken from .Net System.Security.Cryptography.Native
// https://github.com/dotnet/runtime/blob/f64246ce08fb7a58221b2b7c8e68f69c02522b0d/src/libraries/Native/Unix/System.Security.Cryptography.Native/opensslshim.c

#define DEFINEFUNC(ret, func, args, argscall)                  ret (*_g_##func)args; 
#define DEFINEFUNC_LEGACY_1_0(ret, func, args, argscall)       DEFINEFUNC(ret, func, args, argscall)
#define DEFINEFUNC_LEGACY_1(ret, func, args, argscall)         DEFINEFUNC(ret, func, args, argscall)
#define DEFINEFUNC_1_1(ret, func, args, argscall)              DEFINEFUNC(ret, func, args, argscall)
#define DEFINEFUNC_3_0(ret, func, args, argscall)              DEFINEFUNC(ret, func, args, argscall)
#define DEFINEFUNC_RENAMED(ret, func, oldfunc, args, argscall) DEFINEFUNC(ret, func, args, argscall)

FOR_ALL_OPENSSL_FUNCTIONS

#undef DEFINEFUNC
#undef DEFINEFUNC_LEGACY_1_0
#undef DEFINEFUNC_LEGACY_1
#undef DEFINEFUNC_1_1
#undef DEFINEFUNC_3_0
#undef DEFINEFUNC_RENAMED

// Load all the functions stored in FOR_ALL_OPENSSL_FUNCTIONS
// and assign them to their corresponding function pointer
// defined in goopenssl.h.
void
go_openssl_load_functions(void* handle)
{
    int major = go_openssl_version_major(handle);
    int minor = go_openssl_version_minor(handle);

    // This function could be called concurrently from different Goroutines unless correctly locked.
    // If that happen there could be a race in DEFINEFUNC_RENAMED where the global function pointer is NULL,
    // then properly loaded, then goes back to NULL right before being used (then loaded again).
    // To avoid this situation only assign the function pointer when the function has been successfully
    // loaded in tmp_ptr.
    void* tmp_ptr;

#define DEFINEFUNC(ret, func, args, argscall) \
    _g_##func = dlsym(handle, #func);         \
    if (_g_##func == NULL) { fprintf(stderr, "Cannot get required symbol " #func " from libcrypto\n"); abort(); }
#define DEFINEFUNC_LEGACY_1_0(ret, func, args, argscall)  \
    if (major == 1 && minor == 0)                         \
    {                                                     \
        DEFINEFUNC(ret, func, args, argscall)             \
    }
#define DEFINEFUNC_LEGACY_1(ret, func, args, argscall)  \
    if (major == 1)                                     \
    {                                                   \
        DEFINEFUNC(ret, func, args, argscall)           \
    }
#define DEFINEFUNC_1_1(ret, func, args, argscall)     \
    if (major == 3 || (major == 1 && minor == 1))     \
    {                                                 \
        DEFINEFUNC(ret, func, args, argscall)         \
    }
#define DEFINEFUNC_3_0(ret, func, args, argscall)     \
    if (major == 3)                                   \
    {                                                 \
        DEFINEFUNC(ret, func, args, argscall)         \
    }
#define DEFINEFUNC_RENAMED(ret, func, oldfunc, args, argscall)                                              \
    tmp_ptr = dlsym(handle, #func);                                                                         \
    if (tmp_ptr == NULL)                                                                                    \
    {                                                                                                       \
        tmp_ptr = dlsym(handle, #oldfunc);                                                                  \
        if (tmp_ptr == NULL)                                                                                \
        {                                                                                                   \
            fprintf(stderr, "Cannot get required symbol " #func " nor " #oldfunc " from libcrypto\n");      \
            abort();                                                                                        \
        }                                                                                                   \
    }                                                                                                       \
    _g_##func = tmp_ptr;

FOR_ALL_OPENSSL_FUNCTIONS

#undef DEFINEFUNC
#undef DEFINEFUNC_LEGACY_1_0
#undef DEFINEFUNC_LEGACY_1
#undef DEFINEFUNC_1_1
#undef DEFINEFUNC_3_0
#undef DEFINEFUNC_RENAMED
}
