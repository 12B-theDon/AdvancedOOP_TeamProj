#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <string>
#include <vector>

namespace {
constexpr wchar_t UninstallKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
    L"{B21A42C6-0BCE-4C73-A734-1566AB3CD911}_is1";

bool ReadValue(HKEY hive, const wchar_t* name, std::wstring& value) {
    DWORD bytes = 0;
    if (RegGetValueW(hive, UninstallKey, name, RRF_RT_REG_SZ,
                    nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t) || bytes > 65536) {
        return false;
    }
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(hive, UninstallKey, name, RRF_RT_REG_SZ,
                    nullptr, buffer.data(), &bytes) != ERROR_SUCCESS) {
        return false;
    }
    value.assign(buffer.data());
    return true;
}

bool FindUninstaller(std::wstring& executable, bool& registered) {
    const HKEY hives[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    for (HKEY hive : hives) {
        std::wstring displayName;
        std::wstring command;
        if (!ReadValue(hive, L"DisplayName", displayName) ||
            (displayName != L"Fingerprint Evaluator" &&
             displayName.rfind(L"Fingerprint Evaluator ", 0) != 0)) {
            continue;
        }
        registered = true;
        if (!ReadValue(hive, L"UninstallString", command)) continue;
        int count = 0;
        wchar_t** arguments = CommandLineToArgvW(command.c_str(), &count);
        if (!arguments) continue;
        if (count == 1) executable.assign(arguments[0]);
        LocalFree(arguments);
        if (count != 1 || executable.size() < 4 ||
            executable[1] != L':' || executable[2] != L'\\') {
            continue;
        }
        const DWORD attributes = GetFileAttributesW(executable.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            return true;
        }
    }
    return false;
}

void ShowMessage(const wchar_t* korean, const wchar_t* english, UINT icon) {
    const bool useKorean = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN;
    MessageBoxW(nullptr, useKorean ? korean : english,
                L"Fingerprint Evaluator Uninstall", MB_OK | icon);
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::wstring executable;
    bool registered = false;
    if (!FindUninstaller(executable, registered)) {
        if (registered) {
            ShowMessage(L"제거 프로그램을 찾을 수 없습니다. install.exe로 복구 설치한 뒤 다시 실행하세요.",
                        L"The installed uninstaller is missing. Repair the installation with install.exe and try again.",
                        MB_ICONERROR);
        } else {
            ShowMessage(L"이 PC에서 현재 사용자의 Fingerprint Evaluator 설치를 찾을 수 없습니다.",
                        L"Fingerprint Evaluator is not installed for this user on this PC.",
                        MB_ICONINFORMATION);
        }
        return ERROR_NOT_FOUND;
    }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SHELLEXECUTEINFOW request{};
    request.cbSize = sizeof(request);
    request.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    request.lpVerb = L"open";
    request.lpFile = executable.c_str();
    request.nShow = SW_SHOWNORMAL;
    const BOOL launched = ShellExecuteExW(&request);
    const DWORD error = launched ? ERROR_SUCCESS : GetLastError();
    if (SUCCEEDED(initialized)) CoUninitialize();
    if (!launched) {
        ShowMessage(L"제거 창을 열지 못했습니다. Windows 설정의 앱 목록에서 Fingerprint Evaluator를 제거하세요.",
                    L"Could not open the uninstall window. Remove Fingerprint Evaluator from Windows Settings > Apps.",
                    MB_ICONERROR);
    }
    return static_cast<int>(error);
}
