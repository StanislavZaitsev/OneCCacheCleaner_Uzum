#include "pch.h"
#include "OneCCacheCleanerDlg.h"

#include <commctrl.h>

#include <algorithm>
#include <thread>

namespace
{
    constexpr wchar_t kStatusOwnerProperty[] =
        L"OneCCacheCleaner.StatusOwner";

    struct StatusUpdatePayload
    {
        uint64_t generation = 0;
        std::vector<bool> states;
    };
}

BEGIN_MESSAGE_MAP(COneCCacheCleanerDlg, CDialogEx)
    ON_WM_TIMER()
    ON_WM_DESTROY()

    ON_BN_CLICKED(IDC_BROWSE, &COneCCacheCleanerDlg::OnBnClickedBrowse)
    ON_BN_CLICKED(IDC_REFRESH, &COneCCacheCleanerDlg::OnBnClickedRefresh)
    ON_BN_CLICKED(IDC_SELECT_ALL, &COneCCacheCleanerDlg::OnBnClickedSelectAll)
    ON_BN_CLICKED(IDC_DESELECT, &COneCCacheCleanerDlg::OnBnClickedDeselect)

    ON_BN_CLICKED(IDC_CLEAR_SELECTED, &COneCCacheCleanerDlg::OnBnClickedClearSelected)
    ON_BN_CLICKED(IDC_DELETE_CACHE, &COneCCacheCleanerDlg::OnBnClickedDeleteCache)
    ON_BN_CLICKED(IDC_CLOSE_SELECTED, &COneCCacheCleanerDlg::OnBnClickedCloseSelected)

    ON_MESSAGE(WM_APP_UPDATE_STATUS, &COneCCacheCleanerDlg::OnUpdateStatus)

    ON_NOTIFY(NM_CLICK, IDC_BASE_LIST, &COneCCacheCleanerDlg::OnNMClickBaseList)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_BASE_LIST, &COneCCacheCleanerDlg::OnLvnItemchangedBaseList)
END_MESSAGE_MAP()

COneCCacheCleanerDlg::COneCCacheCleanerDlg(CWnd* pParent)
    : CDialogEx(IDD, pParent)
{
}

void COneCCacheCleanerDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);

    DDX_Control(pDX, IDC_EXE_PATH, m_exePath);
    DDX_Control(pDX, IDC_BASE_LIST, m_baseList);
    DDX_Control(pDX, IDC_STATUS, m_status);
}

BOOL COneCCacheCleanerDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    SetWindowTextW(L"Очистка кэша 1С");

    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

    if (m_hIcon != nullptr)
    {
        SetIcon(m_hIcon, TRUE);
        SetIcon(m_hIcon, FALSE);
    }

    ::SetPropW(
        GetSafeHwnd(),
        kStatusOwnerProperty,
        reinterpret_cast<HANDLE>(this)
    );

    m_baseList.ModifyStyle(LVS_TYPEMASK, LVS_REPORT);

    m_baseList.SetExtendedStyle(
        LVS_EX_FULLROWSELECT |
        LVS_EX_CHECKBOXES
    );

    m_baseList.InsertColumn(0, L"База", LVCFMT_LEFT, 350);
    m_baseList.InsertColumn(1, L"ID", LVCFMT_LEFT, 280);
    m_baseList.InsertColumn(2, L"Статус", LVCFMT_LEFT, 120);
    m_baseList.InsertColumn(3, L"Закрыть", LVCFMT_CENTER, 80);

    GetDlgItem(IDC_CLOSE_SELECTED)->EnableWindow(FALSE);

    const std::wstring exePath = OneC::Find1CExe();

    if (!exePath.empty())
    {
        m_exePath.SetWindowTextW(exePath.c_str());
    }

    LoadBases();

    SetTimer(1, 2000, nullptr);

    return TRUE;
}

void COneCCacheCleanerDlg::OnDestroy()
{
    KillTimer(1);

    m_destroying = TRUE;
    ++m_statusGeneration;
    ::RemovePropW(GetSafeHwnd(), kStatusOwnerProperty);

    CDialogEx::OnDestroy();
}

void COneCCacheCleanerDlg::SetStatus(const std::wstring& text)
{
    m_status.SetWindowTextW(text.c_str());
}

std::wstring COneCCacheCleanerDlg::GetExePath()
{
    CString text;

    m_exePath.GetWindowTextW(text);

    return static_cast<LPCWSTR>(text);
}

void COneCCacheCleanerDlg::LoadBases()
{
    ++m_statusGeneration;
    m_statusUpdating = FALSE;

    m_baseList.DeleteAllItems();
    m_bases.clear();
    m_runningStates.clear();

    const std::wstring ibasesPath = OneC::FindIbasesFile();

    if (ibasesPath.empty())
    {
        SetStatus(L"ibases.v8i не найден.");
        return;
    }

    m_bases = OneC::ParseIBasesFile(ibasesPath);

    for (size_t i = 0; i < m_bases.size(); ++i)
    {
        const int itemIndex =
            m_baseList.InsertItem(
                m_baseList.GetItemCount(),
                m_bases[i].name.c_str()
            );

        m_baseList.SetItemText(
            itemIndex,
            1,
            m_bases[i].id.c_str()
        );

        m_baseList.SetItemText(
            itemIndex,
            2,
            L""
        );

        m_baseList.SetItemData(
            itemIndex,
            static_cast<DWORD_PTR>(i)
        );
    }

    m_runningStates.assign(m_bases.size(), false);

    std::wstring status = L"Найдено баз: ";
    status += std::to_wstring(m_bases.size());

    SetStatus(status);

    StartStatusUpdate();

    AutoResizeWindowToList();
}

std::vector<OneC::BaseInfo> COneCCacheCleanerDlg::GetCheckedBases()
{
    std::vector<OneC::BaseInfo> result;

    const int count = m_baseList.GetItemCount();

    for (int i = 0; i < count; ++i)
    {
        if (ListView_GetCheckState(m_baseList.GetSafeHwnd(), i))
        {
            const DWORD_PTR index = m_baseList.GetItemData(i);

            if (index < m_bases.size())
            {
                result.push_back(m_bases[index]);
            }
        }
    }

    return result;
}

void COneCCacheCleanerDlg::UpdateCloseButtonState()
{
    bool checkedAndRunning = false;
    const int count = m_baseList.GetItemCount();

    for (int item = 0; item < count; ++item)
    {
        if (!ListView_GetCheckState(m_baseList.GetSafeHwnd(), item))
        {
            continue;
        }

        const DWORD_PTR index = m_baseList.GetItemData(item);

        if (index < m_runningStates.size() && m_runningStates[index])
        {
            checkedAndRunning = true;
            break;
        }
    }

    CWnd* closeButton = GetDlgItem(IDC_CLOSE_SELECTED);

    if (closeButton != nullptr)
    {
        closeButton->EnableWindow(checkedAndRunning ? TRUE : FALSE);
    }
}

bool COneCCacheCleanerDlg::CloseProcessesAndWait(
    const std::vector<uint32_t>& processIds
)
{
    SetStatus(L"Отправлена команда закрытия 1С. Ожидание завершения...");

    if (!OneC::CloseProcessesGracefully(processIds))
    {
        if (OneC::WaitForProcessesToExit(processIds, 250))
        {
            ++m_statusGeneration;
            m_statusUpdating = FALSE;
            StartStatusUpdate();

            SetStatus(L"Выбранные базы уже закрыты. Выполнение продолжается...");
            return true;
        }

        AfxMessageBox(
            L"Не удалось найти окно 1С для закрытия. "
            L"Закройте базу вручную и повторите операцию.",
            MB_OK | MB_ICONERROR
        );

        SetStatus(L"Операция отменена: базу не удалось закрыть.");
        return false;
    }

    if (!OneC::WaitForProcessesToExit(processIds, 30000))
    {
        AfxMessageBox(
            L"Не все процессы 1С завершились за 30 секунд. "
            L"Возможно, 1С ожидает подтверждения сохранения данных. "
            L"Завершите закрытие и повторите операцию.",
            MB_OK | MB_ICONWARNING
        );

        SetStatus(L"Операция отменена: выбранные базы всё ещё запущены.");

        ++m_statusGeneration;
        m_statusUpdating = FALSE;
        StartStatusUpdate();

        return false;
    }

    ++m_statusGeneration;
    m_statusUpdating = FALSE;
    StartStatusUpdate();

    SetStatus(L"Выбранные базы закрыты. Выполнение продолжается...");
    return true;
}

bool COneCCacheCleanerDlg::EnsureBasesAreClosed(
    const std::vector<OneC::BaseInfo>& bases,
    const std::wstring& actionText
)
{
    CWaitCursor wait;

    const std::vector<OneC::Running1CProcess> processes =
        OneC::GetRunning1CProcesses();

    const std::vector<uint32_t> processIds =
        OneC::GetProcessIdsForBases(bases, processes);

    wait.Restore();

    if (processIds.empty())
    {
        return true;
    }

    std::wstring message = L"Перед ";
    message += actionText;
    message += L" необходимо закрыть запущенные базы:\n\n";

    for (const auto& base : bases)
    {
        const bool running = std::any_of(
            processes.begin(),
            processes.end(),
            [&](const OneC::Running1CProcess& process)
            {
                return OneC::IsProcessForBase(base, process);
            }
        );

        if (running)
        {
            message += L"• ";
            message += base.name;
            message += L"\n";
        }
    }

    message += L"\nЗакрыть их и продолжить?";

    if (AfxMessageBox(
        message.c_str(),
        MB_YESNO | MB_ICONQUESTION) != IDYES)
    {
        SetStatus(L"Операция отменена пользователем.");
        return false;
    }

    return CloseProcessesAndWait(processIds);
}

void COneCCacheCleanerDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1)
    {
        StartStatusUpdate();
    }

    CDialogEx::OnTimer(nIDEvent);
}

void COneCCacheCleanerDlg::StartStatusUpdate()
{
    if (m_statusUpdating)
    {
        return;
    }

    if (m_bases.empty() || m_destroying)
    {
        return;
    }

    m_statusUpdating = TRUE;

    HWND hwnd = GetSafeHwnd();
    const uint64_t generation = m_statusGeneration;
    const std::vector<OneC::BaseInfo> bases = m_bases;
    const void* ownerToken = this;

    std::thread(
        [hwnd, generation, bases, ownerToken]()
        {
            const std::vector<OneC::Running1CProcess> processes =
                OneC::GetRunning1CProcesses();

            StatusUpdatePayload payload;
            payload.generation = generation;
            payload.states.assign(bases.size(), false);

            for (size_t i = 0; i < bases.size(); ++i)
            {
                for (const auto& process : processes)
                {
                    if (OneC::IsProcessForBase(bases[i], process))
                    {
                        payload.states[i] = true;
                        break;
                    }
                }
            }

            if (::IsWindow(hwnd) &&
                ::GetPropW(hwnd, kStatusOwnerProperty) == ownerToken)
            {
                ::SendMessageW(
                    hwnd,
                    WM_APP_UPDATE_STATUS,
                    0,
                    reinterpret_cast<LPARAM>(&payload)
                );
            }
        }
    ).detach();
}

LRESULT COneCCacheCleanerDlg::OnUpdateStatus(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);

    const StatusUpdatePayload* payload =
        reinterpret_cast<const StatusUpdatePayload*>(lParam);

    if (!payload || m_destroying ||
        payload->generation != m_statusGeneration)
    {
        return 0;
    }

    if (!payload->states.empty())
    {
        m_runningStates = payload->states;
        m_runningStates.resize(m_bases.size(), false);

        const int count = m_baseList.GetItemCount();

        for (int i = 0; i < count; ++i)
        {
            const DWORD_PTR index = m_baseList.GetItemData(i);
            const bool isRunning =
                index < m_runningStates.size() && m_runningStates[index];

            if (isRunning)
            {
                m_baseList.SetItemText(i, 2, L"Запущена");
                m_baseList.SetItemText(i, 3, L"✖");
            }
            else
            {
                m_baseList.SetItemText(i, 2, L"");
                m_baseList.SetItemText(i, 3, L"");
            }
        }
    }
    else
    {
        m_runningStates.assign(m_bases.size(), false);
    }

    UpdateCloseButtonState();

    m_statusUpdating = FALSE;

    return 0;
}

void COneCCacheCleanerDlg::OnBnClickedBrowse()
{
    const std::wstring currentPath = GetExePath();

    const std::wstring selectedPath =
        OneC::BrowseForExe(
            GetSafeHwnd(),
            currentPath
        );

    if (!selectedPath.empty())
    {
        m_exePath.SetWindowTextW(selectedPath.c_str());
    }
}

void COneCCacheCleanerDlg::OnBnClickedRefresh()
{
    LoadBases();
}

void COneCCacheCleanerDlg::OnBnClickedSelectAll()
{
    const int count = m_baseList.GetItemCount();

    for (int i = 0; i < count; ++i)
    {
        ListView_SetCheckState(m_baseList.GetSafeHwnd(), i, TRUE);
    }
}

void COneCCacheCleanerDlg::OnBnClickedDeselect()
{
    const int count = m_baseList.GetItemCount();

    for (int i = 0; i < count; ++i)
    {
        ListView_SetCheckState(m_baseList.GetSafeHwnd(), i, FALSE);
    }
}

void COneCCacheCleanerDlg::OnBnClickedClearSelected()
{
    const std::vector<OneC::BaseInfo> selected = GetCheckedBases();

    if (selected.empty())
    {
        AfxMessageBox(
            L"Отметьте хотя бы одну базу.",
            MB_OK | MB_ICONINFORMATION
        );

        return;
    }

    const std::wstring exePath = GetExePath();

    if (exePath.empty() || !OneC::FileExists(exePath))
    {
        AfxMessageBox(
            L"Укажите существующий путь к 1cestart.exe.",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    if (!EnsureBasesAreClosed(selected, L"очисткой кэша через 1С"))
    {
        return;
    }

    CWaitCursor wait;

    int okCount = 0;
    int failCount = 0;

    std::wstring report;

    for (const auto& base : selected)
    {
        std::wstring log;

        const bool ok = OneC::RunClearCache(
            base,
            exePath,
            log
        );

        report += base.name;
        report += L"\n";

        if (ok)
        {
            report += L"Успешно\n\n";
            ++okCount;
        }
        else
        {
            report += L"Ошибка или не подтверждено\n\n";
            ++failCount;
        }
    }

    wait.Restore();

    std::wstring summary = L"Обработано баз: ";
    summary += std::to_wstring(selected.size());
    summary += L"\nУспешно: ";
    summary += std::to_wstring(okCount);
    summary += L"\nС проблемами: ";
    summary += std::to_wstring(failCount);
    summary += L"\n\n";
    summary += report;

    AfxMessageBox(
        summary.c_str(),
        MB_OK | MB_ICONINFORMATION
    );

    std::wstring status = L"Очистка через 1С завершена. Успешно: ";
    status += std::to_wstring(okCount);
    status += L", с проблемами: ";
    status += std::to_wstring(failCount);
    status += L".";
    SetStatus(status);
}

void COneCCacheCleanerDlg::OnBnClickedDeleteCache()
{
    const std::vector<OneC::BaseInfo> selected = GetCheckedBases();

    if (selected.empty())
    {
        AfxMessageBox(
            L"Отметьте хотя бы одну базу.",
            MB_OK | MB_ICONINFORMATION
        );

        return;
    }

    if (!EnsureBasesAreClosed(selected, L"удалением файлов кэша"))
    {
        return;
    }

    const int answer = AfxMessageBox(
        L"Удалить папки кэша для выбранных баз?",
        MB_YESNO | MB_ICONWARNING
    );

    if (answer != IDYES)
    {
        return;
    }

    CWaitCursor wait;

    const OneC::CacheCleanupResult result =
        OneC::ClearSelectedCacheFolders(selected);

    wait.Restore();

    std::wstring message = L"Удалено: ";
    message += std::to_wstring(result.deleted);
    message += L"\nОшибок: ";
    message += std::to_wstring(result.failed);
    message += L"\nПропущено: ";
    message += std::to_wstring(result.skipped);
    message += L"\n\n";
    message += result.report;

    AfxMessageBox(
        message.c_str(),
        MB_OK | MB_ICONINFORMATION
    );

    std::wstring status = L"Удаление кэша завершено. Удалено: ";
    status += std::to_wstring(result.deleted);
    status += L", ошибок: ";
    status += std::to_wstring(result.failed);
    status += L", пропущено: ";
    status += std::to_wstring(result.skipped);
    status += L".";
    SetStatus(status);
}

void COneCCacheCleanerDlg::OnBnClickedCloseSelected()
{
    const std::vector<OneC::BaseInfo> selected = GetCheckedBases();

    if (selected.empty())
    {
        AfxMessageBox(
            L"Отметьте хотя бы одну базу.",
            MB_OK | MB_ICONINFORMATION
        );

        return;
    }

    CWaitCursor wait;

    const std::vector<OneC::Running1CProcess> processes =
        OneC::GetRunning1CProcesses();

    const std::vector<uint32_t> processIds =
        OneC::GetProcessIdsForBases(selected, processes);

    wait.Restore();

    if (processIds.empty())
    {
        AfxMessageBox(
            L"Не найдены запущенные процессы для выбранных баз.",
            MB_OK | MB_ICONINFORMATION
        );

        return;
    }

    std::wstring message =
        L"Закрыть запущенные процессы для выбранных баз?\n\n";

    message += L"Найдено процессов: ";
    message += std::to_wstring(processIds.size());

    const int answer = AfxMessageBox(
        message.c_str(),
        MB_YESNO | MB_ICONQUESTION
    );

    if (answer != IDYES)
    {
        return;
    }

    if (CloseProcessesAndWait(processIds))
    {
        SetStatus(L"Выбранные базы закрыты.");
    }
}

void COneCCacheCleanerDlg::OnNMClickBaseList(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;

    if (pNMHDR == nullptr)
    {
        return;
    }

    LPNMITEMACTIVATE pNMItem =
        reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);

    const int item = pNMItem->iItem;
    const int subItem = pNMItem->iSubItem;

    if (item < 0)
    {
        return;
    }

    // Клик только по колонке "Закрыть"
    if (subItem != 3)
    {
        return;
    }

    const DWORD_PTR index = m_baseList.GetItemData(item);

    if (index >= m_bases.size() ||
        index >= m_runningStates.size() ||
        !m_runningStates[index])
    {
        return;
    }

    const OneC::BaseInfo& base = m_bases[index];

    std::wstring message =
        L"Закрыть запущенные процессы базы \"";

    message += base.name;
    message += L"\"?";

    const int answer = AfxMessageBox(
        message.c_str(),
        MB_YESNO | MB_ICONQUESTION
    );

    if (answer != IDYES)
    {
        return;
    }

    CWaitCursor wait;

    const std::vector<OneC::Running1CProcess> processes =
        OneC::GetRunning1CProcesses();

    const std::vector<uint32_t> processIds =
        OneC::GetProcessIdsForBases({ base }, processes);

    wait.Restore();

    if (processIds.empty())
    {
        AfxMessageBox(
            L"Не найдены запущенные процессы для этой базы.",
            MB_OK | MB_ICONINFORMATION
        );

        StartStatusUpdate();

        return;
    }

    if (CloseProcessesAndWait(processIds))
    {
        SetStatus(L"База закрыта.");
    }
}
void COneCCacheCleanerDlg::OnLvnItemchangedBaseList(NMHDR* pNMHDR, LRESULT* pResult)
{
    UNREFERENCED_PARAMETER(pNMHDR);

    UpdateCloseButtonState();
    *pResult = 0;
}

void COneCCacheCleanerDlg::AutoResizeWindowToList()
{
    if (!m_baseList.GetSafeHwnd())
    {
        return;
    }

    CHeaderCtrl* pHeader = m_baseList.GetHeaderCtrl();

    if (pHeader == nullptr)
    {
        return;
    }

    const int columnCount = pHeader->GetItemCount();

    if (columnCount <= 0)
    {
        return;
    }

    int totalListWidth = 0;

    for (int i = 0; i < columnCount; ++i)
    {
        totalListWidth += m_baseList.GetColumnWidth(i);
    }

    // Запас на чекбоксы, вертикальный скроллбар и небольшие границы.
    totalListWidth += 30;
    totalListWidth += ::GetSystemMetrics(SM_CXVSCROLL);

    CRect rcList;
    m_baseList.GetWindowRect(&rcList);
    ScreenToClient(&rcList);

    const int leftMargin = rcList.left;
    const int rightMargin = (leftMargin > 0) ? leftMargin : 10;

    const int desiredClientWidth =
        leftMargin + totalListWidth + rightMargin;

    CRect rcClient;
    GetClientRect(&rcClient);

    // Чтобы случайно не сузить окно сильнее, чем нужно для других контролов,
    // уменьшать ширину не будем.
    const int newClientWidth =
        (desiredClientWidth > rcClient.Width())
        ? desiredClientWidth
        : rcClient.Width();

    if (newClientWidth == rcClient.Width())
    {
        return;
    }

    // Меняем ширину самого списка.
    m_baseList.SetWindowPos(
        nullptr,
        0,
        0,
        totalListWidth,
        rcList.Height(),
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
    );

    // Меняем ширину всего диалога.
    CRect rcWindow;
    GetWindowRect(&rcWindow);

    const int deltaWidth = newClientWidth - rcClient.Width();

    SetWindowPos(
        nullptr,
        0,
        0,
        rcWindow.Width() + deltaWidth,
        rcWindow.Height(),
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
    );
}
