/* cl.exe /MD /Os /DUNICODE /D_UNICODE sendto+.c Ole32.lib shell32.lib user32.lib Comdlg32.lib Shlwapi.lib
tcc -DUNICODE -D_UNICODE -DMINGW_HAS_SECURE_API -DINITGUID -lshell32 -lole32 -lshlwapi -lComdlg32 sendto+.c
https://msdn.microsoft.com/en-us/library/cc144093.aspx
https://msdn.microsoft.com/en-us/library/windows/desktop/bb776914.aspx
https://msdn.microsoft.com/en-us/library/windows/desktop/bb776885.aspx
GetCurrentDirectory()
64 bits
Wow64EnableWow64FsRedirection() only for system32;
Use a trick for lnk file!
*/

#ifndef UNICODE
#define T_MAX_PATH MAX_PATH
#else
#define T_MAX_PATH 32767
#endif

#define STRICT

#include <tchar.h>
#include <stdio.h>
#include <sys\stat.h>

#include <windows.h>

#include <windowsx.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commdlg.h>

// mingw
DEFINE_GUID(IID_IDropTarget, 0x00000122, 0x0000, 0x0000, 0xc0,0x00, 0x00,0x00,0x00,0x00,0x00,0x46);
DEFINE_GUID(IID_IDataObject, 0x0000010e, 0x0000, 0x0000, 0xc0,0x00, 0x00,0x00,0x00,0x00,0x00,0x46);

#define IDM_SENDTOFIRST 0

TCHAR   *FOLDER_SENDTO;
UINT    idm_g = IDM_SENDTOFIRST;
TCHAR   **PSENTTO;                      /* store the shourtcuts full path */

HINSTANCE       g_hinst;                /* My hinstance */
HMENU           g_hmenuSendTo;          /* Our SendTo popup */
LPSHELLFOLDER   g_psfDesktop;           /* The desktop folder */

UINT FORKING = 0;   /* compatible with UAC focus changes */

/* For system32/SysWOW64, current thread */
PROC m_Wow64EnableWow64FsRedirection;

LPSHELLFOLDER PIDL2PSF(LPITEMIDLIST pidl)
{
    LPSHELLFOLDER psf = NULL;

    if (pidl) {
		g_psfDesktop->lpVtbl->BindToObject(g_psfDesktop, pidl, NULL, &IID_IShellFolder, (LPVOID *)&psf);
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
	wszName = calloc(T_MAX_PATH + 1, sizeof(WCHAR));

#ifdef UNICODE
	if (wcslen(pszPath) >= T_MAX_PATH) { return NULL; }
	wcscpy(wszName, pszPath);
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
    hres = SHBindToParent(pidl, &IID_IShellFolder, (LPVOID *)&psf, (LPCITEMIDLIST*)&pidlLast);
    if (FAILED(hres)) {
        return hres;
    }

    /* Now ask the parent for the the UI object of the child. */
    hres = psf->lpVtbl->GetUIObjectOf(psf, hwnd, 1, (LPCITEMIDLIST*)&pidlLast, riid, NULL, ppvOut);

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

    dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
    hres = pdt->lpVtbl->DragEnter(pdt, pdto, MK_LBUTTON, pt, &dwEffect);
    if (SUCCEEDED(hres) && dwEffect) {
        hres = pdt->lpVtbl->Drop(pdt, pdto, MK_LBUTTON, pt, &dwEffect);

    } else {
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

static size_t read_file(FILE* fp, unsigned char** output) {
    size_t smart_size, count;
    size_t length = 0;
    //make it faster
    if (fp == stdin) {
        smart_size = stdin->_bufsiz;
    }
    else { //unstable for stdin!
        struct stat filestats;
        int fd = fileno(fp);
        fstat(fd, &filestats);
        smart_size = filestats.st_size + 1; // +1 to get EOF, BIG file
    }
    //
    *output = calloc(1, 1); //just in case
    while (!feof(fp)) {
        *output = realloc(*output, length + smart_size + 1);
        count = fread(*output + length, 1, smart_size, fp);
        memset(*output + length + count, 0, 1); // append 0
        length += count;
    }
    *output = realloc(*output, length + 1);
    //
    return length;
}

/* ;p run 64 bits app in "program files" */
int GetShortcutTargetPath(TCHAR *szShortcutFile, TCHAR *szTargetPath)
{
	int rc = 0;
	FILE *fd;
	unsigned char *buf;
	size_t len;

	// open shortcut file
	fd = _tfopen(szShortcutFile, _T("rb"));
	len = read_file(fd, &buf);
	fclose(fd);

    if (len < 0x4C) {
        goto cleanup;
    }

	// check for LNK file header "4C 00 00 00"
	if(*(DWORD*)(buf + 0x00) != 0x0000004C) {
		// LNK file header invalid
		goto cleanup;
	}

	// check for shell link GUID "{00021401-0000-0000-00C0-000000000046}"
	if(*(DWORD*)(buf + 0x04) != 0x00021401
	|| *(DWORD*)(buf + 0x08) != 0x00000000
	|| *(DWORD*)(buf + 0x0C) != 0x000000C0
	|| *(DWORD*)(buf + 0x10) != 0x46000000) {
		// shell link GUID invalid
		goto cleanup;
	}

	// check for presence of shell item ID list
	if((*(BYTE*)(buf + 0x14) & 0x01) == 0) {
		// shell item ID list is not present
		goto cleanup;
	}

	// pidl of target
	rc = SHGetPathFromIDList((LPCITEMIDLIST)(buf + 0x4E), szTargetPath);

cleanup:
    free(buf);

	return rc;
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
                LPTSTR pszPathTarget;   // redirect right
                UINT len;
                //
                pszPath = pidl_to_name(psf, pidl, SHGDN_FORPARSING);
                if (pszPath == NULL) {continue;}
                /* ;p run 64 bits app in "program files" */
                if (m_Wow64EnableWow64FsRedirection != NULL && _tcsicmp(PathFindExtension(pszPath), _T(".lnk")) == 0) {
                    pszPathTarget = CoTaskMemAlloc(T_MAX_PATH + 1);
                    if (GetShortcutTargetPath(pszPath, pszPathTarget) == 0) {
                        CoTaskMemFree(pszPathTarget);
                        pszPathTarget = pszPath;
                    }
                    //MessageBox(NULL, pszPath, NULL, MB_OK);
                }
                else {
                    pszPathTarget = pszPath;
                }
                pszName = pidl_to_name(psf, pidl, SHGDN_NORMAL);
                if (pszName == NULL) {continue;}
                //
                // store rather than retrial
                PSENTTO = (TCHAR**)realloc(PSENTTO, sizeof(TCHAR*) * (idm_g - IDM_SENDTOFIRST + 1));
                if (PSENTTO == NULL) {continue;}
                PSENTTO[idm_g] = StrDup(pszPathTarget);
                //
                if (PathIsDirectory(pszPath)) {
                    HMENU hSubMenu = CreatePopupMenu();
                    if (AppendMenu(hmenu, MF_ENABLED | MF_STRING | MF_POPUP, (UINT)hSubMenu, pszName)) {
                        idm_g++;
                        FolderToMenu(hwnd, hSubMenu, pszPath);
                    }
                }
                else {
                    if (AppendMenu(hmenu, MF_ENABLED | MF_STRING, idm_g, pszName)) {
                        mii.cbSize = sizeof(mii);
                        mii.fMask = MIIM_DATA;
                        mii.dwItemData = (ULONG_PTR)pidl;
                        SetMenuItemInfo(hmenu, idm_g, FALSE, &mii);
                        idm_g++;
                    }
                }
                //
                CoTaskMemFree(pszPath);
                CoTaskMemFree(pszPathTarget);
                CoTaskMemFree(pszName);
            }
            peidl->lpVtbl->Release(peidl);
        }
        psf->lpVtbl->Release(psf);
    }

    if (idm_g == IDM_SENDTOFIRST) {
        AppendMenu(hmenu, MF_GRAYED | MF_DISABLED | MF_STRING,
                   idm_g, TEXT("Send what sent to me to my sendto ^_^"));
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

    //
#if !defined(_MSC_VER)
#ifdef _UNICODE
#define __tgetmainargs __wgetmainargs
#else
#define __tgetmainargs __getmainargs
#endif
    typedef struct { int newmode; } _startupinfo;
    int __cdecl __tgetmainargs(int *pargc, _TCHAR ***pargv, _TCHAR ***penv, int globb, _startupinfo*);
    _TCHAR **env;
    _startupinfo start_info = {0};
    __tgetmainargs(&__argc, &__targv, &env, 1, &start_info); //tchar.h
#endif

    FORKING = 1;
    //
    if (__argc < 2) {
        ShellExecute(NULL, _T("open"), PSENTTO[idm], NULL, NULL, SW_SHOWDEFAULT);
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
    // Exit as done!
    FORKING = 0;
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
    if (FORKING == 0) PostQuitMessage(0);
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

    m_Wow64EnableWow64FsRedirection = GetProcAddress(GetModuleHandle(_T("Kernel32")), "Wow64EnableWow64FsRedirection");
    if (m_Wow64EnableWow64FsRedirection != NULL) {m_Wow64EnableWow64FsRedirection(FALSE);}

    hr = SHGetDesktopFolder(&g_psfDesktop);
    if (FAILED(hr)) {
        return FALSE;
    }

    FOLDER_SENDTO = calloc(T_MAX_PATH + 1, sizeof(TCHAR));
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
    for (i = 0; i < idm_g; i++) {
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
    POINT pt = {0, 0};

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

    SetWindowLong(hwnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW);
    ShowWindow(hwnd, nCmdShow);

    // run once!
    GetCursorPos(&pt);
    TrackPopupMenu(g_hmenuSendTo, TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    //
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    TermApp();

    if (SUCCEEDED(hrInit))
        CoUninitialize();

    return 0;
}
