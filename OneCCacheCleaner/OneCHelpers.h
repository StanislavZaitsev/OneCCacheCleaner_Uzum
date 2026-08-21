#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace OneC
{
    // Информация о базе из ibases.v8i
    struct BaseInfo
    {
        std::wstring name;
        std::wstring id;

        std::wstring folder;
        std::wstring server;
        std::wstring ref;
        std::wstring user;

        std::wstring connect;
    };

    // Результат удаления папок кэша
    struct CacheCleanupResult
    {
        int deleted = 0;
        int failed = 0;
        int skipped = 0;

        std::wstring report;
    };

    // Информация о запущенном процессе 1С
    struct Running1CProcess
    {
        uint32_t processId = 0;
        std::wstring commandLine;
        std::wstring ibName;
        std::wstring folder;
        std::wstring server;
        std::wstring ref;
    };

    // Проверка существования файла
    bool FileExists(const std::wstring& path);

    // Проверка существования каталога
    bool DirExists(const std::wstring& path);

    // Поиск ibases.v8i с учётом 1CEStart.cfg / CommonInfoBases
    std::wstring FindIbasesFile();

    // Разбор ibases.v8i
    std::vector<BaseInfo> ParseIBasesFile(const std::wstring& path);

    // Поиск 1cestart.exe
    // Название функции оставлено Find1CExe, чтобы было меньше изменений.
    std::wstring Find1CExe();

    // Запуск очистки кэша базы через 1cestart.exe
    bool RunClearCache(
        const BaseInfo& base,
        const std::wstring& exePath,
        std::wstring& log
    );

    // Диалог выбора 1cestart.exe
    std::wstring BrowseForExe(
        void* ownerHwnd,
        const std::wstring& initial
    );

    // Удаление папок кэша для выбранных баз
    CacheCleanupResult ClearSelectedCacheFolders(
        const std::vector<BaseInfo>& selected
    );

    // Получить список запущенных процессов 1С
    std::vector<Running1CProcess> GetRunning1CProcesses();

    // Проверить, относится ли процесс к базе из ibases.v8i
    bool IsProcessForBase(
        const BaseInfo& base,
        const Running1CProcess& process
    );

    // Получить PID процессов для выбранных баз из готового снимка WMI
    std::vector<uint32_t> GetProcessIdsForBases(
        const std::vector<BaseInfo>& bases,
        const std::vector<Running1CProcess>& processes
    );

    // Аккуратно закрыть процессы по PID
    bool CloseProcessesGracefully(
        const std::vector<uint32_t>& processIds
    );

    // Дождаться завершения ранее найденных процессов
    bool WaitForProcessesToExit(
        const std::vector<uint32_t>& processIds,
        uint32_t timeoutMs
    );
}
