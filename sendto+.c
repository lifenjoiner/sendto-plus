/* cl.exe /MD /Os /DUNICODE /D_UNICODE sendto+.c Ole32.lib shell32.lib user32.lib Comdlg32.lib Shlwapi.lib
https://msdn.microsoft.com/en-us/library/cc144093.aspx
https://msdn.microsoft.com/en-us/library/windows/desktop/bb776914.aspx
https://msdn.microsoft.com/en-us/library/windows/desktop/bb776885.aspx
GetCurrentDirectory()
*/

#ifndef UNICODE
#define T_MAX_PATH MAX_PATH
#else
#define T_MAX_PATH 32767
#endif

#include <tchar.h>

#define STRICT
#include <windows.h>
#ifndef RC_INVOKED
#include <windowsx.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <commdlg.h>
#endif

#define IDM_SENDTOFIRST 0

TCHAR   *FOLDER_SENDTO;
UINT    idm_t = IDM_SENDTOFIRST;
TCHAR   **PSENTTO;                          /* store the shourtcuts full path */

HINSTANCE       g_hinst;                /* My hinstance */
HMENU           g_hmenuSendTo;          /* Our SendTo popup */
LPSHELLFOLDER   g_psfDesktop;           /* The desktop folder */

LPSHELLFOLDER PIDL2PSF(LPITEMIDLIST pidl)
{
    HRESULT hres;
    LPSHELLFOLDER psf;

    if (pidl) {
		hres = g_psfDesktop->lpVtbl->BindToObject(g_psfDesktop,
                    pidl, NULL, &IID_IShellFolder, (LPVOID *)&psf);
        if (SUCCEEDED(hres)) {
            /* Woo-hoo, we're done */
        } else {
            psf = NULL;
        }
    } else {
        psf = NULL;
    }
    return psf;
}

LPITEMIDLIST PidlFromPath(HWND hwnd, LPCTSTR pszPath)
{
    LPITEMIDLIST pidl;
    ULONG ulEaten;
    DWORD dwAttributes;
    HRESULT hres;
	WCHAR *wszName;
	//
	wszName = calloc(T_MAX_PATH, sizeof(WCHAR));
#ifdef UNICODE
	if (FAILED(StringCchCopy(wszName, T_MAX_PATH, pszPath))) {
		return NULL;
	}
#else
    if (!MultiByteToWideChar(CP_ACP, 0, pszPath, -1, wszName, T_MAX_PATH)) {
        return NULL;
    }
#endif

    hres = g_psfDesktop->lpVtbl->ParseDisplayName(g_psfDesktop, hwnd,
                         NULL, wszName, &ulEaten, &pidl, &dwAttributes);
    free(wszName);
    if (FAILED(hres)) {
        return NULL;
    }

    return pidl;
}

LPSHELLFOLDER GetFolder(HWND hwnd, LPCTSTR pszPath)
{
    LPITEMIDLIST pidl;

    pidl = PidlFromPath(hwnd, pszPath);

    return PIDL2PSF(pidl);
}

/*****************************************************************************
 *  GetUIObjectOfAbsPidl
 *      Given an absolute (desktop-relative) LPITEMIDLIST, get the
 *      specified UI object.
 *****************************************************************************/

HRESULT GetUIObjectOfAbsPidl(HWND hwnd, LPITEMIDLIST pidl, REFIID riid, LPVOID *ppvOut)
{
    /*
     *  To get the UI object of an absolute pidl, we must first bind
     *  to its parent, and then call GetUIObjectOf on the last part.
     */

    LPITEMIDLIST pidlLast;
    LPSHELLFOLDER psf;
    HRESULT hres;
    /* Just for safety's sake. */
    *ppvOut = NULL;
	hres = SHBindToParent(pidl, &IID_IShellFolder, (LPVOID *)&psf, &pidlLast);
    if (FAILED(hres)) {
        return hres;
    }

    /* Now ask the parent for the the UI object of the child. */
    hres = psf->lpVtbl->GetUIObjectOf(psf, hwnd, 1, &pidlLast,
                                riid, NULL, ppvOut);

    /*
     *  Regardless of whether or not the GetUIObjectOf succeeded,
     *  we have no further use for the parent folder.
     */
    psf->lpVtbl->Release(psf);

    return hres;
}

/*****************************************************************************
 *  GetUIObjectOfPath
 *      Given an absolute path, get its specified UI object.
 *****************************************************************************/

HRESULT GetUIObjectOfPath(HWND hwnd, LPCTSTR pszPath, REFIID riid, LPVOID *ppvOut)
{
    LPITEMIDLIST pidl;
    HRESULT hres;

    /* Just for safety's sake. */
    *ppvOut = NULL;

    pidl = PidlFromPath(hwnd, pszPath);
    if (!pidl) {
        return E_FAIL;
    }

    hres = GetUIObjectOfAbsPidl(hwnd, pidl, riid, ppvOut);

    CoTaskMemFree(pidl);

    return hres;
}

/*****************************************************************************
 *  DoDrop
 *      Drop a data object on a drop target.
 *****************************************************************************/

void DoDrop(LPDATAOBJECT pdto, LPDROPTARGET pdt)
{
    POINTL pt = { 0, 0 };
    DWORD dwEffect;
    HRESULT hres;

    /*
     *  The data object enters the drop target via the left button
     *  with all drop effects permitted.
     */
    dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
    hres = pdt->lpVtbl->DragEnter(pdt, pdto, MK_LBUTTON, pt, &dwEffect);
    if (SUCCEEDED(hres) && dwEffect) {
        /* The drop target likes the data object and the effect. Go drop it. */
        hres = pdt->lpVtbl->Drop(pdt, pdto, MK_LBUTTON, pt, &dwEffect);

    } else {
        /*
         *  The drop target didn't like us.  Tell it we're leaving,
         *  sorry to bother you.
         */
        hres = pdt->lpVtbl->DragLeave(pdt);
    }
}

LPTSTR pidl_to_name(LPSHELLFOLDER psf, LPITEMIDLIST pidl, SHGDNF uFlags) {
    HRESULT hres;
    STRRET str;
    LPTSTR pszName = NULL;
    //
    hres = psf->lpVtbl->GetDisplayNameOf(psf, pidl, uFlags, &str);
    if (hres == S_OK) {
        hres = StrRetToStr(&str, pidl, &pszName);
    }
    return pszName;
}

void FolderToMenu(HWND hwnd, HMENU hmenu, LPCTSTR pszFolder)
{
    MENUITEMINFO mii;
    LPSHELLFOLDER psf;
    LPITEMIDLIST pidl;
    LPENUMIDLIST peidl;
    HRESULT hres;
    STRRET str;

    psf = GetFolder(hwnd, pszFolder);
    if (psf) {
        hres = psf->lpVtbl->EnumObjects(psf, hwnd,
                    SHCONTF_FOLDERS | SHCONTF_NONFOLDERS,
                    &peidl);
        if (SUCCEEDED(hres)) {
            while (peidl->lpVtbl->Next(peidl, 1, &pidl, NULL) == S_OK) {
                LPTSTR pszPath, pszName;
                UINT len;
                //
                pszPath = pidl_to_name(psf, pidl, SHGDN_FORPARSING);
                if (pszPath == NULL) {continue;}
                pszName = pidl_to_name(psf, pidl, SHGDN_NORMAL);
                if (pszName == NULL) {continue;}
                //
                // store rather than retrial
                PSENTTO = (TCHAR**)realloc(PSENTTO, sizeof(TCHAR*) * (idm_t + 1));
                if (PSENTTO == NULL) {continue;}
                PSENTTO[idm_t] = StrDup(pszPath);
                //
                if (PathIsDirectory(pszPath)) {
                    HMENU hSubMenu = CreatePopupMenu();
                    if (AppendMenu(hmenu, MF_ENABLED | MF_POPUP | MF_STRING, (UINT)hSubMenu, pszName)) {
                        idm_t++;
                        FolderToMenu(hwnd, hSubMenu, pszPath);
                    }
                }
                else {
                    if (AppendMenu(hmenu, MF_ENABLED | MF_STRING, idm_t, pszName)) {
                        mii.cbSize = sizeof(mii);
                        mii.fMask = MIIM_DATA;
                        mii.dwItemData = (ULONG_PTR)pidl;
                        SetMenuItemInfo(hmenu, idm_t, FALSE, &mii);
                        idm_t++;
                    }
                }
                //
                CoTaskMemFree(pszPath);
                CoTaskMemFree(pszName);
            }
            peidl->lpVtbl->Release(peidl);
        }
        psf->lpVtbl->Release(psf);

    }

    if (idm_t == IDM_SENDTOFIRST) {
        AppendMenu(hmenu, MF_GRAYED | MF_DISABLED | MF_STRING,
                   idm_t, TEXT("Send what sent to me to my sendto ^_^"));
    }
}

void SendTo_BuildSendToMenu(HWND hwnd, HMENU hmenu)
{
    FolderToMenu(hwnd, hmenu, FOLDER_SENDTO);
}

void SendTo_OnInitMenuPopup(HWND hwnd, HMENU hmenu, UINT item, BOOL fSystemMenu)
{
    if (hmenu == g_hmenuSendTo) { /* :p only top level */
        SendTo_BuildSendToMenu(hwnd, hmenu);
    }
}

void SendTo_SendToItem(HWND hwnd, int idm)
{
    LPDATAOBJECT pdto;
    LPDROPTARGET pdt;
    HRESULT hres;
    int i;

    if (__argc == 1) {
        ShellExecute(NULL, NULL, PSENTTO[idm], NULL, NULL, SW_SHOWDEFAULT);
    }
    else {
        hres = GetUIObjectOfPath(hwnd, PSENTTO[idm], &IID_IDropTarget, (LPVOID *)&pdt);
        if (SUCCEEDED(hres)) {
            for (i = 1; i < __argc; i++) {
                /* First convert our filename to a data object. */
                hres = GetUIObjectOfPath(hwnd, __targv[i], &IID_IDataObject, (LPVOID *)&pdto);
                if (SUCCEEDED(hres)) {
                        /* Now drop the file on the drop target. */
                        DoDrop(pdto, pdt);
                        pdt->lpVtbl->Release(pdt);
                }
            }
        }
        pdto->lpVtbl->Release(pdto);
    }
    // Exit anyway!
    PostQuitMessage(0);
}

void SendTo_OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
    SendTo_SendToItem(hwnd, id);
}

BOOL SendTo_OnCreate(HWND hwnd, LPCREATESTRUCT lpCreateStruct)
{
    g_hmenuSendTo = CreatePopupMenu();

    return TRUE;
}

/* !!! */
void SendTo_OnSetFocus(HWND hwnd, HWND hwndOldFocus)
{
    POINT pt;
    //
    if (GetCursorPos(&pt)) {
        TrackPopupMenu(g_hmenuSendTo, TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    }
}

void SendTo_OnKillFocus(HWND hwnd, HWND hwndOldFocus)
{
    PostQuitMessage(0);
}

LRESULT CALLBACK SendTo_WndProc(HWND hwnd, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uiMsg) {

    HANDLE_MSG(hwnd, WM_CREATE, SendTo_OnCreate);

    HANDLE_MSG(hwnd, WM_SETFOCUS, SendTo_OnSetFocus);

    HANDLE_MSG(hwnd, WM_KILLFOCUS, SendTo_OnKillFocus);

    HANDLE_MSG(hwnd, WM_COMMAND, SendTo_OnCommand);

    HANDLE_MSG(hwnd, WM_INITMENUPOPUP, SendTo_OnInitMenuPopup);

    }

    return DefWindowProc(hwnd, uiMsg, wParam, lParam);
}

BOOL InitApp(void)
{
    WNDCLASS wc;
    HRESULT hr;

    wc.style = 0;
    wc.lpfnWndProc = SendTo_WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = TEXT("SendTo+");

    RegisterClass(&wc);

    hr = SHGetDesktopFolder(&g_psfDesktop);
    if (FAILED(hr)) {
        return FALSE;
    }

    FOLDER_SENDTO = calloc(T_MAX_PATH, sizeof(TCHAR));
    if (FOLDER_SENDTO == NULL) {return FALSE;}

    return TRUE;
}

void TermApp(void)
{
    int i;
    //
    if (g_psfDesktop) {
        g_psfDesktop->lpVtbl->Release(g_psfDesktop);
        g_psfDesktop = NULL;
    }
    for (i = 0; i < idm_t; i++) {
        LocalFree(PSENTTO[i]);
    }
    free(PSENTTO);
    //
    free(FOLDER_SENDTO);
}

int WINAPI _tWinMain(HINSTANCE hinst, HINSTANCE hinstPrev, LPTSTR lpCmdLine, int nCmdShow) 
{
    MSG msg;
    HWND hwnd;
    HRESULT hrInit;

    g_hinst = hinst;

    if (!InitApp()) return 0;

    if (GetFullPathName(TEXT("sendto"), T_MAX_PATH, FOLDER_SENDTO, NULL) == 0) {return 1;}

    hrInit = CoInitialize(NULL);

    hwnd = CreateWindow(
        TEXT("SendTo+"),                /* Class Name */
        TEXT("lifenjoiner"),            /* Title */
        WS_POPUP,                       /* Style */
        CW_USEDEFAULT, CW_USEDEFAULT,   /* Position */
        0, 0,                           /* Size */
        NULL,                           /* Parent */
        NULL,                           /* No menu */
        hinst,                          /* Instance */
        0);                             /* No special parameters */

    ShowWindow(hwnd, nCmdShow);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    TermApp();

    if (SUCCEEDED(hrInit))
        CoUninitialize();

    return 0;
}
