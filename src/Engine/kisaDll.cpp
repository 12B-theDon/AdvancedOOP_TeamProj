#include "kisaDll.h"
#include "../Worker/Protocol.h"
#include "../Public/FvcSpec.h"
#include <QFileInfo>
#include <QString>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#endif

namespace engine {
struct KisaDllAlgorithm::State {
    std::string dll;
    std::string worker;
    DllOptions options;
    std::atomic<bool> cancelled{false};
    std::atomic<unsigned int> next{0};
#ifdef _WIN32
    struct Process {
        HANDLE process = nullptr;
        HANDLE input = nullptr;
        HANDLE output = nullptr;
        HANDLE job = nullptr;
        bool first = true;
        std::mutex mutex;
        ~Process() { Stop(); }
        void Stop() {
            if (job) { CloseHandle(job); job = nullptr; }
            if (process) { TerminateProcess(process, 1); WaitForSingleObject(process, 5000); CloseHandle(process); process = nullptr; }
            if (input) { CloseHandle(input); input = nullptr; }
            if (output) { CloseHandle(output); output = nullptr; }
            first = true;
        }
    };
    std::vector<std::unique_ptr<Process>> processes;
    bool Read(Process& process, void* destination, DWORD length, ULONGLONG deadline, std::string& error) {
        unsigned char* bytes = static_cast<unsigned char*>(destination);
        while (length > 0) {
            if (cancelled.load()) { error = "[CANCELLED] Stopped."; return false; }
            if (GetTickCount64() >= deadline) { error = "[TIMEOUT] Worker call exceeded its deadline."; return false; }
            DWORD available = 0;
            if (!PeekNamedPipe(process.output, nullptr, 0, nullptr, &available, nullptr)) { error = "[CRASH] Worker pipe closed."; return false; }
            if (available > 0) {
                DWORD received = 0;
                if (!ReadFile(process.output, bytes, std::min(length, available), &received, nullptr) || received == 0) { error = "[CRASH] Worker response interrupted."; return false; }
                bytes += received; length -= received;
            } else {
                if (WaitForSingleObject(process.process, 0) == WAIT_OBJECT_0) { error = "[CRASH] Worker exited."; return false; }
                Sleep(5);
            }
        }
        return true;
    }
    bool Write(Process& process, const void* source, DWORD length, ULONGLONG deadline, std::string& error) {
        if (length == 0) return true;
        OVERLAPPED operation = {};
        operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!operation.hEvent) { error = "[FAIL] Cannot create IPC write event."; return false; }
        DWORD sent = 0;
        const BOOL complete = WriteFile(process.input, source, length, &sent, &operation);
        if (!complete && GetLastError() != ERROR_IO_PENDING) {
            CloseHandle(operation.hEvent); error = "[CRASH] Worker request pipe closed."; return false;
        }
        while (!complete && WaitForSingleObject(operation.hEvent, 5) == WAIT_TIMEOUT) {
            if (cancelled.load() || GetTickCount64() >= deadline) {
                error = cancelled.load() ? "[CANCELLED] Stopped." : "[TIMEOUT] Worker request exceeded its deadline.";
                CancelIoEx(process.input, &operation);
                // The OVERLAPPED structure must outlive cancelled I/O completion.
                GetOverlappedResult(process.input, &operation, &sent, TRUE);
                CloseHandle(operation.hEvent); return false;
            }
        }
        const BOOL written = GetOverlappedResult(process.input, &operation, &sent, TRUE);
        CloseHandle(operation.hEvent);
        if (!written || sent != length) { error = "[CRASH] Worker request interrupted."; return false; }
        return true;
    }
    bool CreateInput(Process& process, HANDLE& childInput, SECURITY_ATTRIBUTES& security, std::string& error) {
        static std::atomic<unsigned int> sequence{0};
        const std::wstring name = L"\\\\.\\pipe\\FingerprintWorker-" + std::to_wstring(GetCurrentProcessId())
            + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(sequence.fetch_add(1));
        process.input = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, nullptr);
        if (process.input == INVALID_HANDLE_VALUE) { process.input = nullptr; error = "[FAIL] Cannot create worker input."; return false; }
        OVERLAPPED connection = {}; connection.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!connection.hEvent) { error = "[FAIL] Cannot create IPC connection event."; return false; }
        const BOOL connected = ConnectNamedPipe(process.input, &connection);
        const DWORD status = connected ? ERROR_SUCCESS : GetLastError();
        if (!connected && status != ERROR_IO_PENDING) {
            CloseHandle(connection.hEvent); error = "[FAIL] Cannot connect worker input."; return false;
        }
        childInput = CreateFileW(name.c_str(), GENERIC_READ, 0, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (childInput == INVALID_HANDLE_VALUE) {
            childInput = nullptr; CancelIoEx(process.input, &connection);
            DWORD ignored = 0; GetOverlappedResult(process.input, &connection, &ignored, TRUE);
            CloseHandle(connection.hEvent); error = "[FAIL] Cannot open worker input."; return false;
        }
        DWORD ignored = 0;
        const BOOL finished = connected || GetOverlappedResult(process.input, &connection, &ignored, TRUE);
        CloseHandle(connection.hEvent);
        if (!finished) { CloseHandle(childInput); childInput = nullptr; error = "[FAIL] Cannot finish IPC connection."; return false; }
        return true;
    }
    bool Start(Process& process, std::string& error) {
        SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE childInput = nullptr, childOutput = nullptr;
        if (!CreateInput(process, childInput, security, error)) return false;
        if (!CreatePipe(&process.output, &childOutput, &security, 0)) { CloseHandle(childInput); error = "[FAIL] Cannot create worker output."; return false; }
        SetHandleInformation(process.input, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(process.output, HANDLE_FLAG_INHERIT, 0);
        const std::wstring executable = QString::fromStdString(worker).toStdWString();
        std::wstring command = L"\"" + executable + L"\" \"" + QString::fromStdString(dll).toStdWString() + L"\" "
            + std::to_wstring(reinterpret_cast<std::uintptr_t>(childInput)) + L" "
            + std::to_wstring(reinterpret_cast<std::uintptr_t>(childOutput));
        STARTUPINFOW startup = {}; startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        const HANDLE nullError = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
        if (nullError == INVALID_HANDLE_VALUE) { CloseHandle(childInput); CloseHandle(childOutput); error = "[FAIL] Cannot isolate worker standard streams."; return false; }
        startup.hStdInput = startup.hStdOutput = startup.hStdError = nullError;
        PROCESS_INFORMATION info = {};
        // Serialize process creation so another simultaneous launch cannot inherit
        // these temporary child pipe handles and keep a dead worker's pipe alive.
        const BOOL created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &info);
        CloseHandle(childInput); CloseHandle(childOutput);
        if (nullError != INVALID_HANDLE_VALUE) CloseHandle(nullError);
        if (!created) { error = "[FAIL] Cannot start FingerprintWorker.exe."; return false; }
        process.process = info.hProcess;
        process.job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!process.job || !SetInformationJobObject(process.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))
            || !AssignProcessToJobObject(process.job, process.process)) {
            CloseHandle(info.hThread); error = "[FAIL] Cannot isolate worker in a Windows job."; return false;
        }
        ResumeThread(info.hThread); CloseHandle(info.hThread);
        ipc::Response ready;
        if (!Read(process, &ready, sizeof(ready), GetTickCount64() + options.startup_timeout_ms, error)) return false;
        if (ready.magic != ipc::Magic || ready.version != ipc::Version || !std::memchr(ready.message, '\0', sizeof(ready.message))) { error = "[MISMATCH] Invalid worker handshake."; return false; }
        if (ready.result != 1) { error = std::string("[FAIL] ") + ready.message; return false; }
        return true;
    }
    bool Call(ipc::Request request, const std::vector<unsigned char>& first, const std::vector<unsigned char>& second,
        std::vector<unsigned char>& bytes, double& score, double& time_ms, std::string& error) {
        Process& process = *processes[next.fetch_add(1) % processes.size()];
        std::lock_guard<std::mutex> lock(process.mutex);
        if (cancelled.load()) { error = "[CANCELLED] Stopped."; return false; }
        if (!process.process) {
            static std::mutex launches;
            std::lock_guard<std::mutex> launchLock(launches);
            if (!Start(process, error)) { process.Stop(); return false; }
        }
        const int timeout = process.first ? std::max(options.timeout_ms, options.first_call_timeout_ms) : options.timeout_ms;
        const ULONGLONG deadline = GetTickCount64() + timeout;
        request.first_size = static_cast<std::uint32_t>(first.size()); request.second_size = static_cast<std::uint32_t>(second.size());
        ipc::Response response;
        if (!Write(process, &request, sizeof(request), deadline, error) || !Write(process, first.data(), request.first_size, deadline, error)
            || !Write(process, second.data(), request.second_size, deadline, error) || !Read(process, &response, sizeof(response), deadline, error)) { process.Stop(); return false; }
        process.first = false;
        const bool comparison = request.operation == ipc::Operation::Compare;
        if (response.magic != ipc::Magic || response.version != ipc::Version || !std::memchr(response.message, '\0', sizeof(response.message))
            || !std::isfinite(response.call_time_ms) || response.call_time_ms < 0
            || (response.result != 0 && response.result != 1) || response.data_size >= kisa::TemplateCapacity
            || (comparison && response.data_size != 0)
            || (response.status == ipc::Status::Normal && response.result == 1 && (comparison ? !std::isfinite(response.score) || response.score < 0 || response.score > 1 : response.data_size == 0))) {
            error = "[MISMATCH] Invalid DLL output or IPC response."; process.Stop(); return false;
        }
        time_ms = response.call_time_ms;
        if (response.status != ipc::Status::Normal) {
            error = response.status == ipc::Status::Crash ? "[CRASH] DLL raised a structured exception." : "[MISMATCH] DLL violated its ABI.";
            process.Stop(); return false;
        }
        if (!response.result) { error = "[FAIL] DLL error " + std::to_string(response.error_code) + ": " + response.message; return false; }
        bytes.resize(response.data_size);
        if (!Read(process, bytes.data(), response.data_size, deadline, error)) { bytes.clear(); process.Stop(); return false; }
        score = response.score; return true;
    }
#else
    bool Call(ipc::Request, const std::vector<unsigned char>&, const std::vector<unsigned char>&,
        std::vector<unsigned char>&, double&, double&, std::string& error) {
        error = "[FAIL] DLL evaluation requires Windows; training and report tests are portable."; return false;
    }
#endif
};
KisaDllAlgorithm::KisaDllAlgorithm(const std::string& dll, const std::string& worker, const DllOptions& options) : state_(new State) {
    state_->dll = QFileInfo(QString::fromStdString(dll)).absoluteFilePath().toStdString();
    state_->worker = QFileInfo(QString::fromStdString(worker)).absoluteFilePath().toStdString();
    state_->options = options;
    state_->options.workers = std::clamp(options.workers, 1, MaxConcurrency);
    state_->options.timeout_ms = std::max(1, options.timeout_ms);
    state_->options.startup_timeout_ms = std::max(1, options.startup_timeout_ms);
#ifdef _WIN32
    for (int i = 0; i < state_->options.workers; ++i) state_->processes.emplace_back(new State::Process);
#endif
}
KisaDllAlgorithm::~KisaDllAlgorithm() = default;
void KisaDllAlgorithm::Cancel() { state_->cancelled.store(true); }
void KisaDllAlgorithm::ResetCancel() {
#ifdef _WIN32
    if (state_->cancelled.load()) for (const std::unique_ptr<State::Process>& process : state_->processes) { std::lock_guard<std::mutex> lock(process->mutex); process->Stop(); }
#endif
    state_->cancelled.store(false);
}
bool KisaDllAlgorithm::Extract(const fingerprint::Image& image, bool enrollment, std::vector<unsigned char>& bytes, std::string& error) {
    bytes.clear(); error.clear();
    const int type = kisa::ImageType(image.database_id, image.width, image.height);
    if (type == 0 || image.pixels.size() != std::size_t(image.width) * image.height) { error = "[FAIL] Image does not match FVC iType and dimensions."; return false; }
    ipc::Request request; request.type = static_cast<std::uint32_t>(type);
    request.operation = enrollment ? ipc::Operation::Enroll : ipc::Operation::Attempt;
    double score = 0, time = 0;
    return state_->Call(request, image.pixels, {}, bytes, score, time, error);
}
bool KisaDllAlgorithm::Compare(const std::vector<unsigned char>& enrollment, const std::vector<unsigned char>& attempt, double& score, std::string& error, double* time_ms) {
    score = 0; error.clear();
    if (enrollment.empty() || attempt.empty() || enrollment.size() >= kisa::TemplateCapacity || attempt.size() >= kisa::TemplateCapacity) { error = "[MISMATCH] Invalid opaque template length."; return false; }
    ipc::Request request; request.operation = ipc::Operation::Compare;
    std::vector<unsigned char> bytes;
    double duration = -1;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const bool success = state_->Call(request, enrollment, attempt, bytes, score, duration, error);
    if (time_ms) *time_ms = duration >= 0 ? duration
        : std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return success;
}
}
