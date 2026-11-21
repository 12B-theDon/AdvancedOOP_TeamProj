#pragma once

// K-NBTC fingerprint ABI (guide pp.26–29, fingerprint IndustryAlgorithm example).
// cdecl, C linkage, 1=success / 0=failure. Caller provides a 1,000,000-byte
// output buffer and a 256-byte error buffer. Compare has no length parameters.
#ifndef _WIN32
#error "The fingerprint KISA API requires Windows."
#endif
#define KISA_CALL __cdecl
#ifdef KISA_EXPORTS
#define KISA_API extern "C" __declspec(dllexport)
#else
#define KISA_API extern "C"
#endif

KISA_API int KISA_CALL KISA_Enroll(int iType, unsigned char* pImage,
    unsigned char* pEnroll, int* piEnrollSize, int* piErrorCode, char arErrorMessage[256]);
KISA_API int KISA_CALL KISA_Attempt(int iType, unsigned char* pImage,
    unsigned char* pAttempt, int* piAttemptSize, int* piErrorCode, char arErrorMessage[256]);
KISA_API int KISA_CALL KISA_Compare(unsigned char* pEnroll, unsigned char* pAttempt,
    double* pdScore, int* piErrorCode, char arErrorMessage[256]);

namespace kisa {
using ExtractFunction = int (KISA_CALL *)(int, unsigned char*, unsigned char*, int*, int*, char*);
using CompareFunction = int (KISA_CALL *)(unsigned char*, unsigned char*, double*, int*, char*);
}
