#include "platform/win/WinAutostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QString>

#include <windows.h>

namespace {

// Shared identity of the two autostart forms.
const wchar_t* kRunSubKey   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kValueName   = L"SnapCap";
const QString  kTaskName    = QStringLiteral("SnapCap");

QString exePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

LPCWSTR wide(const QString& s)
{
    return reinterpret_cast<LPCWSTR>(s.utf16());
}

// Run schtasks.exe with a pre-built native command-line tail (schtasks parses
// /tr specially, so we control the quoting ourselves). Returns the exit code, or
// -1 if the process could not run. CREATE_NO_WINDOW suppresses the console flash.
int runSchtasks(const QString& nativeArgs)
{
    QProcess p;
    p.setProgram(QStringLiteral("schtasks"));
    p.setNativeArguments(nativeArgs);
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
        a->flags |= CREATE_NO_WINDOW;
    });
    p.start();
    if (!p.waitForFinished(10000))
        return -1;
    return p.exitCode();
}

} // namespace

bool WinAutostart::currentProcessElevated() const
{
    bool elevated = false;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION info{};
        DWORD size = 0;
        if (GetTokenInformation(token, TokenElevation, &info, sizeof(info), &size))
            elevated = info.TokenIsElevated != 0;
        CloseHandle(token);
    }
    return elevated;
}

// --- Run-key form (registry, no elevation needed) ---------------------------

bool WinAutostart::runKeyPresent() const
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    const LONG r = RegQueryValueExW(key, kValueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

bool WinAutostart::setRunKey(bool present) const
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = ERROR_SUCCESS;
    if (present) {
        const QString data = QStringLiteral("\"%1\"").arg(exePath());   // quoted for spaces
        r = RegSetValueExW(key, kValueName, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(data.utf16()),
                           static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(key, kValueName);
        if (r == ERROR_FILE_NOT_FOUND)
            r = ERROR_SUCCESS;                       // already absent = success
    }
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

// --- Scheduled-task form (schtasks.exe; create/delete need elevation) -------

bool WinAutostart::taskPresent() const
{
    return runSchtasks(QStringLiteral("/query /tn \"%1\"").arg(kTaskName)) == 0;
}

bool WinAutostart::createTask() const
{
    // /rl highest = run with the user's highest privileges (no per-logon UAC prompt).
    const QString args =
        QStringLiteral("/create /tn \"%1\" /sc onlogon /rl highest /tr \"\\\"%2\\\"\" /f")
            .arg(kTaskName, exePath());
    return runSchtasks(args) == 0;
}

bool WinAutostart::deleteTask() const
{
    return runSchtasks(QStringLiteral("/delete /tn \"%1\" /f").arg(kTaskName)) == 0;
}

// --- IAutostart -------------------------------------------------------------

bool WinAutostart::isEnabled() const
{
    return runKeyPresent() || taskPresent();
}

bool WinAutostart::isElevated() const
{
    return taskPresent();
}

bool WinAutostart::apply(bool enabled, bool elevated)
{
    if (!enabled) {
        bool ok = setRunKey(false);
        if (taskPresent()) {
            if (!currentProcessElevated())
                return false;                         // can't remove the task un-elevated
            ok = deleteTask() && ok;
        }
        return ok;
    }

    if (elevated) {
        if (!currentProcessElevated())
            return false;                             // caller relaunches elevated
        setRunKey(false);                             // keep a single form
        return createTask();
    }

    // Non-elevated form requested.
    if (taskPresent() && !currentProcessElevated())
        return false;                                 // can't drop the elevated task
    if (currentProcessElevated())
        deleteTask();
    return setRunKey(true);
}

bool WinAutostart::relaunchElevated()
{
    const QString exe = exePath();
    // Pass a flag so the elevated instance skips the single-instance guard — a
    // high-integrity process could otherwise connect to this (medium-integrity)
    // instance's pipe and exit, defeating the elevation. See app/main.cpp.
    const HINSTANCE h = ShellExecuteW(nullptr, L"runas", wide(exe),
                                      L"--relaunched-elevated", nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(h) > 32;         // <=32 => failure/UAC declined
}
