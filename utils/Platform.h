/*=====================================================================
Platform.h
-------------------
Copyright Glare Technologies Limited 2014 -
=====================================================================*/
#pragma once


#if defined(_WIN32) && !defined(__MINGW32__)
#define INDIGO_STRONG_INLINE __forceinline
#else
#define INDIGO_STRONG_INLINE inline
#endif


// To disallow copy-construction and assignment operators, put this in the private part of a class:
#define INDIGO_DISABLE_COPY(TypeName) \
	TypeName(const TypeName &); \
	TypeName &operator=(const TypeName &);


// Declare a variable as used.  This can be used to avoid the Visual Studio 'local variable is initialized but not referenced' warning.
#ifdef _MSC_VER
#define GLARE_DECLARE_USED(x) (x)
#else
#define GLARE_DECLARE_USED(x) (void)(x);
#endif

// This is like the usual assert() macro, except in NDEBUG mode ('release' mode) it marks the variable as used by writing the expression as a statement.
// NOTE: Use with care as the expression may have side effects.
#ifdef NDEBUG
#define assertOrDeclareUsed(expr) (expr)
#else
#define assertOrDeclareUsed(expr) assert(expr)
#endif


//Compiler Definitiosn
//#define COMPILER_GCC
//#define COMPILER_MSVC 1
//#define COMPILER_MSVC_6
//#define COMPILER_MSVC_2003 1

#ifdef _MSC_VER
#define COMPILER_MSVC 1
#endif


// Workaround for MSVC not including stdint.h for fixed-width int types
#ifdef _MSC_VER
typedef signed __int8		int8_t;
typedef signed __int16		int16_t;
typedef signed __int32		int32_t;
typedef signed __int64		int64_t;
typedef unsigned __int8		uint8_t;
typedef unsigned __int16	uint16_t;
typedef unsigned __int32	uint32_t;
typedef unsigned __int64	uint64_t;
#else
#include <stdint.h>
#endif

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
