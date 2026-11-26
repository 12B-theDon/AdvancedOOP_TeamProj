#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>

namespace fs = std::filesystem;
static std::wstring Quote(const std::wstring& text) {
    std::wstring result = L"\""; std::size_t slashes = 0;
    for (wchar_t c : text) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'\"' ? 2 : 1), L'\\'); slashes = 0;
        if (c == L'\"') result += L'\\';
        result += c;
    }
    result.append(slashes * 2, L'\\'); return result + L'\"';
}
static fs::path Executable() {
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) throw std::runtime_error("Cannot locate install.exe.");
    return fs::path(std::wstring(buffer.data(), length));
}
static fs::path LocalData() {
    PWSTR text = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &text))) throw std::runtime_error("Cannot locate LocalAppData.");
    fs::path result(text); CoTaskMemFree(text); return result / L"FingerprintInstaller";
}
static DWORD Run(const fs::path& program, const std::vector<std::wstring>& args, bool wait) {
    std::wstring command = Quote(program.wstring());
    for (const std::wstring& arg : args) command += L" " + Quote(arg);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
        throw std::runtime_error("Could not start the installer process. Windows error: " + std::to_string(GetLastError()));
    DWORD code = 0;
    if (wait) { WaitForSingleObject(process.hProcess, INFINITE); GetExitCodeProcess(process.hProcess, &code); }
    CloseHandle(process.hThread); CloseHandle(process.hProcess); return code;
}
static void Licenses() {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(102), RT_RCDATA);
    if (!resource) { std::cout << "MSVC build: see the Visual Studio redistributable terms and docs/THIRD_PARTY.md.\n"; return; }
    HGLOBAL loaded = LoadResource(nullptr, resource); const void* bytes = loaded ? LockResource(loaded) : nullptr;
    if (!bytes) throw std::runtime_error("Cannot read embedded notices.");
    DWORD count = 0; const DWORD size = SizeofResource(nullptr, resource);
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes, size, &count, nullptr) || count != size)
        throw std::runtime_error("Cannot display notices.");
}
static void Extract(const fs::path& folder) {
    fs::create_directories(folder);
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!resource) throw std::runtime_error("Embedded bootstrap script is missing.");
    DWORD size = SizeofResource(nullptr, resource); HGLOBAL loaded = LoadResource(nullptr, resource);
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    if (!bytes || size == 0) throw std::runtime_error("Cannot read embedded bootstrap script.");
    HANDLE file = CreateFileW((folder / L"bootstrap_windows.ps1").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create the bootstrap script.");
    DWORD written = 0; BOOL ok = WriteFile(file, bytes, size, &written, nullptr); CloseHandle(file);
    if (!ok || written != size) throw std::runtime_error("Cannot write the bootstrap script.");
}
static void LogNativeFailure(const fs::path& path, const std::string& text) noexcept {
    if (path.empty()) return;
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0; WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr); CloseHandle(file);
}
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8); bool pause = true;
    fs::path errorLog; std::string step = "Read installer arguments";
    try {
        bool runner = false; fs::path source, hint, origin, runFolder;
        for (int i = 1; i < argc; ++i) {
            std::wstring arg(argv[i]);
            if (arg == L"--licenses") { Licenses(); return 0; }
            else if (arg == L"--no-pause") pause = false;
            else if (arg == L"--runner") runner = true;
            else if (arg == L"--help") { std::wcout << L"install.exe [--source <checkout>] [--no-pause]\ninstall.exe --extract-only <folder>\ninstall.exe --licenses\n"; return 0; }
            else if (arg == L"--extract-only" && i + 1 < argc) { Extract(fs::path(argv[++i])); return 0; }
            else if (arg == L"--source" && i + 1 < argc) source = fs::absolute(fs::path(argv[++i]));
            else if (arg == L"--source-hint" && i + 1 < argc) hint = fs::path(argv[++i]);
            else if (arg == L"--origin" && i + 1 < argc) origin = fs::path(argv[++i]);
            else throw std::runtime_error("Unknown or incomplete argument. Use install.exe --help.");
        }
        step = "Locate the installer and user work folder";
        const fs::path self = Executable(), base = LocalData();
        fs::create_directories(base / L"logs"); errorLog = base / L"logs/native_installation.log";
        if (!runner) {
            SYSTEMTIME now{}; GetLocalTime(&now); wchar_t name[80]{};
            swprintf_s(name, L"%04u%02u%02u_%02u%02u%02u_%lu", now.wYear, now.wMonth, now.wDay,
                       now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
            step = "Copy the installer to its independent run folder";
            runFolder = base / L"runs" / name; fs::create_directories(runFolder);
            errorLog = runFolder / L"installation.log";
            fs::path copy = runFolder / L"installer-runner.exe";
            fs::copy_file(self, copy, fs::copy_options::none);
            std::vector<std::wstring> args{L"--runner", L"--origin", self.wstring(), L"--source-hint", self.parent_path().wstring()};
            if (!source.empty()) { args.push_back(L"--source"); args.push_back(source.wstring()); }
            if (!pause) args.push_back(L"--no-pause");
            Run(copy, args, false);
            return 0; // Release the original install.exe before deployment starts.
        }
        runFolder = self.parent_path();
        if (origin.empty() || !fs::equivalent(runFolder.parent_path(), base / L"runs")) throw std::runtime_error("Invalid installer runner location.");
        errorLog = runFolder / L"installation.log"; step = "Extract the embedded bootstrap";
        Extract(runFolder);
        std::vector<wchar_t> windows(32768); UINT length = GetSystemDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
        if (length == 0 || length >= windows.size()) throw std::runtime_error("Cannot locate Windows PowerShell.");
        fs::path powershell = fs::path(std::wstring(windows.data(), length)) / L"WindowsPowerShell/v1.0/powershell.exe";
        fs::path log = runFolder / L"installation.log";
        std::vector<std::wstring> args{L"-NoLogo", L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-File",
            (runFolder / L"bootstrap_windows.ps1").wstring(), L"-SourceHint", hint.wstring(),
            L"-OriginExecutable", origin.wstring(), L"-RunnerExecutable", self.wstring(), L"-LogPath", log.wstring()};
        if (!source.empty()) { args.push_back(L"-SourcePath"); args.push_back(source.wstring()); }
        step = "Start Windows PowerShell bootstrap";
        DWORD code = Run(powershell, args, true);
        if (code != 0) {
            std::wcerr << L"\nInstallation failed. Details remain in: " << log.wstring() << L"\n";
            if (pause) { std::wcout << L"Press Enter to close.\n"; std::wstring line; std::getline(std::wcin, line); }
        }
        return static_cast<int>(code);
    } catch (const std::exception& error) {
        const std::string message = "Installation failed at " + step + ": " + error.what() + "\n";
        LogNativeFailure(errorLog, message); std::cerr << message;
        if (!errorLog.empty()) std::wcerr << L"Log: " << errorLog.wstring() << L"\n";
        if (pause) { std::cout << "Press Enter to close.\n"; std::string line; std::getline(std::cin, line); }
        return 1;
    }
}
