#pragma once

#include <afxwin.h>
#include <afxdlgs.h>
#include <afxcmn.h>
#include <afxdialogex.h>

#include <vector>

#include "resource.h"
#include "OneCHelpers.h"

#define WM_APP_UPDATE_STATUS (WM_APP + 1)

class COneCCacheCleanerDlg : public CDialogEx
{
public:
    COneCCacheCleanerDlg(CWnd* pParent = nullptr);

    enum { IDD = IDD_ONECCACHECLEANER_DIALOG };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();

    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnDestroy();

    afx_msg void OnBnClickedBrowse();
    afx_msg void OnBnClickedRefresh();
    afx_msg void OnBnClickedSelectAll();
    afx_msg void OnBnClickedDeselect();

    afx_msg void OnBnClickedClearSelected();
    afx_msg void OnBnClickedDeleteCache();
    afx_msg void OnBnClickedCloseSelected();

    afx_msg void OnNMClickBaseList(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnLvnItemchangedBaseList(NMHDR* pNMHDR, LRESULT* pResult);

    afx_msg LRESULT OnUpdateStatus(WPARAM wParam, LPARAM lParam);

    DECLARE_MESSAGE_MAP()

private:
    void LoadBases();
    void StartStatusUpdate();
    void UpdateCloseButtonState();

    std::vector<OneC::BaseInfo> GetCheckedBases();
    bool EnsureBasesAreClosed(
        const std::vector<OneC::BaseInfo>& bases,
        const std::wstring& actionText
    );
    bool CloseProcessesAndWait(const std::vector<uint32_t>& processIds);

    std::wstring GetExePath();
    void SetStatus(const std::wstring& text);

    void AutoResizeWindowToList();

private:
    CEdit m_exePath;
    CListCtrl m_baseList;
    CStatic m_status;

    std::vector<OneC::BaseInfo> m_bases;
    std::vector<bool> m_runningStates;

    BOOL m_statusUpdating = FALSE;
    BOOL m_destroying = FALSE;
    uint64_t m_statusGeneration = 0;
    HICON m_hIcon = nullptr;
};
