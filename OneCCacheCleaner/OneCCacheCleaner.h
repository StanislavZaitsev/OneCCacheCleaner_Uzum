#pragma once

#ifndef __AFXWIN_H__
#error include 'pch.h' before including this file for PCH
#endif

#include "resource.h"

class COneCCacheCleanerApp : public CWinApp
{
public:
    COneCCacheCleanerApp() noexcept {}

    virtual BOOL InitInstance();
};