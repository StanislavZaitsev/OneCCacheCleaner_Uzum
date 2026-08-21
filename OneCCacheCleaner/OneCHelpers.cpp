#include "pch.h"
#include "OneCHelpers.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shlobj.h>
#include <commdlg.h>
#include <wbemidl.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "ole32.lib")

// ---------------------------------------------------------------------------
// Базовые файловые функции
// ---------------------------------------------------------------------------

namespace OneC
{
    bool FileExists(const std::wstring& path)
    {
        const DWORD attributes = ::GetFileAttributesW(path.c_str());

        return attributes != INVALID_FILE_ATTRIBUTES &&
            !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool DirExists(const std::wstring& path)
    {
        const DWORD attributes = ::GetFileAttributesW(path.c_str());

        return attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY);
    }
}

// ---------------------------------------------------------------------------
// Вспомогательные строковые и файловые функции
// ---------------------------------------------------------------------------

namespace
{
    std::wstring ToLowerW(std::wstring s)
    {
        std::transform(
            s.begin(),
            s.end(),
            s.begin(),
            [](wchar_t ch)
            {
                return static_cast<wchar_t>(std::towlower(ch));
            }
        );

        return s;
    }

    std::wstring TrimW(const std::wstring& s)
    {
        const size_t first = s.find_first_not_of(L" \t\r\n");

        if (first == std::wstring::npos)
        {
            return std::wstring();
        }

        const size_t last = s.find_last_not_of(L" \t\r\n");

        return s.substr(first, last - first + 1);
    }

    std::wstring UnquoteW(const std::wstring& s)
    {
        std::wstring t = TrimW(s);

        if (t.size() >= 2)
        {
            if ((t.front() == L'"' && t.back() == L'"') ||
                (t.front() == L'\'' && t.back() == L'\''))
            {
                return TrimW(t.substr(1, t.size() - 2));
            }
        }

        return t;
    }

    std::vector<std::wstring> SplitLinesW(const std::wstring& text)
    {
        std::vector<std::wstring> lines;

        size_t start = 0;

        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == L'\n')
            {
                std::wstring line = text.substr(start, i - start);

                if (!line.empty() && line.back() == L'\r')
                {
                    line.pop_back();
                }

                lines.push_back(line);

                start = i + 1;
            }
        }

        if (start < text.size())
        {
            std::wstring line = text.substr(start);

            if (!line.empty() && line.back() == L'\r')
            {
                line.pop_back();
            }

            lines.push_back(line);
        }

        return lines;
    }

    std::wstring GetEnvVarW(const wchar_t* name)
    {
        const DWORD len = ::GetEnvironmentVariableW(name, nullptr, 0);

        if (len == 0)
        {
            return std::wstring();
        }

        std::wstring value(static_cast<size_t>(len), L'\0');

        const DWORD copied = ::GetEnvironmentVariableW(
            name,
            &value[0],
            len
        );

        if (copied == 0)
        {
            return std::wstring();
        }

        value.resize(copied);

        return value;
    }

    std::wstring GetKnownFolderPathW(int csidl)
    {
        wchar_t path[MAX_PATH] = {};

        const HRESULT hr = ::SHGetFolderPathW(
            nullptr,
            csidl,
            nullptr,
            SHGFP_TYPE_CURRENT,
            path
        );

        if (SUCCEEDED(hr))
        {
            return path;
        }

        return std::wstring();
    }

    void UniqueStrings(std::vector<std::wstring>& items)
    {
        std::vector<std::wstring> unique;

        for (const auto& item : items)
        {
            const auto duplicate = std::find_if(
                unique.begin(),
                unique.end(),
                [&](const std::wstring& existing)
                {
                    return ::lstrcmpiW(existing.c_str(), item.c_str()) == 0;
                }
            );

            if (duplicate == unique.end())
            {
                unique.push_back(item);
            }
        }

        items.swap(unique);
    }

    std::wstring ExpandEnvironmentPath(const std::wstring& path)
    {
        if (path.empty())
        {
            return std::wstring();
        }

        const DWORD needed = ::ExpandEnvironmentStringsW(
            path.c_str(),
            nullptr,
            0
        );

        if (needed == 0)
        {
            return path;
        }

        std::vector<wchar_t> buffer(static_cast<size_t>(needed), 0);

        const DWORD copied = ::ExpandEnvironmentStringsW(
            path.c_str(),
            buffer.data(),
            needed
        );

        if (copied == 0)
        {
            return path;
        }

        return buffer.data();
    }

    std::wstring AppendPath(
        const std::wstring& base,
        const std::wstring& child
    )
    {
        if (base.empty())
        {
            return child;
        }

        if (base.back() == L'\\')
        {
            return base + child;
        }

        return base + L"\\" + child;
    }

    std::vector<BYTE> ReadFileBytes(const std::wstring& path)
    {
        HANDLE file = ::CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (file == INVALID_HANDLE_VALUE)
        {
            return {};
        }

        LARGE_INTEGER fileSize = {};

        if (!::GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0)
        {
            ::CloseHandle(file);
            return {};
        }

        if (fileSize.QuadPart >
            static_cast<LONGLONG>((std::numeric_limits<DWORD>::max)()))
        {
            ::CloseHandle(file);
            return {};
        }

        std::vector<BYTE> data(static_cast<size_t>(fileSize.QuadPart));

        DWORD read = 0;

        BOOL ok = ::ReadFile(
            file,
            data.data(),
            static_cast<DWORD>(data.size()),
            &read,
            nullptr
        );

        ::CloseHandle(file);

        if (!ok)
        {
            return {};
        }

        data.resize(read);

        return data;
    }

    bool TryMultiByteToWideW(
        UINT codePage,
        const BYTE* data,
        size_t dataSize,
        DWORD flags,
        std::wstring& out
    )
    {
        if (dataSize == 0)
        {
            out.clear();
            return true;
        }

        int required = ::MultiByteToWideChar(
            codePage,
            flags,
            reinterpret_cast<LPCCH>(data),
            static_cast<int>(dataSize),
            nullptr,
            0
        );

        if (required <= 0)
        {
            return false;
        }

        out.assign(static_cast<size_t>(required), L'\0');

        int converted = ::MultiByteToWideChar(
            codePage,
            flags,
            reinterpret_cast<LPCCH>(data),
            static_cast<int>(dataSize),
            &out[0],
            required
        );

        return converted > 0;
    }

    std::wstring DecodeText(const std::vector<BYTE>& data)
    {
        if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xFE)
        {
            size_t byteCount = data.size() - 2;
            byteCount -= byteCount % 2;

            std::wstring result(
                reinterpret_cast<const wchar_t*>(data.data() + 2),
                byteCount / 2
            );

            if (!result.empty() && result[0] == 0xFEFF)
            {
                result.erase(0, 1);
            }

            return result;
        }

        if (data.size() >= 3 &&
            data[0] == 0xEF &&
            data[1] == 0xBB &&
            data[2] == 0xBF)
        {
            std::wstring result;

            if (TryMultiByteToWideW(
                CP_UTF8,
                data.data() + 3,
                data.size() - 3,
                MB_ERR_INVALID_CHARS,
                result))
            {
                return result;
            }
        }

        std::wstring result;

        if (TryMultiByteToWideW(
            CP_UTF8,
            data.data(),
            data.size(),
            MB_ERR_INVALID_CHARS,
            result))
        {
            return result;
        }

        if (TryMultiByteToWideW(
            1251,
            data.data(),
            data.size(),
            0,
            result))
        {
            return result;
        }

        if (TryMultiByteToWideW(
            CP_ACP,
            data.data(),
            data.size(),
            0,
            result))
        {
            return result;
        }

        return std::wstring();
    }

    std::wstring QuoteArg(const std::wstring& arg)
    {
        if (arg.empty())
        {
            return L"\"\"";
        }

        const bool needQuotes =
            arg.find_first_of(L" \t") != std::wstring::npos ||
            arg.find(L'"') != std::wstring::npos;

        if (!needQuotes)
        {
            return arg;
        }

        std::wstring quoted = L"\"";
        size_t backslashes = 0;

        for (const wchar_t ch : arg)
        {
            if (ch == L'\\')
            {
                ++backslashes;
                continue;
            }

            if (ch == L'"')
            {
                quoted.append(backslashes * 2 + 1, L'\\');
                quoted += ch;
            }
            else
            {
                quoted.append(backslashes, L'\\');
                quoted += ch;
            }

            backslashes = 0;
        }

        quoted.append(backslashes * 2, L'\\');
        quoted += L'"';

        return quoted;
    }

    bool RunProcessAndWait(
        const std::wstring& applicationPath,
        const std::wstring& arguments,
        DWORD timeoutMs,
        std::wstring& log
    )
    {
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION pi = {};

        std::wstring commandLine = QuoteArg(applicationPath);

        if (!arguments.empty())
        {
            commandLine += L" ";
            commandLine += arguments;
        }

        std::vector<wchar_t> cmdLine(commandLine.begin(), commandLine.end());
        cmdLine.push_back(L'\0');

        if (!::CreateProcessW(
            applicationPath.c_str(),
            cmdLine.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &si,
            &pi))
        {
            log = L"Ошибка CreateProcess. Код ошибки: ";
            log += std::to_wstring(::GetLastError());

            return false;
        }

        const DWORD waitResult = ::WaitForSingleObject(pi.hProcess, timeoutMs);

        if (waitResult == WAIT_TIMEOUT)
        {
            ::CloseHandle(pi.hProcess);
            ::CloseHandle(pi.hThread);

            log = L"Превышено время ожидания процесса; процесс оставлен запущенным.";

            return false;
        }

        if (waitResult == WAIT_FAILED)
        {
            const DWORD error = ::GetLastError();

            ::CloseHandle(pi.hProcess);
            ::CloseHandle(pi.hThread);

            log = L"Ошибка ожидания процесса. Код ошибки: ";
            log += std::to_wstring(error);

            return false;
        }

        DWORD exitCode = 0;

        if (!::GetExitCodeProcess(pi.hProcess, &exitCode))
        {
            exitCode = static_cast<DWORD>(-1);
        }

        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);

        log = L"Код завершения: ";
        log += std::to_wstring(exitCode);

        return exitCode == 0;
    }
}

// ---------------------------------------------------------------------------
// Поиск 1CEStart.cfg и CommonInfoBases
// ---------------------------------------------------------------------------

namespace
{
    std::vector<std::wstring> Find1CEStartCfgFiles()
    {
        std::vector<std::wstring> candidates;

        auto addRoot = [&](const std::wstring& root)
            {
                if (root.empty())
                {
                    return;
                }

                candidates.push_back(root + L"\\1C\\1CEStart\\1CEStart.cfg");
                candidates.push_back(root + L"\\1C\\1CEStart.cfg");
                candidates.push_back(root + L"\\1C\\1cv8\\1CEStart.cfg");
            };

        addRoot(GetEnvVarW(L"APPDATA"));
        addRoot(GetKnownFolderPathW(CSIDL_APPDATA));

        addRoot(GetEnvVarW(L"LOCALAPPDATA"));
        addRoot(GetKnownFolderPathW(CSIDL_LOCAL_APPDATA));

        addRoot(GetEnvVarW(L"ALLUSERSPROFILE"));
        addRoot(GetKnownFolderPathW(CSIDL_COMMON_APPDATA));

        UniqueStrings(candidates);

        std::vector<std::wstring> result;

        for (const auto& candidate : candidates)
        {
            if (OneC::FileExists(candidate))
            {
                result.push_back(candidate);
            }
        }

        return result;
    }

    std::vector<std::wstring> GetCommonInfoBasesValues(
        const std::wstring& cfgPath
    )
    {
        std::vector<std::wstring> result;

        const std::vector<BYTE> bytes = ReadFileBytes(cfgPath);

        if (bytes.empty())
        {
            return result;
        }

        const std::wstring text = DecodeText(bytes);
        const std::vector<std::wstring> lines = SplitLinesW(text);

        for (const std::wstring& rawLine : lines)
        {
            std::wstring line = TrimW(rawLine);

            if (line.empty())
            {
                continue;
            }

            if (line.front() == L'#' || line.front() == L';')
            {
                continue;
            }

            const size_t eq = line.find(L'=');

            if (eq == std::wstring::npos || eq == 0)
            {
                continue;
            }

            const std::wstring key =
                ToLowerW(TrimW(line.substr(0, eq)));

            if (key != L"commoninfobases")
            {
                continue;
            }

            std::wstring value = TrimW(line.substr(eq + 1));

            size_t start = 0;

            for (size_t i = 0; i <= value.size(); ++i)
            {
                if (i == value.size() || value[i] == L';')
                {
                    std::wstring part = value.substr(start, i - start);

                    part = TrimW(UnquoteW(part));

                    if (!part.empty())
                    {
                        result.push_back(part);
                    }

                    start = i + 1;
                }
            }

            break;
        }

        return result;
    }

    std::wstring ResolveIbasesFromCommonPath(
        const std::wstring& rawPath,
        const std::wstring& cfgPath
    )
    {
        std::wstring path = TrimW(UnquoteW(rawPath));

        if (path.empty())
        {
            return std::wstring();
        }

        path = ExpandEnvironmentPath(path);

        if (path.empty())
        {
            return std::wstring();
        }

        const bool isAbsolute =
            (path.size() >= 3 && std::iswalpha(path[0]) && path[1] == L':') ||
            (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\');

        if (!isAbsolute)
        {
            const size_t slash = cfgPath.find_last_of(L"\\/");

            if (slash != std::wstring::npos)
            {
                path = AppendPath(cfgPath.substr(0, slash), path);
            }
        }

        if (OneC::FileExists(path))
        {
            return path;
        }

        if (OneC::DirExists(path))
        {
            const std::wstring candidate = AppendPath(path, L"ibases.v8i");

            if (OneC::FileExists(candidate))
            {
                return candidate;
            }
        }

        const std::wstring withExtension = path + L".v8i";

        if (OneC::FileExists(withExtension))
        {
            return withExtension;
        }

        return std::wstring();
    }
}

// ---------------------------------------------------------------------------
// Парсинг Connect и ibases.v8i
// ---------------------------------------------------------------------------

namespace
{
    void ParseConnectString(
        const std::wstring& connect,
        OneC::BaseInfo& info
    )
    {
        size_t start = 0;

        auto parsePart = [&](const std::wstring& rawPart)
            {
                std::wstring part = TrimW(rawPart);

                if (part.empty())
                {
                    return;
                }

                const size_t eq = part.find(L'=');

                if (eq == std::wstring::npos || eq == 0)
                {
                    return;
                }

                const std::wstring key =
                    ToLowerW(TrimW(part.substr(0, eq)));

                const std::wstring value =
                    UnquoteW(TrimW(part.substr(eq + 1)));

                if (key == L"srvr" || key == L"server")
                {
                    info.server = value;
                }
                else if (key == L"ref")
                {
                    info.ref = value;
                }
                else if (key == L"file")
                {
                    info.folder = value;
                }
                else if (key == L"usr" || key == L"user")
                {
                    info.user = value;
                }
            };

        for (size_t i = 0; i <= connect.size(); ++i)
        {
            if (i == connect.size() || connect[i] == L';')
            {
                parsePart(connect.substr(start, i - start));
                start = i + 1;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Реестр и поиск 1cestart.exe
// ---------------------------------------------------------------------------

namespace
{
    std::wstring ReadRegistryStringValue(
        HKEY rootKey,
        const std::wstring& subKey,
        const std::wstring& valueName,
        REGSAM additionalAccess
    )
    {
        HKEY key = nullptr;

        if (::RegOpenKeyExW(
            rootKey,
            subKey.c_str(),
            0,
            KEY_READ | additionalAccess,
            &key) != ERROR_SUCCESS)
        {
            return std::wstring();
        }

        DWORD type = 0;
        DWORD size = 0;

        if (::RegQueryValueExW(
            key,
            valueName.c_str(),
            nullptr,
            &type,
            nullptr,
            &size) != ERROR_SUCCESS)
        {
            ::RegCloseKey(key);
            return std::wstring();
        }

        if ((type != REG_SZ && type != REG_EXPAND_SZ) || size == 0)
        {
            ::RegCloseKey(key);
            return std::wstring();
        }

        std::vector<wchar_t> buffer((size / sizeof(wchar_t)) + 1, 0);

        if (::RegQueryValueExW(
            key,
            valueName.c_str(),
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(buffer.data()),
            &size) != ERROR_SUCCESS)
        {
            ::RegCloseKey(key);
            return std::wstring();
        }

        ::RegCloseKey(key);

        std::wstring value = buffer.data();

        if (type == REG_EXPAND_SZ)
        {
            value = ExpandEnvironmentPath(value);
        }

        return TrimW(UnquoteW(value));
    }

    std::vector<std::wstring> GetRegistrySubKeys(
        HKEY rootKey,
        const std::wstring& subKey,
        REGSAM additionalAccess
    )
    {
        std::vector<std::wstring> result;

        HKEY key = nullptr;

        if (::RegOpenKeyExW(
            rootKey,
            subKey.c_str(),
            0,
            KEY_READ | additionalAccess,
            &key) != ERROR_SUCCESS)
        {
            return result;
        }

        DWORD index = 0;
        wchar_t name[1024];
        DWORD nameLength = ARRAYSIZE(name);

        while (::RegEnumKeyExW(
            key,
            index,
            name,
            &nameLength,
            nullptr,
            nullptr,
            nullptr,
            nullptr) == ERROR_SUCCESS)
        {
            result.push_back(name);
            nameLength = ARRAYSIZE(name);
            ++index;
        }

        ::RegCloseKey(key);

        return result;
    }

    std::wstring ExtractExecutablePathFromCommand(
        const std::wstring& command
    )
    {
        std::wstring cmd = TrimW(command);

        if (cmd.empty())
        {
            return std::wstring();
        }

        if (cmd.front() == L'"')
        {
            const size_t endQuote = cmd.find(L'"', 1);

            if (endQuote != std::wstring::npos)
            {
                return ExpandEnvironmentPath(
                    TrimW(cmd.substr(1, endQuote - 1))
                );
            }
        }

        const std::wstring lower = ToLowerW(cmd);

        size_t pos = 0;

        while ((pos = lower.find(L".exe", pos)) != std::wstring::npos)
        {
            const size_t afterExe = pos + 4;

            if (afterExe == cmd.size() ||
                cmd[afterExe] == L' ' ||
                cmd[afterExe] == L'\t' ||
                cmd[afterExe] == L'"')
            {
                return ExpandEnvironmentPath(
                    TrimW(cmd.substr(0, afterExe))
                );
            }

            pos = afterExe;
        }

        const size_t space = cmd.find_first_of(L" \t");

        if (space != std::wstring::npos)
        {
            return ExpandEnvironmentPath(
                TrimW(cmd.substr(0, space))
            );
        }

        return ExpandEnvironmentPath(cmd);
    }

    std::wstring NormalizePossibleCommand(const std::wstring& raw)
    {
        std::wstring value = TrimW(UnquoteW(raw));

        if (value.empty())
        {
            return std::wstring();
        }

        const std::wstring lower = ToLowerW(value);

        if (lower.find(L".exe") != std::wstring::npos)
        {
            const std::wstring extracted =
                ExtractExecutablePathFromCommand(value);

            if (!extracted.empty())
            {
                return ExpandEnvironmentPath(extracted);
            }
        }

        return ExpandEnvironmentPath(value);
    }

    std::wstring TryFind1CEStartInLocation(const std::wstring& location)
    {
        if (location.empty())
        {
            return std::wstring();
        }

        const std::wstring path = NormalizePossibleCommand(location);

        if (path.empty())
        {
            return std::wstring();
        }

        if (OneC::FileExists(path))
        {
            return path;
        }

        std::wstring exe = AppendPath(path, L"common\\1cestart.exe");

        if (OneC::FileExists(exe))
        {
            return exe;
        }

        exe = AppendPath(path, L"1cestart.exe");

        if (OneC::FileExists(exe))
        {
            return exe;
        }

        exe = AppendPath(path, L"bin\\1cestart.exe");

        if (OneC::FileExists(exe))
        {
            return exe;
        }

        return std::wstring();
    }

    std::wstring Find1CEStartFromRegistryView(
        HKEY rootKey,
        const std::wstring& baseSubKey,
        REGSAM view
    )
    {
        auto tryRegistryValue = [&](
            HKEY key,
            const std::wstring& subKey,
            const std::wstring& valueName = L""
            ) -> std::wstring
            {
                const std::wstring raw = ReadRegistryStringValue(
                    key,
                    subKey,
                    valueName,
                    view
                );

                if (raw.empty())
                {
                    return std::wstring();
                }

                const std::wstring normalized =
                    NormalizePossibleCommand(raw);

                return TryFind1CEStartInLocation(normalized);
            };

        // App Paths для текущего проверяемого раздела HKLM или HKCU.
        std::wstring exe = tryRegistryValue(
            rootKey,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\"
            L"App Paths\\1cestart.exe"
        );

        if (!exe.empty())
        {
            return exe;
        }

        // Эти разделы достаточно проверять при проходе по HKLM.
        // Параметр view обеспечивает проверку 32- и 64-разрядных представлений.
        if (rootKey == HKEY_LOCAL_MACHINE)
        {
            exe = tryRegistryValue(
                HKEY_CLASSES_ROOT,
                L"Applications\\1cestart.exe\\shell\\open\\command"
            );

            if (!exe.empty())
            {
                return exe;
            }

            exe = tryRegistryValue(
                HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Classes\\Applications\\"
                L"1cestart.exe\\shell\\open\\command"
            );

            if (!exe.empty())
            {
                return exe;
            }

            exe = tryRegistryValue(
                HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Classes\\V83.InfoBaseList\\"
                L"shell\\Open\\command"
            );

            if (!exe.empty())
            {
                return exe;
            }
        }

        const std::vector<std::wstring> valueNames =
        {
            L"Location",
            L"InstallPath",
            L"Path",
            L"BinFolder",
            L""
        };

        for (const auto& valueName : valueNames)
        {
            exe = tryRegistryValue(
                rootKey,
                baseSubKey,
                valueName
            );

            if (!exe.empty())
            {
                return exe;
            }
        }

        const std::vector<std::wstring> subKeys =
            GetRegistrySubKeys(rootKey, baseSubKey, view);

        for (const auto& subKey : subKeys)
        {
            const std::wstring subKeyPath =
                baseSubKey + L"\\" + subKey;

            for (const auto& valueName : valueNames)
            {
                exe = tryRegistryValue(
                    rootKey,
                    subKeyPath,
                    valueName
                );

                if (!exe.empty())
                {
                    return exe;
                }
            }
        }

        return std::wstring();
    }

    std::wstring Find1CEStartFromRegistry()
    {
        const HKEY rootKeys[] =
        {
            HKEY_LOCAL_MACHINE,
            HKEY_CURRENT_USER
        };

        const std::wstring baseKeys[] =
        {
            L"SOFTWARE\\1C\\1Cv8"
        };

        const std::vector<REGSAM> views =
        {
            0,
            KEY_WOW64_64KEY,
            KEY_WOW64_32KEY
        };

        for (const auto rootKey : rootKeys)
        {
            for (const auto& baseKey : baseKeys)
            {
                for (const auto view : views)
                {
                    const std::wstring exe =
                        Find1CEStartFromRegistryView(
                            rootKey,
                            baseKey,
                            view
                        );

                    if (!exe.empty())
                    {
                        return exe;
                    }
                }
            }
        }

        return std::wstring();
    }

    std::wstring Find1CEStartInCommonFolders()
    {
        std::vector<std::wstring> roots;

        auto addRoot = [&](const std::wstring& root)
            {
                if (!root.empty())
                {
                    roots.push_back(root);
                }
            };

        addRoot(GetEnvVarW(L"ProgramFiles"));
        addRoot(GetEnvVarW(L"ProgramFiles(x86)"));
        addRoot(GetKnownFolderPathW(CSIDL_PROGRAM_FILES));
        addRoot(GetKnownFolderPathW(CSIDL_PROGRAM_FILESX86));

        UniqueStrings(roots);

        for (const auto& root : roots)
        {
            const std::wstring exe =
                root + L"\\1cv8\\common\\1cestart.exe";

            if (OneC::FileExists(exe))
            {
                return exe;
            }
        }

        return std::wstring();
    }

    void Find1CEStartRecursive(
        const std::wstring& dir,
        int depth,
        std::wstring& best,
        FILETIME& bestTime,
        bool& found
    )
    {
        if (depth > 8)
        {
            return;
        }

        const std::wstring pattern = dir + L"\\*";

        WIN32_FIND_DATAW fd = {};

        HANDLE findHandle = ::FindFirstFileW(pattern.c_str(), &fd);

        if (findHandle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        do
        {
            const std::wstring name = fd.cFileName;

            if (name == L"." || name == L"..")
            {
                continue;
            }

            const std::wstring fullPath = dir + L"\\" + name;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                {
                    Find1CEStartRecursive(
                        fullPath,
                        depth + 1,
                        best,
                        bestTime,
                        found
                    );
                }
            }
            else
            {
                if (::lstrcmpiW(fd.cFileName, L"1cestart.exe") == 0)
                {
                    if (!found ||
                        ::CompareFileTime(&fd.ftLastWriteTime, &bestTime) > 0)
                    {
                        found = true;
                        bestTime = fd.ftLastWriteTime;
                        best = fullPath;
                    }
                }
            }

        } while (::FindNextFileW(findHandle, &fd));

        ::FindClose(findHandle);
    }
}

// ---------------------------------------------------------------------------
// WMI: запущенные процессы 1С
// ---------------------------------------------------------------------------

namespace
{
    std::wstring ExtractParameterValue(
        const std::wstring& commandLine,
        const std::wstring& parameterName
    )
    {
        if (commandLine.empty() || parameterName.empty())
        {
            return std::wstring();
        }

        const std::wstring lower = ToLowerW(commandLine);
        const std::wstring parameterLower = ToLowerW(parameterName);

        size_t pos = 0;

        while (true)
        {
            pos = lower.find(parameterLower, pos);

            if (pos == std::wstring::npos)
            {
                return std::wstring();
            }

            const size_t afterParameter = pos + parameterLower.size();
            const bool validStart = pos == 0 ||
                lower[pos - 1] == L' ' ||
                lower[pos - 1] == L'\t' ||
                lower[pos - 1] == L'"';

            const bool validEnd = afterParameter == lower.size() ||
                lower[afterParameter] == L' ' ||
                lower[afterParameter] == L'\t' ||
                lower[afterParameter] == L':' ||
                lower[afterParameter] == L'=' ||
                lower[afterParameter] == L'"';

            if (validStart && validEnd)
            {
                break;
            }

            pos += parameterLower.size();
        }

        pos += parameterLower.size();

        while (pos < commandLine.size() &&
            (commandLine[pos] == L' ' ||
                commandLine[pos] == L'\t' ||
                commandLine[pos] == L':' ||
                commandLine[pos] == L'='))
        {
            ++pos;
        }

        if (pos >= commandLine.size())
        {
            return std::wstring();
        }

        if (commandLine[pos] == L'"')
        {
            ++pos;

            const size_t start = pos;

            while (pos < commandLine.size() && commandLine[pos] != L'"')
            {
                ++pos;
            }

            return TrimW(commandLine.substr(start, pos - start));
        }

        const size_t start = pos;

        while (pos < commandLine.size() &&
            commandLine[pos] != L' ' &&
            commandLine[pos] != L'\t' &&
            commandLine[pos] != L'"' &&
            commandLine[pos] != L'/')
        {
            ++pos;
        }

        return TrimW(commandLine.substr(start, pos - start));
    }

    std::wstring NormalizePathForComparison(std::wstring path)
    {
        path = ExpandEnvironmentPath(UnquoteW(path));

        if (path.empty())
        {
            return path;
        }

        const DWORD required = ::GetFullPathNameW(
            path.c_str(),
            0,
            nullptr,
            nullptr
        );

        if (required > 0)
        {
            std::vector<wchar_t> buffer(required, L'\0');

            if (::GetFullPathNameW(
                path.c_str(),
                required,
                buffer.data(),
                nullptr) > 0)
            {
                path = buffer.data();
            }
        }

        while (path.size() > 3 &&
            (path.back() == L'\\' || path.back() == L'/'))
        {
            path.pop_back();
        }

        return path;
    }

    void ExtractProcessIdentity(
        const std::wstring& commandLine,
        OneC::Running1CProcess& process
    )
    {
        process.ibName = ExtractParameterValue(commandLine, L"/IBName");
        process.folder = NormalizePathForComparison(
            ExtractParameterValue(commandLine, L"/F")
        );

        const std::wstring sValue =
            ExtractParameterValue(commandLine, L"/S");

        if (!sValue.empty())
        {
            const size_t slash = sValue.rfind(L'\\');

            if (slash != std::wstring::npos && slash + 1 < sValue.size())
            {
                process.server = TrimW(sValue.substr(0, slash));
                process.ref = TrimW(sValue.substr(slash + 1));
            }
            else
            {
                process.server = TrimW(sValue);
            }
        }

        if (process.ref.empty())
        {
            process.ref = ExtractParameterValue(commandLine, L"/Ref");
        }
    }

    std::vector<OneC::Running1CProcess> QueryRunning1CProcessesWMI()
    {
        std::vector<OneC::Running1CProcess> result;
        DWORD currentSessionId = 0;
        const bool hasCurrentSession = ::ProcessIdToSessionId(
            ::GetCurrentProcessId(),
            &currentSessionId
        ) != FALSE;

        HRESULT hrInit = ::CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED
        );

        bool needCoUninitialize = SUCCEEDED(hrInit);

        if (hrInit == RPC_E_CHANGED_MODE)
        {
            // COM уже инициализирован в другом режиме и пригоден для WMI.
            hrInit = S_OK;
            needCoUninitialize = false;
        }

        if (FAILED(hrInit) && hrInit != S_FALSE)
        {
            return result;
        }

        const HRESULT securityResult = ::CoInitializeSecurity(
            nullptr,
            -1,
            nullptr,
            nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE,
            nullptr
        );

        if (FAILED(securityResult) && securityResult != RPC_E_TOO_LATE)
        {
            if (needCoUninitialize)
            {
                ::CoUninitialize();
            }

            return result;
        }

        IWbemLocator* locator = nullptr;

        HRESULT hr = ::CoCreateInstance(
            CLSID_WbemLocator,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IWbemLocator,
            reinterpret_cast<void**>(&locator)
        );

        if (FAILED(hr))
        {
            if (needCoUninitialize)
            {
                ::CoUninitialize();
            }

            return result;
        }

        IWbemServices* services = nullptr;

        BSTR networkResource = ::SysAllocString(L"ROOT\\CIMV2");

        hr = locator->ConnectServer(
            networkResource,
            nullptr,
            nullptr,
            nullptr,
            0,
            nullptr,
            nullptr,
            &services
        );

        ::SysFreeString(networkResource);

        if (FAILED(hr))
        {
            locator->Release();

            if (needCoUninitialize)
            {
                ::CoUninitialize();
            }

            return result;
        }

        hr = ::CoSetProxyBlanket(
            services,
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_NONE,
            nullptr,
            RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE
        );

        if (FAILED(hr))
        {
            services->Release();
            locator->Release();

            if (needCoUninitialize)
            {
                ::CoUninitialize();
            }

            return result;
        }

        IEnumWbemClassObject* enumerator = nullptr;

        BSTR language = ::SysAllocString(L"WQL");

        BSTR query = ::SysAllocString(
            L"SELECT ProcessId, CommandLine "
            L"FROM Win32_Process "
            L"WHERE Name='1cv8c.exe' OR Name='1cv8.exe'"
        );

        hr = services->ExecQuery(
            language,
            query,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr,
            &enumerator
        );

        ::SysFreeString(language);
        ::SysFreeString(query);

        if (FAILED(hr))
        {
            services->Release();
            locator->Release();

            if (needCoUninitialize)
            {
                ::CoUninitialize();
            }

            return result;
        }

        while (true)
        {
            IWbemClassObject* classObject = nullptr;
            ULONG returned = 0;

            hr = enumerator->Next(
                2000,
                1,
                &classObject,
                &returned
            );

            if (hr == WBEM_S_TIMEDOUT || FAILED(hr) || returned == 0)
            {
                break;
            }

            OneC::Running1CProcess process;

            VARIANT pidVariant;
            ::VariantInit(&pidVariant);

            hr = classObject->Get(
                L"ProcessId",
                0,
                &pidVariant,
                nullptr,
                nullptr
            );

            if (SUCCEEDED(hr))
            {
                VARIANT convertedPid;
                ::VariantInit(&convertedPid);

                if (SUCCEEDED(::VariantChangeType(
                    &convertedPid,
                    &pidVariant,
                    0,
                    VT_UI4)))
                {
                    process.processId =
                        static_cast<uint32_t>(convertedPid.ulVal);
                }

                ::VariantClear(&convertedPid);
            }

            ::VariantClear(&pidVariant);

            VARIANT commandVariant;
            ::VariantInit(&commandVariant);

            hr = classObject->Get(
                L"CommandLine",
                0,
                &commandVariant,
                nullptr,
                nullptr
            );

            if (SUCCEEDED(hr) &&
                commandVariant.vt == VT_BSTR &&
                commandVariant.bstrVal != nullptr)
            {
                process.commandLine = commandVariant.bstrVal;
            }

            ::VariantClear(&commandVariant);

            ExtractProcessIdentity(process.commandLine, process);

            DWORD processSessionId = 0;
            const bool isCurrentSession =
                process.processId != 0 &&
                (!hasCurrentSession ||
                    (::ProcessIdToSessionId(
                        process.processId,
                        &processSessionId) != FALSE &&
                        processSessionId == currentSessionId));

            if (isCurrentSession)
            {
                result.push_back(process);
            }

            classObject->Release();
        }

        enumerator->Release();
        services->Release();
        locator->Release();

        if (needCoUninitialize)
        {
            ::CoUninitialize();
        }

        return result;
    }

    struct EnumWindowsData
    {
        uint32_t processId = 0;
        std::vector<HWND>* windows = nullptr;
    };

    BOOL CALLBACK EnumWindowsCallback(
        HWND hwnd,
        LPARAM lParam
    )
    {
        EnumWindowsData* data =
            reinterpret_cast<EnumWindowsData*>(lParam);

        DWORD processId = 0;

        ::GetWindowThreadProcessId(hwnd, &processId);

        if (processId == data->processId &&
            ::GetWindow(hwnd, GW_OWNER) == nullptr)
        {
            data->windows->push_back(hwnd);
        }

        return TRUE;
    }
}

// ---------------------------------------------------------------------------
// Удаление кэша
// ---------------------------------------------------------------------------

namespace
{
    bool TryNormalizeCacheId(
        const std::wstring& rawId,
        std::wstring& normalized
    )
    {
        normalized = TrimW(UnquoteW(rawId));

        if (normalized.size() == 38 &&
            normalized.front() == L'{' &&
            normalized.back() == L'}')
        {
            normalized = normalized.substr(1, 36);
        }

        if (normalized.size() != 36)
        {
            return false;
        }

        for (size_t i = 0; i < normalized.size(); ++i)
        {
            const bool hyphenPosition =
                i == 8 || i == 13 || i == 18 || i == 23;

            if (hyphenPosition)
            {
                if (normalized[i] != L'-')
                {
                    return false;
                }
            }
            else if (!std::iswxdigit(normalized[i]))
            {
                return false;
            }
        }

        return true;
    }

    std::vector<std::wstring> Get1CCacheRoots()
    {
        std::vector<std::wstring> roots;

        std::wstring appData = GetEnvVarW(L"APPDATA");

        if (appData.empty())
        {
            appData = GetKnownFolderPathW(CSIDL_APPDATA);
        }

        std::wstring localAppData = GetEnvVarW(L"LOCALAPPDATA");

        if (localAppData.empty())
        {
            localAppData = GetKnownFolderPathW(CSIDL_LOCAL_APPDATA);
        }

        if (!appData.empty())
        {
            roots.push_back(appData + L"\\1C\\1cv8");
        }

        if (!localAppData.empty())
        {
            roots.push_back(localAppData + L"\\1C\\1cv8");
        }

        UniqueStrings(roots);

        return roots;
    }

    bool DeleteDirectoryRecursive(const std::wstring& dir)
    {
        const DWORD rootAttributes = ::GetFileAttributesW(dir.c_str());

        if (rootAttributes == INVALID_FILE_ATTRIBUTES)
        {
            return false;
        }

        // Не переходим по точкам повторного анализа за пределы кэша.
        if (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            return ::RemoveDirectoryW(dir.c_str()) != FALSE;
        }

        bool ok = true;

        const std::wstring pattern = dir + L"\\*";

        WIN32_FIND_DATAW fd = {};

        HANDLE findHandle = ::FindFirstFileW(pattern.c_str(), &fd);

        if (findHandle == INVALID_HANDLE_VALUE)
        {
            const DWORD error = ::GetLastError();

            if (error == ERROR_FILE_NOT_FOUND ||
                error == ERROR_PATH_NOT_FOUND)
            {
                ::SetFileAttributesW(
                    dir.c_str(),
                    FILE_ATTRIBUTE_NORMAL
                );

                if (!::RemoveDirectoryW(dir.c_str()))
                {
                    return false;
                }

                return true;
            }

            return false;
        }

        do
        {
            const std::wstring name = fd.cFileName;

            if (name == L"." || name == L"..")
            {
                continue;
            }

            const std::wstring fullPath = dir + L"\\" + name;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                {
                    if (!::RemoveDirectoryW(fullPath.c_str()))
                    {
                        ok = false;
                    }
                }
                else
                {
                    if (!DeleteDirectoryRecursive(fullPath))
                    {
                        ok = false;
                    }
                }
            }
            else
            {
                ::SetFileAttributesW(
                    fullPath.c_str(),
                    FILE_ATTRIBUTE_NORMAL
                );

                if (!::DeleteFileW(fullPath.c_str()))
                {
                    ok = false;
                }
            }

        } while (::FindNextFileW(findHandle, &fd));

        ::FindClose(findHandle);

        ::SetFileAttributesW(
            dir.c_str(),
            FILE_ATTRIBUTE_NORMAL
        );

        if (!::RemoveDirectoryW(dir.c_str()))
        {
            ok = false;
        }

        return ok;
    }
}

// ---------------------------------------------------------------------------
// Публичные функции OneC
// ---------------------------------------------------------------------------

namespace OneC
{
    std::wstring FindIbasesFile()
    {
        const std::vector<std::wstring> cfgPaths = Find1CEStartCfgFiles();

        for (const auto& cfgPath : cfgPaths)
        {
            const std::vector<std::wstring> commonInfoBasesPaths =
                GetCommonInfoBasesValues(cfgPath);

            for (const auto& commonPath : commonInfoBasesPaths)
            {
                const std::wstring resolved =
                    ResolveIbasesFromCommonPath(commonPath, cfgPath);

                if (!resolved.empty())
                {
                    return resolved;
                }
            }
        }

        std::vector<std::wstring> candidates;

        std::wstring appData = GetEnvVarW(L"APPDATA");

        if (appData.empty())
        {
            appData = GetKnownFolderPathW(CSIDL_APPDATA);
        }

        if (!appData.empty())
        {
            candidates.push_back(appData + L"\\1C\\1CEStart\\ibases.v8i");
            candidates.push_back(appData + L"\\1C\\1cv8\\ibases.v8i");
        }

        std::wstring localAppData = GetEnvVarW(L"LOCALAPPDATA");

        if (localAppData.empty())
        {
            localAppData = GetKnownFolderPathW(CSIDL_LOCAL_APPDATA);
        }

        if (!localAppData.empty())
        {
            candidates.push_back(localAppData + L"\\1C\\1CEStart\\ibases.v8i");
            candidates.push_back(localAppData + L"\\1C\\1cv8\\ibases.v8i");
        }

        UniqueStrings(candidates);

        for (const auto& path : candidates)
        {
            if (FileExists(path))
            {
                return path;
            }
        }

        return std::wstring();
    }

    std::vector<BaseInfo> ParseIBasesFile(const std::wstring& path)
    {
        const std::vector<BYTE> bytes = ReadFileBytes(path);

        if (bytes.empty())
        {
            return {};
        }

        const std::wstring text = DecodeText(bytes);
        const std::vector<std::wstring> lines = SplitLinesW(text);

        std::vector<BaseInfo> result;

        BaseInfo current;
        bool inSection = false;

        auto pushCurrent = [&]()
            {
                if (inSection && !current.name.empty())
                {
                    result.push_back(current);
                }
            };

        for (const std::wstring& rawLine : lines)
        {
            std::wstring line = TrimW(rawLine);

            if (line.empty())
            {
                continue;
            }

            if (line.front() == L'[' && line.back() == L']')
            {
                if (!current.connect.empty())
                {
                    pushCurrent();
                }
                
                current = BaseInfo();

                current.name = TrimW(line.substr(1, line.size() - 2));
                inSection = !current.name.empty();

                continue;
            }

            if (!inSection)
            {
                continue;
            }

            const size_t eq = line.find(L'=');

            if (eq == std::wstring::npos || eq == 0)
            {
                continue;
            }

            const std::wstring key =
                ToLowerW(TrimW(line.substr(0, eq)));

            const std::wstring value =
                TrimW(line.substr(eq + 1));

            if (key == L"id")
            {
                current.id = UnquoteW(value);
            }
            else if (key == L"connect")
            {
                current.connect = value;

                ParseConnectString(value, current);
            }
            else if (key == L"name")
            {
                const std::wstring name = UnquoteW(value);

                if (!name.empty())
                {
                    current.name = name;
                }
            }
            else if (key == L"user" || key == L"usr")
            {
                current.user = UnquoteW(value);
            }
            else if (key == L"srvr" || key == L"server")
            {
                current.server = UnquoteW(value);
            }
            else if (key == L"ref")
            {
                current.ref = UnquoteW(value);
            }
        }

        pushCurrent();

        return result;
    }

    std::wstring Find1CExe()
    {
        const std::wstring registryExe = Find1CEStartFromRegistry();

        if (!registryExe.empty())
        {
            return registryExe;
        }

        const std::wstring commonExe = Find1CEStartInCommonFolders();

        if (!commonExe.empty())
        {
            return commonExe;
        }

        std::vector<std::wstring> roots;

        const std::wstring programFiles = GetEnvVarW(L"ProgramFiles");
        const std::wstring programFilesX86 = GetEnvVarW(L"ProgramFiles(x86)");

        if (!programFiles.empty())
        {
            roots.push_back(programFiles + L"\\1cv8");
        }

        if (!programFilesX86.empty())
        {
            roots.push_back(programFilesX86 + L"\\1cv8");
        }

        const std::wstring knownProgramFiles =
            GetKnownFolderPathW(CSIDL_PROGRAM_FILES);

        if (!knownProgramFiles.empty())
        {
            roots.push_back(knownProgramFiles + L"\\1cv8");
        }

        const std::wstring knownProgramFilesX86 =
            GetKnownFolderPathW(CSIDL_PROGRAM_FILESX86);

        if (!knownProgramFilesX86.empty())
        {
            roots.push_back(knownProgramFilesX86 + L"\\1cv8");
        }

        UniqueStrings(roots);

        std::wstring best;
        FILETIME bestTime = {};
        bool found = false;

        for (const auto& root : roots)
        {
            if (DirExists(root))
            {
                Find1CEStartRecursive(
                    root,
                    0,
                    best,
                    bestTime,
                    found
                );
            }
        }

        return best;
    }

    bool RunClearCache(
        const BaseInfo& base,
        const std::wstring& exePath,
        std::wstring& log
    )
    {
        if (exePath.empty() || !FileExists(exePath))
        {
            log = L"Не найден 1cestart.exe";
            return false;
        }

        std::wstring arguments = L"ENTERPRISE";

        if (!base.server.empty())
        {
            std::wstring connectionString = base.server;

            if (!base.ref.empty())
            {
                connectionString += L"\\";
                connectionString += base.ref;
            }

            arguments += L" /S ";
            arguments += QuoteArg(connectionString);
        }
        else if (!base.folder.empty())
        {
            arguments += L" /F ";
            arguments += QuoteArg(base.folder);
        }
        else
        {
            log = L"Не удалось определить параметры базы: нет Server/Ref или Folder.";
            return false;
        }

        arguments += L" /ClearCache";

        return RunProcessAndWait(exePath, arguments, 180000, log);
    }

    std::wstring BrowseForExe(
        void* ownerHwnd,
        const std::wstring& initial
    )
    {
        HWND owner = static_cast<HWND>(ownerHwnd);

        std::vector<wchar_t> buffer(32768, 0);

        const std::wstring init = initial.empty()
            ? L"1cestart.exe"
            : initial;

        for (size_t i = 0; i < init.size() && i + 1 < buffer.size(); ++i)
        {
            buffer[i] = init[i];
        }

        std::wstring filter = L"1cestart.exe";
        filter.push_back(L'\0');
        filter += L"1cestart.exe";
        filter.push_back(L'\0');
        filter.push_back(L'\0');

        OPENFILENAMEW ofn = {};

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile = buffer.data();
        ofn.nMaxFile = static_cast<DWORD>(buffer.size());
        ofn.lpstrTitle = L"Выберите 1cestart.exe";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if (::GetOpenFileNameW(&ofn))
        {
            return buffer.data();
        }

        return std::wstring();
    }

    CacheCleanupResult ClearSelectedCacheFolders(
        const std::vector<BaseInfo>& selected
    )
    {
        CacheCleanupResult result;

        const std::vector<std::wstring> cacheRoots = Get1CCacheRoots();

        for (const auto& base : selected)
        {
            std::wstring normalizedId;

            if (!TryNormalizeCacheId(base.id, normalizedId))
            {
                ++result.skipped;

                result.report += base.name;
                result.report += L": некорректный ID, папка кэша не удаляется\n";

                continue;
            }

            bool found = false;

            for (const auto& root : cacheRoots)
            {
                const std::wstring cacheDir = AppendPath(root, normalizedId);

                if (!DirExists(cacheDir))
                {
                    continue;
                }

                found = true;

                if (DeleteDirectoryRecursive(cacheDir))
                {
                    ++result.deleted;

                    result.report += L"Удалён: ";
                    result.report += cacheDir;
                    result.report += L"\n";
                }
                else
                {
                    ++result.failed;

                    result.report += L"Не удалось удалить: ";
                    result.report += cacheDir;
                    result.report += L"\n";
                }
            }

            if (!found)
            {
                ++result.skipped;

                result.report += base.name;
                result.report += L": папка кэша не найдена\n";
            }
        }

        if (result.report.empty())
        {
            result.report = L"Нет данных об удалении.";
        }

        return result;
    }

    std::vector<Running1CProcess> GetRunning1CProcesses()
    {
        return QueryRunning1CProcessesWMI();
    }

    bool IsProcessForBase(
        const BaseInfo& base,
        const Running1CProcess& process
    )
    {
        if (!base.name.empty() && !process.ibName.empty() &&
            ::lstrcmpiW(base.name.c_str(), process.ibName.c_str()) == 0)
        {
            return true;
        }

        if (!base.folder.empty() && !process.folder.empty())
        {
            const std::wstring baseFolder =
                NormalizePathForComparison(base.folder);

            if (::lstrcmpiW(baseFolder.c_str(), process.folder.c_str()) == 0)
            {
                return true;
            }
        }

        if (!base.server.empty() && !process.server.empty() &&
            ::lstrcmpiW(base.server.c_str(), process.server.c_str()) == 0)
        {
            if (base.ref.empty() && process.ref.empty())
            {
                return true;
            }

            if (!base.ref.empty() && !process.ref.empty() &&
                ::lstrcmpiW(base.ref.c_str(), process.ref.c_str()) == 0)
            {
                return true;
            }
        }

        return base.server.empty() &&
            !base.ref.empty() &&
            !process.ref.empty() &&
            ::lstrcmpiW(base.ref.c_str(), process.ref.c_str()) == 0;
    }

    std::vector<uint32_t> GetProcessIdsForBases(
        const std::vector<BaseInfo>& bases,
        const std::vector<Running1CProcess>& processes
    )
    {
        std::vector<uint32_t> result;

        for (const auto& process : processes)
        {
            for (const auto& base : bases)
            {
                if (IsProcessForBase(base, process))
                {
                    result.push_back(process.processId);
                    break;
                }
            }
        }

        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());

        return result;
    }

    bool CloseProcessesGracefully(
        const std::vector<uint32_t>& processIds
    )
    {
        bool anyAction = false;

        for (const uint32_t processId : processIds)
        {
            std::vector<HWND> windows;

            EnumWindowsData data;
            data.processId = processId;
            data.windows = &windows;

            ::EnumWindows(
                EnumWindowsCallback,
                reinterpret_cast<LPARAM>(&data)
            );

            if (!windows.empty())
            {
                for (HWND hwnd : windows)
                {
                    if (::PostMessageW(hwnd, WM_CLOSE, 0, 0))
                    {
                        anyAction = true;
                    }
                }
            }
        }

        return anyAction;
    }

    bool WaitForProcessesToExit(
        const std::vector<uint32_t>& processIds,
        uint32_t timeoutMs
    )
    {
        std::vector<HANDLE> processHandles;

        for (const uint32_t processId : processIds)
        {
            HANDLE process = ::OpenProcess(
                SYNCHRONIZE,
                FALSE,
                processId
            );

            if (process != nullptr)
            {
                processHandles.push_back(process);
            }
            else if (::GetLastError() != ERROR_INVALID_PARAMETER)
            {
                for (HANDLE handle : processHandles)
                {
                    ::CloseHandle(handle);
                }

                return false;
            }
        }

        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeoutMs);

        bool allExited = false;

        while (true)
        {
            allExited = true;

            for (HANDLE handle : processHandles)
            {
                if (::WaitForSingleObject(handle, 0) == WAIT_TIMEOUT)
                {
                    allExited = false;
                    break;
                }
            }

            if (!allExited)
            {
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            else
            {
                break;
            }
        }

        for (HANDLE handle : processHandles)
        {
            ::CloseHandle(handle);
        }

        return allExited;
    }
}
