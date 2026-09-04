/*
 * Mahjong Launcher - raw Win32 port of the tkinter version.
 * No CRT (msvcrt) dependency - only kernel32.dll and user32.dll.
 *
 * Notes on the port:
 *  - COMBOBOX / BUTTON / STATIC / EDIT are "predefined window classes"
 *    implemented inside user32.dll itself, so no comctl32.dll / manifest /
 *    InitCommonControlsEx() is needed for this UI.
 *  - String building uses wsprintfW (user32.dll) and lstrcpyW/lstrcatW
 *    (kernel32.dll) instead of the CRT's sprintf/strcpy/strcat.
 *  - os.execvp() has no real Win32 equivalent (Windows can't replace the
 *    running process image). The closest match is CreateProcess() to spawn
 *    the child, followed by ExitProcess() so the launcher itself goes away.
 *  - memset/memcpy are implemented locally at the bottom of this file: even
 *    with no libc linked in, GCC can still emit calls to these two names for
 *    struct zeroing/copying, so we supply our own so the link succeeds.
 */

#define UNICODE
#define _UNICODE
#include <windows.h>

/* ---- control IDs -------------------------------------------------- */
#define IDC_RES_COMBO       1001
#define IDC_AA_CHECK        1002
#define IDC_HOSTMODE_CHECK  1004
#define IDC_CLIENTS_LABEL   1005
#define IDC_CLIENTS_COMBO   1006
#define IDC_HOSTADDR_LABEL  1007
#define IDC_HOSTADDR_EDIT   1008
#define IDC_LAUNCH_BTN      1009

static HINSTANCE g_hInst;

static HWND hResCombo, hAACheck, hHostModeCheck, hLaunchBtn;
static HWND hClientsLabel, hClientsCombo, hHostAddrLabel, hHostAddrEdit;

static const wchar_t *RESOLUTIONS[] = { L"1280x720", L"1600x900", L"1920x1080" };
#define NUM_RESOLUTIONS 3

/* ---- tiny freestanding helpers (see bottom of file) ---------------- */
void *memset(void *dst, int val, size_t n);
void *memcpy(void *dst, const void *src, size_t n);

/* ---------------------------------------------------------------------
 * update_host_client_ui(): mirrors LauncherApp.update_host_client_ui()
 * Rather than grid_forget()/re-grid, we just show/hide the two pairs
 * of controls that occupy the same "dynamic frame" slot.
 * --------------------------------------------------------------------- */
static void UpdateHostClientUI(void)
{
    BOOL hostMode = (SendMessageW(hHostModeCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

    ShowWindow(hClientsLabel, hostMode ? SW_SHOW : SW_HIDE);
    ShowWindow(hClientsCombo, hostMode ? SW_SHOW : SW_HIDE);
    ShowWindow(hHostAddrLabel, hostMode ? SW_HIDE : SW_SHOW);
    ShowWindow(hHostAddrEdit, hostMode ? SW_HIDE : SW_SHOW);
}

/* ---------------------------------------------------------------------
 * launch_game(): mirrors build_args() + os.execvp()
 * --------------------------------------------------------------------- */
static void LaunchGame(HWND hwnd)
{
    wchar_t cmd[512];
    wchar_t tmp[128];
    wchar_t addr[256];
    int mode, nclients;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    lstrcpyW(cmd, L"mahjong.exe");

    /* --screen-mode <index> */
    mode = (int)SendMessageW(hResCombo, CB_GETCURSEL, 0, 0);
    wsprintfW(tmp, L" --screen-mode %d", mode);
    lstrcatW(cmd, tmp);

    /* --no-aa */
    if (SendMessageW(hAACheck, BM_GETCHECK, 0, 0) != BST_CHECKED)
        lstrcatW(cmd, L" --no-aa");


    if (SendMessageW(hHostModeCheck, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        /* --host --num-clients <n> */
        nclients = (int)SendMessageW(hClientsCombo, CB_GETCURSEL, 0, 0);
        lstrcatW(cmd, L" --host");
        wsprintfW(tmp, L" --num-clients %d", nclients);
        lstrcatW(cmd, tmp);
    } else {
        /* --client <address> */
        addr[0] = L'\0';
        GetWindowTextW(hHostAddrEdit, addr, 256);
        lstrcatW(cmd, L" --client ");
        lstrcatW(cmd, addr);
    }

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    /* CreateProcess + ExitProcess is the Win32 stand-in for os.execvp():
       Windows has no way to replace the running process image in place. */
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        MessageBoxW(hwnd, L"Failed to launch mahjong.exe", L"Mahjong Launcher",
                    MB_OK | MB_ICONERROR);
        return;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    ExitProcess(0);
}

/* ---------------------------------------------------------------------
 * Window procedure
 * --------------------------------------------------------------------- */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD code = HIWORD(wp);
        if (id == IDC_HOSTMODE_CHECK && code == BN_CLICKED) {
            UpdateHostClientUI();
        } else if (id == IDC_LAUNCH_BTN && code == BN_CLICKED) {
            LaunchGame(hwnd);
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------------
 * Build the window and all child controls (this is the C equivalent of
 * LauncherApp.__init__).
 * --------------------------------------------------------------------- */
static HWND CreateMainWindow(void)
{
    WNDCLASSW wc;
    HWND hwnd;
    int i;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = g_hInst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"MahjongLauncherClass";
    RegisterClassW(&wc);

    /* resizable(False, False) -> no thick frame, no maximize box */
    hwnd = CreateWindowExW(
        0, L"MahjongLauncherClass", L"Mahjong Launcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 240,
        NULL, NULL, g_hInst, NULL);

    /* row 0: Resolution: [combo] */
    CreateWindowExW(0, L"STATIC", L"Resolution:", WS_CHILD | WS_VISIBLE,
                     10, 12, 80, 20, hwnd, NULL, g_hInst, NULL);
    hResCombo = CreateWindowExW(
        0, L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        95, 9, 135, 150, hwnd, (HMENU)IDC_RES_COMBO, g_hInst, NULL);
    for (i = 0; i < NUM_RESOLUTIONS; i++)
        SendMessageW(hResCombo, CB_ADDSTRING, 0, (LPARAM)RESOLUTIONS[i]);
    SendMessageW(hResCombo, CB_SETCURSEL, 0, 0);

    /* row 1: [x] Antialiasing  */
    hAACheck = CreateWindowExW(
        0, L"BUTTON", L"Antialiasing", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        10, 42, 110, 20, hwnd, (HMENU)IDC_AA_CHECK, g_hInst, NULL);
    SendMessageW(hAACheck, BM_SETCHECK, BST_CHECKED, 0);

    /* row 2: [x] Host Mode */
    hHostModeCheck = CreateWindowExW(
        0, L"BUTTON", L"Host Mode", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        10, 72, 150, 20, hwnd, (HMENU)IDC_HOSTMODE_CHECK, g_hInst, NULL);
    SendMessageW(hHostModeCheck, BM_SETCHECK, BST_CHECKED, 0);

    /* row 3: dynamic frame (host-mode pair) */
    hClientsLabel = CreateWindowExW(
        0, L"STATIC", L"Number of Clients:", WS_CHILD,
        10, 104, 120, 20, hwnd, (HMENU)IDC_CLIENTS_LABEL, g_hInst, NULL);
    hClientsCombo = CreateWindowExW(
        0, L"COMBOBOX", NULL,
        WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        135, 101, 60, 100, hwnd, (HMENU)IDC_CLIENTS_COMBO, g_hInst, NULL);
    {
        static const wchar_t *counts[] = { L"0", L"1", L"2", L"3" };
        for (i = 0; i < 4; i++)
            SendMessageW(hClientsCombo, CB_ADDSTRING, 0, (LPARAM)counts[i]);
        SendMessageW(hClientsCombo, CB_SETCURSEL, 0, 0);
    }

    /* row 3: dynamic frame (client-mode pair) */
    hHostAddrLabel = CreateWindowExW(
        0, L"STATIC", L"Host Address:", WS_CHILD,
        10, 104, 100, 20, hwnd, (HMENU)IDC_HOSTADDR_LABEL, g_hInst, NULL);
    hHostAddrEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"127.0.0.1",
        WS_CHILD | WS_TABSTOP,
        135, 102, 135, 20, hwnd, (HMENU)IDC_HOSTADDR_EDIT, g_hInst, NULL);
    SendMessageW(hHostAddrEdit, EM_LIMITTEXT, 200, 0); /* bound cmd buffer use */

    UpdateHostClientUI(); /* initial show/hide, same as __init__ does */

    /* row 4: [ Launch ] */
    hLaunchBtn = CreateWindowExW(
        0, L"BUTTON", L"Launch", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        95, 142, 100, 30, hwnd, (HMENU)IDC_LAUNCH_BTN, g_hInst, NULL);

    return hwnd;
}

int WinMainCRTStartup(void)
{
    HWND hwnd;
    MSG msg;

    g_hInst = GetModuleHandleW(NULL);
    hwnd = CreateMainWindow();
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ExitProcess((UINT)msg.wParam);
}

/* ---------------------------------------------------------------------
 * Minimal memset/memcpy so the link succeeds with no libc present.
 * GCC can silently lower struct-zeroing (`= {0}`) and struct copies to
 * calls to these two names even at -O0/-O2, regardless of -ffreestanding.
 * --------------------------------------------------------------------- */
void *memset(void *dst, int val, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)val;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}