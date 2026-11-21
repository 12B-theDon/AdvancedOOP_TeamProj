#include "Protocol.h"
#include "../Public/BioApi.h"
#include "../Public/FvcSpec.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <vector>

namespace {
bool Read(HANDLE pipe, void* output, DWORD size) {
    unsigned char* data = static_cast<unsigned char*>(output);
    while (size > 0) {
        DWORD received = 0;
        if (!ReadFile(pipe, data, size, &received, nullptr) || received == 0) return false;
        data += received; size -= received;
    }
    return true;
}
bool Write(HANDLE pipe, const void* input, DWORD size) {
    const unsigned char* data = static_cast<const unsigned char*>(input);
    while (size > 0) {
        DWORD sent = 0;
        if (!WriteFile(pipe, data, size, &sent, nullptr) || sent == 0) return false;
        data += sent; size -= sent;
    }
    return true;
}
// Keep SEH in a function without C++ objects requiring stack unwinding.
bool Invoke(kisa::ExtractFunction extract, kisa::CompareFunction compare, const ipc::Request* request,
    unsigned char* first, unsigned char* second, unsigned char* output, int* size, ipc::Response* response) {
    __try {
        if (request->operation == ipc::Operation::Compare)
            response->result = compare(first, second, &response->score, &response->error_code, response->message);
        else response->result = extract(static_cast<int>(request->type), first, output, size, &response->error_code, response->message);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    wchar_t* inputEnd = nullptr; wchar_t* outputEnd = nullptr;
    const std::uintptr_t inputValue = static_cast<std::uintptr_t>(std::wcstoull(argv[2], &inputEnd, 10));
    const std::uintptr_t outputValue = static_cast<std::uintptr_t>(std::wcstoull(argv[3], &outputEnd, 10));
    if (!inputValue || !outputValue || *inputEnd != L'\0' || *outputEnd != L'\0') return 2;
    const HANDLE input = reinterpret_cast<HANDLE>(inputValue), output = reinterpret_cast<HANDLE>(outputValue);
    if (!SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0) || !SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0)) return 2;
    // IPC has dedicated handles: foreign DLL stdout/stderr cannot corrupt replies.
    const HMODULE library = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    const kisa::ExtractFunction enroll = library ? reinterpret_cast<kisa::ExtractFunction>(GetProcAddress(library, "KISA_Enroll")) : nullptr;
    const kisa::ExtractFunction attempt = library ? reinterpret_cast<kisa::ExtractFunction>(GetProcAddress(library, "KISA_Attempt")) : nullptr;
    const kisa::CompareFunction compare = library ? reinterpret_cast<kisa::CompareFunction>(GetProcAddress(library, "KISA_Compare")) : nullptr;
    ipc::Response ready;
    ready.result = enroll && attempt && compare ? 1 : 0;
    if (!ready.result) std::strcpy(ready.message, "Cannot load DLL or required undecorated KISA exports; check bitness and dependencies.");
    if (!Write(output, &ready, sizeof(ready)) || !ready.result) return 3;
    for (;;) {
        ipc::Request request;
        if (!Read(input, &request, sizeof(request))) break;
        const bool comparison = request.operation == ipc::Operation::Compare;
        const kisa::FvcSpec* spec = kisa::Find(static_cast<int>(request.type));
        if (request.magic != ipc::Magic || request.version != ipc::Version
            || (request.operation != ipc::Operation::Enroll && request.operation != ipc::Operation::Attempt && !comparison)
            || (comparison && (request.first_size == 0 || request.first_size >= kisa::TemplateCapacity || request.second_size == 0 || request.second_size >= kisa::TemplateCapacity))
            || (!comparison && (!spec || request.first_size != std::uint32_t(spec->width * spec->height) || request.second_size != 0))) return 4;
        // Template buffers are opaque. Zero padding supports the ABI's lengthless
        // Compare; only actual returned bytes cross IPC or enter saved templates.
        const std::size_t firstCapacity = comparison ? kisa::TemplateCapacity : request.first_size;
        const std::size_t secondCapacity = comparison ? kisa::TemplateCapacity : 0;
        constexpr int GuardSize = 64;
        std::vector<unsigned char> first(firstCapacity + GuardSize, 0), second(secondCapacity + GuardSize, 0);
        std::vector<unsigned char> data(kisa::TemplateCapacity + GuardSize, 0);
        std::fill(first.end() - GuardSize, first.end(), 0xa5);
        std::fill(second.end() - GuardSize, second.end(), 0xa5);
        std::fill(data.end() - GuardSize, data.end(), 0xa5);
        if (!Read(input, first.data(), request.first_size) || !Read(input, second.data(), request.second_size)) break;
        ipc::Response response; int size = 0;
        const kisa::ExtractFunction extract = request.operation == ipc::Operation::Enroll ? enroll : attempt;
        const std::chrono::steady_clock::time_point callStart = std::chrono::steady_clock::now();
        if (!Invoke(extract, compare, &request, first.data(), second.data(), data.data(), &size, &response)) response.status = ipc::Status::Crash;
        response.call_time_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callStart).count();
        const bool guards = std::all_of(first.end() - GuardSize, first.end(), [](unsigned char value) { return value == 0xa5; })
            && std::all_of(second.end() - GuardSize, second.end(), [](unsigned char value) { return value == 0xa5; })
            && std::all_of(data.end() - GuardSize, data.end(), [](unsigned char value) { return value == 0xa5; });
        if (response.status == ipc::Status::Normal && (!guards || !std::memchr(response.message, '\0', kisa::ErrorCapacity)
            || (response.result != 0 && response.result != 1)
            || (response.result == 1 && (comparison ? !std::isfinite(response.score) || response.score < 0 || response.score > 1 : size <= 0 || size >= kisa::TemplateCapacity))))
            response.status = ipc::Status::Mismatch;
        response.message[kisa::ErrorCapacity - 1] = '\0';
        if (response.status == ipc::Status::Normal && response.result == 1 && !comparison) response.data_size = static_cast<std::uint32_t>(size);
        if (!Write(output, &response, sizeof(response)) || !Write(output, data.data(), response.data_size)) break;
        // An exception or bad ABI output leaves unknown DLL state. Restart next call.
        if (response.status != ipc::Status::Normal) return 5;
    }
    // Process teardown owns all DLL/runtime cleanup; do not call foreign destructors.
    return 0;
}
