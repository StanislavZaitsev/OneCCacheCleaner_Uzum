#include "pch.h"

#include "OneCCacheCleaner.h"
#include "OneCCacheCleanerDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// Глобальный объект приложения.
// Обычно он уже есть в шаблоне MFC.
COneCCacheCleanerApp theApp;

BOOL COneCCacheCleanerApp::InitInstance()
{
    // Инициализация общих контролов Windows.
    // Нужна для нормального ListView, кнопок и т.п.
    INITCOMMONCONTROLSEX icc = {};

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES;

    ::InitCommonControlsEx(&icc);

    // Вызываем InitInstance базового класса.
    // __super сам подставит нужный базовый класс:
    // CWinApp или CWinAppEx.
    __super::InitInstance();

    // Создаём и показываем главный диалог.
    COneCCacheCleanerDlg dlg;

    m_pMainWnd = &dlg;

    dlg.DoModal();

    // Диалог закрыт, приложение может завершиться.
    return FALSE;
}