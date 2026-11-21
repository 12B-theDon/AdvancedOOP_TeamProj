#include "../Public/BioApi.h"
#include "../Public/FvcSpec.h"
#include <windows.h>

namespace {
HMODULE selfModule = nullptr;
HMODULE coreModule = nullptr;
INIT_ONCE initialized = INIT_ONCE_STATIC_INIT;
kisa::ExtractFunction enrollFunction = nullptr;
kisa::ExtractFunction attemptFunction = nullptr;
kisa::CompareFunction compareFunction = nullptr;

BOOL CALLBACK LoadCore(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[32768] = {};
    const DWORD size = GetModuleFileNameW(selfModule, path, 32768);
    if (size == 0 || size >= 32768) return TRUE;
    DWORD slash = size;
    while (slash > 0 && path[slash - 1] != L'\\') --slash;
    const wchar_t name[] = L"FingerprintAlgorithmCore.dll";
    if (slash + sizeof(name) / sizeof(wchar_t) >= 32768) return TRUE;
    for (DWORD i = 0; i < sizeof(name) / sizeof(wchar_t); ++i) path[slash + i] = name[i];
    coreModule = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (coreModule) {
        enrollFunction = reinterpret_cast<kisa::ExtractFunction>(GetProcAddress(coreModule, "CoreEnroll"));
        attemptFunction = reinterpret_cast<kisa::ExtractFunction>(GetProcAddress(coreModule, "CoreAttempt"));
        compareFunction = reinterpret_cast<kisa::CompareFunction>(GetProcAddress(coreModule, "CoreCompare"));
    }
    return TRUE;
}
int CoreError(int* code, char* message) {
    if (code) *code = static_cast<int>(kisa::Error::CoreLoad);
    if (message) {
        const char text[] = "Cannot load sibling FingerprintAlgorithmCore.dll or its dependencies.";
        unsigned int i = 0;
        for (; i + 1 < sizeof(text); ++i) message[i] = text[i];
        message[i] = '\0';
    }
    return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) selfModule = module;
    // Never load Torch, wait for threads, or unload dependencies under loader lock.
    return TRUE;
}
int KISA_CALL KISA_Enroll(int type, unsigned char* image, unsigned char* data, int* size, int* code, char* message) {
    if (size) *size = 0;
    InitOnceExecuteOnce(&initialized, LoadCore, nullptr, nullptr);
    if (!enrollFunction) return CoreError(code, message);
    return enrollFunction(type, image, data, size, code, message);
}
int KISA_CALL KISA_Attempt(int type, unsigned char* image, unsigned char* data, int* size, int* code, char* message) {
    if (size) *size = 0;
    InitOnceExecuteOnce(&initialized, LoadCore, nullptr, nullptr);
    if (!attemptFunction) return CoreError(code, message);
    return attemptFunction(type, image, data, size, code, message);
}
int KISA_CALL KISA_Compare(unsigned char* enrollment, unsigned char* attempt, double* score, int* code, char* message) {
    if (score) *score = 0;
    InitOnceExecuteOnce(&initialized, LoadCore, nullptr, nullptr);
    if (!compareFunction) return CoreError(code, message);
    return compareFunction(enrollment, attempt, score, code, message);
}
