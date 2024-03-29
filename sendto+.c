/* cl.exe /MD /Os /GA /DUNICODE /D_UNICODE sendto+.c Ole32.lib shell32.lib user32.lib Comdlg32.lib Comctl32.lib Shlwapi.lib
tcc sendto+.c -DUNICODE -D_UNICODE -DMINGW_HAS_SECURE_API -DINITGUID -lshell32 -lole32 -lshlwapi -lComdlg32 -lgdi32 -lcomctl32

https://msdn.microsoft.com/en-us/library/cc144093.aspx
https://msdn.microsoft.com/en-us/library/windows/desktop/bb776914.aspx
https://msdn.microsoft.com/en-us/library/windows/desktop/bb776885.aspx
GetCurrentDirectory()

64-bit
Wow64EnableWow64FsRedirection() only for system32;
32-bit environment is shadowed in 64-bit version.
*/

#define STRICT

#ifndef UNICODE
#define T_MAX_PATH MAX_PATH
#else
#define T_MAX_PATH 32767
#endif

#include <tchar.h>

#include <windows.h>

#include <windowsx.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <commctrl.h>

// tcc or any other compiler support wWinMain() and '__argc/__targv'
#if defined(__TINYC__)
DEFINE_GUID(IID_IDropTarget, 0x00000122, 0x0000, 0x0000, 0xc0,0x00, 0x00,0x00,0x00,0x00,0x00,0x46);
DEFINE_GUID(IID_IDataObject, 0x0000010e, 0x0000, 0x0000, 0xc0,0x00, 0x00,0x00,0x00,0x00,0x00,0x46);
#endif

#define IDM_SENDTOFIRST 0

TCHAR   *g_FOLDER_SENDTO;
TCHAR   **g_PSENDTO;                    /* Store the shourtcuts full path */
UINT    g_idm = IDM_SENDTOFIRST;

HBITMAP *g_hBmpImageA;                  /* MenuItemBitmap */

HINSTANCE       g_hinst;                /* My hinstance */
HMENU           g_hmenuSendTo;          /* My SendTo popup */
LPSHELLFOLDER   g_psfDesktop;           /* The desktop folder */

UINT g_FORKING = 0;   /* compatible with UAC focus changes */

LPSHELLFOLDER PIDLTolpShellFolder(LPITEMIDLIST pidl)
{
    LPSHELLFOLDER psf = NULL;

    if (pidl) {
        g_psfDesktop->lpVtbl->BindToObject(g_psfDesktop, pidl, NULL, &IID_IShellFolder, (LPVOID *)&psf);
    }
    return psf;
}

LPITEMIDLIST PIDLFromPath(HWND hwnd, LPCTSTR pszPath)
{
    LPITEMIDLIST pidl;
    ULONG ulEaten;
    DWORD dwAttributes;
    HRESULT hres;
    WCHAR *wszName;

#ifdef UNICODE
    wszName = (WCHAR *)pszPath;
#else
    wszName = calloc(T_MAX_PATH, sizeof(WCHAR));
    if (!MultiByteToWideChar(CP_ACP, 0, pszPath, -1, wszName, T_MAX_PATH)) {
        return NULL;
    }
#endif
    hres = g_psfDesktop->lpVtbl->ParseDisplayName(g_psfDesktop, hwnd, NULL, wszName, &ulEaten, &pidl, &dwAttributes);
#ifndef UNICODE
    free(wszName);
#endif
    if (FAILED(hres)) {
        return NULL;
    }
    return pidl;
}

LPSHELLFOLDER GetFolder(HWND hwnd, LPCTSTR pszPath)
{
    LPITEMIDLIST pidl;

    pidl = PIDLFromPath(hwnd, pszPath);
    return PIDLTolpShellFolder(pidl);
}

HRESULT GetUIObjectOfAbsPIDLs(HWND hwnd, LPITEMIDLIST *pidls, INT NumOfpidls, REFIID riid, LPVOID *ppvOut)
{
    LPITEMIDLIST *pidlLasts;
    LPSHELLFOLDER psf;
    HRESULT hres;
    INT i;

    *ppvOut = NULL;
    pidlLasts = malloc(sizeof(LPITEMIDLIST) * NumOfpidls);
    if (pidlLasts == NULL) {
        return E_FAIL;
    }

    for (i = 0; i < NumOfpidls; i++) {
        hres = SHBindToParent(pidls[i], &IID_IShellFolder, (LPVOID *)&psf, (LPCITEMIDLIST *)&pidlLasts[i]);
        if (FAILED(hres)) {
            goto Fail;
        }
        if (i < NumOfpidls - 1) {
            psf->lpVtbl->Release(psf);
        }
    }
    hres = psf->lpVtbl->GetUIObjectOf(psf, hwnd, NumOfpidls, pidlLasts, riid, NULL, ppvOut);
Fail:
    psf->lpVtbl->Release(psf);
    return hres;
}

HRESULT GetUIObjectOfPaths(HWND hwnd, LPCTSTR *pszPaths, INT NumOfPaths, REFIID riid, LPVOID *ppvOut)
{
    LPITEMIDLIST *pidls;
    HRESULT hres;
    INT i;

    *ppvOut = NULL;
    pidls = malloc(sizeof(LPITEMIDLIST) * NumOfPaths);
    if (pidls == NULL) {
        return E_FAIL;
    }

    for (i = 0; i < NumOfPaths; i++) {
        pidls[i] = PIDLFromPath(hwnd, pszPaths[i]);
        if (pidls[i] == NULL) {
            goto Fail;
        }
    }
    hres = GetUIObjectOfAbsPIDLs(hwnd, pidls, NumOfPaths, riid, ppvOut);
Fail:
    for (i = 0; i < NumOfPaths; i++) {
        CoTaskMemFree(pidls[i]);
    }
    free(pidls);
    return hres;
}

void DoDrop(LPDATAOBJECT pdto, LPDROPTARGET pdt)
{
    POINTL pt = {0, 0};
    DWORD dwEffect;
    HRESULT hres;

    dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
    hres = pdt->lpVtbl->DragEnter(pdt, pdto, MK_LBUTTON, pt, &dwEffect);
    if (SUCCEEDED(hres) && dwEffect) {
        hres = pdt->lpVtbl->Drop(pdt, pdto, MK_LBUTTON, pt, &dwEffect);
    }
    else {
        hres = pdt->lpVtbl->DragLeave(pdt);
    }
}

LPTSTR PIDLToName(LPSHELLFOLDER psf, LPITEMIDLIST pidl, SHGDNF uFlags) {
    HRESULT hres;
    STRRET str;
    LPTSTR pszName = NULL;

    hres = psf->lpVtbl->GetDisplayNameOf(psf, pidl, uFlags, &str);
    if (hres == S_OK) {
        hres = StrRetToStr(&str, pidl, &pszName);
    }
    return pszName;
}

// GdipCreateHBITMAPFromBitmap drops background's alpha.
HBITMAP ImageList_CreateHBITMAPFromIcon(HIMAGELIST himl, int i, UINT flags)
{
    HBITMAP hBMP = NULL;

    HDC hCDC = CreateCompatibleDC(NULL);
    if (hCDC != NULL) {
        int width = 16, height = -16;
        BITMAPINFO bmi = { {sizeof(BITMAPINFOHEADER), width, height, 1, 32, BI_RGB, 0, 0, 0, 0, 0}, {{0, 0, 0, 0}} };
        if (ImageList_GetIconSize(himl, &width, &height)) {
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height;
            hBMP = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, NULL, NULL, 0);
            SelectObject(hCDC, hBMP);
            ImageList_Draw(himl, i, hCDC, 0, 0, ILD_TRANSPARENT);
        }
        DeleteDC(hCDC); // hBMP is not occupied any more.
    }
    return hBMP;
}

HBITMAP GetHBitMapByPath(LPTSTR pszPath)
{
    SHFILEINFO ShFI = {0};
    HBITMAP hBMP = NULL;

    UINT uFlags = SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON;
    HIMAGELIST imageList = (HIMAGELIST)SHGetFileInfo(pszPath, FILE_ATTRIBUTE_NORMAL, &ShFI, sizeof(SHFILEINFO), uFlags);
    if (imageList != 0) {
        hBMP = ImageList_CreateHBITMAPFromIcon(imageList, ShFI.iIcon, ILD_TRANSPARENT);
    }
    return hBMP;
}

void FolderToMenu(HWND hwnd, HMENU hmenu, LPCTSTR pszFolder)
{
    LPSHELLFOLDER psf;
    HRESULT hres;
    STRRET str;

    /* OS_WOW6432 */
    if ((PROC)(GetProcAddress(GetModuleHandle(_T("Shlwapi")), (LPCSTR)437))(30)) {
        AppendMenu(hmenu, MF_GRAYED | MF_DISABLED | MF_STRING, g_idm, TEXT("64-bit OS needs 64-bit version :p"));
        return;
    }

    psf = GetFolder(hwnd, pszFolder);
    if (psf) {
        LPENUMIDLIST peidl;
        hres = psf->lpVtbl->EnumObjects(psf, hwnd, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &peidl);
        if (SUCCEEDED(hres)) {
            LPITEMIDLIST pidl;
            MENUITEMINFO mii;
            MENUINFO mi;
            while (peidl->lpVtbl->Next(peidl, 1, &pidl, NULL) == S_OK) {
                LPTSTR pszPath, pszName;
                UINT uFlags = MF_ENABLED | MF_STRING;
                //
                pszPath = PIDLToName(psf, pidl, SHGDN_FORPARSING);
                pszName = PIDLToName(psf, pidl, SHGDN_NORMAL);
                //
                // Path should be enough.
                CoTaskMemFree(pidl);
                if (pszPath == NULL || pszName == NULL) {
                    continue;
                }
                //
                // Store the path to retrieve, as we check if it is a dir and launch it later.
                g_PSENDTO = (TCHAR **)realloc(g_PSENDTO, sizeof(TCHAR *) * (g_idm - IDM_SENDTOFIRST + 1));
                if (g_PSENDTO == NULL) {
                    continue;
                }
                g_PSENDTO[g_idm] = _tcsdup(pszPath);
                //
                // Icon
                g_hBmpImageA = (HBITMAP *)realloc(g_hBmpImageA, sizeof(HBITMAP) * (g_idm - IDM_SENDTOFIRST + 1));
                g_hBmpImageA[g_idm] = GetHBitMapByPath(pszPath);
                //
                if (PathIsDirectory(pszPath)) {
                    HMENU hSubMenu = CreatePopupMenu();
                    if (AppendMenu(hmenu, uFlags | MF_POPUP, (UINT_PTR)hSubMenu, pszName)) {
                        mi.cbSize = sizeof(mi);
                        mi.fMask = MIM_HELPID;
                        mi.dwContextHelpID = g_idm;
                        SetMenuInfo(hSubMenu, &mi);
                        g_idm++;
                    }
                }
                else {
                    if (AppendMenu(hmenu, uFlags, g_idm, pszName)) {
                        mii.cbSize = sizeof(mii);
                        mii.fMask = MIIM_DATA;
                        if (g_hBmpImageA[g_idm] != NULL) {
                            mii.fMask |= MIIM_BITMAP;
                            mii.hbmpItem = g_hBmpImageA[g_idm];
                        }
                        SetMenuItemInfo(hmenu, g_idm, FALSE, &mii);
                        g_idm++;
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

    if (g_idm == IDM_SENDTOFIRST) {
        AppendMenu(hmenu, MF_GRAYED | MF_DISABLED | MF_STRING, g_idm, TEXT("Send what sent to me to my sendto ^_^"));
    }
}

void SendTo_OnInitMenuPopup(HWND hwnd, HMENU hmenu, UINT item, BOOL fSystemMenu)
{
    if (GetMenuItemCount(hmenu) > 0) {
        return;
    }
    if (hmenu == g_hmenuSendTo) { /* :p only top level */
        FolderToMenu(hwnd, hmenu, g_FOLDER_SENDTO);
    }
    else {
        MENUINFO mi;
        mi.cbSize = sizeof(mi);
        mi.fMask = MIM_HELPID;
        if (GetMenuInfo(hmenu, &mi)) {
            FolderToMenu(hwnd, hmenu, g_PSENDTO[mi.dwContextHelpID]);
        }
    }
}

void SendTo_SendToItem(HWND hwnd, int idm)
{
    HRESULT hres;

    g_FORKING = 1;
    if (__argc == 1) {
        ShellExecute(NULL, NULL, g_PSENDTO[idm], NULL, NULL, SW_SHOWDEFAULT);
    }
    else {
        LPDATAOBJECT pdto;
        LPDROPTARGET pdt;
        hres = GetUIObjectOfPaths(hwnd, &g_PSENDTO[idm], 1, &IID_IDropTarget, (LPVOID *)&pdt);
        if (SUCCEEDED(hres)) {
            /* First convert all filenames to a data object. */
            hres = GetUIObjectOfPaths(hwnd, __targv + 1, __argc - 1, &IID_IDataObject, (LPVOID *)&pdto);
            if (SUCCEEDED(hres)) {
                    /* Now drop the file on the drop target. */
                    DoDrop(pdto, pdt);
                    pdt->lpVtbl->Release(pdt);
            }
        }
        pdto->lpVtbl->Release(pdto);
    }
    // Exit as done!
    g_FORKING = 0;
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

/* UAC focus changes!!! */
void SendTo_OnKillFocus(HWND hwnd, HWND hwndOldFocus)
{
    if (g_FORKING == 0) {
        PostQuitMessage(0);
    }
}

LRESULT CALLBACK SendTo_WndProc(HWND hwnd, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uiMsg) {
    HANDLE_MSG(hwnd, WM_CREATE, SendTo_OnCreate);
    HANDLE_MSG(hwnd, WM_INITMENUPOPUP, SendTo_OnInitMenuPopup);
    HANDLE_MSG(hwnd, WM_COMMAND, SendTo_OnCommand);
    HANDLE_MSG(hwnd, WM_KILLFOCUS, SendTo_OnKillFocus);
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

    g_FOLDER_SENDTO = calloc(T_MAX_PATH + 1, sizeof(TCHAR));
    if (g_FOLDER_SENDTO == NULL) {
        return FALSE;
    }

    return TRUE;
}

void TermApp(void)
{
    int i, n;

    if (g_psfDesktop) {
        g_psfDesktop->lpVtbl->Release(g_psfDesktop);
        g_psfDesktop = NULL;
    }
    n = g_idm - IDM_SENDTOFIRST;
    for (i = 0; i < n; i++) {
        free(g_PSENDTO[i]);
        if (g_hBmpImageA[i] != NULL) {
            DeleteObject(g_hBmpImageA[i]);
        }
    }
    free(g_PSENDTO);
    free(g_FOLDER_SENDTO);
    free(g_hBmpImageA);
}

int WINAPI _tWinMain(HINSTANCE hinst, HINSTANCE hinstPrev, LPTSTR lpCmdLine, int nCmdShow) 
{
    MSG msg;
    HWND hwnd;
    HRESULT hrInit;
    POINT pt = {0, 0};

    g_hinst = hinst;

    if (!InitApp()) {
        return 1;
    }
    if (GetFullPathName(TEXT("sendto"), T_MAX_PATH, g_FOLDER_SENDTO, NULL) == 0) {
        return 2;
    }
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

    SetWindowLong(hwnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW);
    ShowWindow(hwnd, nCmdShow);

    GetCursorPos(&pt);
    TrackPopupMenu(g_hmenuSendTo, TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    TermApp();

    if (SUCCEEDED(hrInit)) {
        CoUninitialize();
    }

    return 0;
}
