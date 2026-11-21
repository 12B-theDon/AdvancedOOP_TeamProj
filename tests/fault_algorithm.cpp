#include "../src/Public/BioApi.h"
#include "../src/Public/FvcSpec.h"
#include <windows.h>
#include <cmath>
#include <cstring>
#include <limits>
#ifdef _MSC_VER
#include <intrin.h>
#endif
namespace {
DWORD WINAPI PauseWorker(void* thread) {
    Sleep(50); SuspendThread(static_cast<HANDLE>(thread)); CloseHandle(static_cast<HANDLE>(thread)); return 0;
}
int Extract(int type, unsigned char* image, unsigned char* data, int* size, int* code, char* message, unsigned char role) {
    *size = 0; *code = 0; message[0] = '\0';
    switch (image[0]) {
    case 1: *code = 42; std::strcpy(message, "intentional failure"); return 0;
    case 2: Sleep(2000); break;
    case 3: *static_cast<volatile int*>(nullptr) = 1; break;
    case 4:
#ifdef _MSC_VER
        __fastfail(7);
#else
        TerminateProcess(GetCurrentProcess(), 7);
#endif
        break;
    case 5: return 1;
    case 6: *size = kisa::TemplateCapacity; return 1;
    case 7: return 2;
    case 10: data[kisa::TemplateCapacity] = 0; break;
    case 11: std::memset(message, 'x', 256); break;
    case 16: {
        const char log[] = "foreign DLL log\n"; DWORD written = 0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), log, sizeof(log) - 1, &written, nullptr);
        break;
    }
    case 15: {
        const HANDLE current = OpenThread(THREAD_SUSPEND_RESUME, FALSE, GetCurrentThreadId());
        const HANDLE pause = CreateThread(nullptr, 0, PauseWorker, current, 0, nullptr);
        if (pause) CloseHandle(pause); else if (current) CloseHandle(current);
        break;
    }
    }
    data[0] = image[0]; data[1] = role; data[2] = static_cast<unsigned char>(type);
    data[3] = image[kisa::Find(type)->width * kisa::Find(type)->height - 1];
    *size = 4;
    return 1;
}
}
int KISA_CALL KISA_Enroll(int type, unsigned char* image, unsigned char* data, int* size, int* code, char* message) {
    return Extract(type, image, data, size, code, message, 1);
}
int KISA_CALL KISA_Attempt(int type, unsigned char* image, unsigned char* data, int* size, int* code, char* message) {
    return Extract(type, image, data, size, code, message, 2);
}
int KISA_CALL KISA_Compare(unsigned char* a, unsigned char* b, double* score, int* code, char* message) {
    *code = 0; message[0] = '\0';
    if (a[1] != 1 || b[1] != 2 || a[2] != b[2] || a[3] != 201 || b[3] != 201) { *code = 43; return 0; }
    if (b[0] == 8) { *score = 1.1; return 1; }
    if (b[0] == 9) { *score = std::numeric_limits<double>::quiet_NaN(); return 1; }
    if (b[0] == 12) Sleep(2000);
    if (b[0] == 13) { *code = 44; return 0; }
    *score = (a[0] + b[0]) / 100.0; return 1;
}
