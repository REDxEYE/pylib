//
// Created by AORUS on 13.07.2022.
//

#pragma once


#if defined(_MSC_VER)
//  Microsoft
    #if defined(LIB)
        #define  DLL_EXPORT   extern "C"  __declspec(dllexport)
        #define  DLL_IMPORT   extern "C"  __declspec(dllimport)
    #else
        #define  DLL_EXPORT   __declspec(dllexport)
        #define  DLL_IMPORT   __declspec(dllimport)
    #endif
#elif defined(__GNUC__)
//  GCC
    #if defined(LIB)
        #define DLL_EXPORT   extern "C"  __attribute__((visibility("default")))
        #define DLL_IMPORT
    #else
        #define DLL_EXPORT  __attribute__((visibility("default")))
        #define DLL_IMPORT
    #endif
#else
//  do nothing and hope for the best?
#define DLL_EXPORT
#define DLL_IMPORT
#pragma warning Unknown dynamic link import/export semantics.
#endif

#define BIT_MASK(bit_count) ((1 << (bit_count)) - 1)
#define SET_BITS(target, bit_offset, bit_count, value) target|= (((value)&BIT_MASK(bit_count)) << (bit_offset))

#define SWAP(TYPE, A, B)  do{TYPE TMP=A;A=B;B=TMP;}while(0)

enum ERROR_CODES {
    NO_ERROR,
    INPUT_BUFFER_SMALL,
    OUTPUT_BUFFER_SMALL,
};