#pragma once

// K-NBTC fingerprint DLL interface ("생체인식시스템 시험·인증 안내서" 25.11, pp.26-29).
// Every function returns 1 on success and 0 on failure; on failure piErrorCode and
// arErrorMessage describe the reason.

#ifdef _WIN32
#  ifdef _KISA_IMPORT
#    define KISADLL extern "C" __declspec(dllimport)
#  else
#    define KISADLL extern "C" __declspec(dllexport)
#  endif
#else
#  define KISADLL extern "C" __attribute__((visibility("default")))
#endif

// Template of an enrollment image.
// iType: 1 = FVC2000_DB3_B (448x478), 2 = FVC2002_DB3_B (300x300), 3 = FVC2004_DB1_B (640x480).
// pImage: raw gray8 pixels, row-major from the top-left pixel, width*height bytes, no header.
// pEnroll: caller-allocated buffer; the template is smaller than 1,000,000 bytes.
KISADLL int KISA_Enroll(int iType, unsigned char* pImage, unsigned char* pEnroll, int* piEnrollSize,
                        int* piErrorCode, char arErrorMessage[256]);

// Same as KISA_Enroll for an attempt (probe) image.
KISADLL int KISA_Attempt(int iType, unsigned char* pImage, unsigned char* pAttempt, int* piAttemptSize,
                         int* piErrorCode, char arErrorMessage[256]);

// pdScore: 0 (different) to 1 (same). Both templates must come from the same iType.
KISADLL int KISA_Compare(unsigned char* pEnroll, unsigned char* pAttempt, double* pdScore,
                         int* piErrorCode, char arErrorMessage[256]);

// Function pointer types for LoadLibrary/GetProcAddress callers.
typedef int (*KISA_ENROLL_FN)(int, unsigned char*, unsigned char*, int*, int*, char*);
typedef int (*KISA_ATTEMPT_FN)(int, unsigned char*, unsigned char*, int*, int*, char*);
typedef int (*KISA_COMPARE_FN)(unsigned char*, unsigned char*, double*, int*, char*);
