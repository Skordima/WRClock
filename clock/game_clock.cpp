#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shlobj.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include "Resource.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Идентификаторы управляющих элементов
#define IDC_SET_TIME     1001
#define IDC_CLIP         1002
#define IDC_THEME_SWITCH 1003
#define IDC_ABOUT        1004

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")

// -----------------------------------------------------------------------------
// Константы времени игры
// -----------------------------------------------------------------------------
const double GAME_MINUTE_REAL_SECONDS = 8.75; // 1 игровая минута = 8.75 сек
const int    MINUTES_IN_DAY           = 1440;

// -----------------------------------------------------------------------------
// Структура темы и две предустановки (светлая/тёмная)
// -----------------------------------------------------------------------------
struct Theme {
    COLORREF bg;
    COLORREF text;
    COLORREF buttonBg;
    COLORREF buttonText;
    COLORREF accent;
};

const Theme LIGHT_THEME{ RGB(250,250,250), RGB(0,0,0),   RGB(230,230,230), RGB(0,0,0),   RGB(0,120,215) };
const Theme DARK_THEME { RGB(32,32,32),   RGB(255,255,255), RGB(60,60,60),  RGB(255,255,255), RGB(0,120,215) };

// -----------------------------------------------------------------------------
// Глобальные переменные
// -----------------------------------------------------------------------------
HINSTANCE g_hInst = nullptr;

HWND g_hTime = nullptr;
HWND g_hCountdown = nullptr;
HWND g_hRealCountdown = nullptr;
HWND g_hSetTime = nullptr;
HWND g_hCopy = nullptr;
HWND g_hTheme = nullptr;
HWND g_hAbout = nullptr;

bool    g_dark = false;
Theme   g_theme = LIGHT_THEME;
HBRUSH  g_brBackground = nullptr;
HFONT   g_fontLarge = nullptr;
HFONT   g_fontSmall = nullptr;

double      g_offset = 0.0;            // смещение в секундах
ULONGLONG   g_start  = 0;              // момент запуска (100 нс)

// -----------------------------------------------------------------------------
// Прототипы
// -----------------------------------------------------------------------------
ULONGLONG GetTime100ns();
void      UpdateClock();
void      ApplyTheme(HWND hwnd);
void      DrawButton(LPDRAWITEMSTRUCT dis);
std::wstring FormatTime(int minutes);
std::wstring GetIniPath();
void      SaveGameTime();
void      LoadGameTime();
INT_PTR CALLBACK SetTimeDlg(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK AboutDlg(HWND, UINT, WPARAM, LPARAM);

// -----------------------------------------------------------------------------
// Вспомогательные функции времени
// -----------------------------------------------------------------------------
ULONGLONG GetTime100ns()
{
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

std::wstring FormatTime(int minutes)
{
    int h = (minutes / 60) % 24;
    int m = minutes % 60;
    std::wostringstream ss;
    ss << std::setw(2) << std::setfill(L'0') << h
       << L":"
       << std::setw(2) << std::setfill(L'0') << m;
    return ss.str();
}

// -----------------------------------------------------------------------------
// Работа с INI файлом в AppData
// -----------------------------------------------------------------------------
std::wstring GetIniPath()
{
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPath(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE,
                                  nullptr, 0, path)))
    {
        std::wstring dir = std::wstring(path) + L"\\WRClock";
        CreateDirectory(dir.c_str(), nullptr);
        return dir + L"\\gameclock.ini";
    }
    return L"gameclock.ini";
}

void SaveGameTime()
{
    ULONGLONG now100 = GetTime100ns();
    double elapsed = (now100 - g_start) / 10000000.0; // сек
    double gameMin = (elapsed + g_offset) / GAME_MINUTE_REAL_SECONDS;
    int    gameInt = static_cast<int>(gameMin);

    wchar_t buf[64];
    std::wstring ini = GetIniPath();
    swprintf(buf, 64, L"%d", gameInt);
    WritePrivateProfileString(L"Game", L"GameTime", buf, ini.c_str());
    swprintf(buf, 64, L"%llu", now100);
    WritePrivateProfileString(L"Game", L"CloseTime", buf, ini.c_str());
}

void LoadGameTime()
{
    wchar_t buf[64] = {0};
    std::wstring ini = GetIniPath();
    int storedGame = 0;
    ULONGLONG storedClose = 0;
    if (GetPrivateProfileString(L"Game", L"GameTime", L"", buf, 64, ini.c_str()) > 0)
        storedGame = _wtoi(buf);
    if (GetPrivateProfileString(L"Game", L"CloseTime", L"", buf, 64, ini.c_str()) > 0)
        storedClose = _wtoi64(buf);

    ULONGLONG now = GetTime100ns();
    if (storedClose > 0 && now > storedClose)
    {
        double realElapsed = (now - storedClose) / 10000000.0;
        double additional = realElapsed / GAME_MINUTE_REAL_SECONDS;
        g_offset = (storedGame + additional) * GAME_MINUTE_REAL_SECONDS;
    }
    else
    {
        SYSTEMTIME st; GetLocalTime(&st);
        int sysMinutes = st.wHour * 60 + st.wMinute;
        g_offset = sysMinutes * GAME_MINUTE_REAL_SECONDS;
    }
}

// -----------------------------------------------------------------------------
// Темизация
// -----------------------------------------------------------------------------
void ApplyTheme(HWND hwnd)
{
    if (g_brBackground) DeleteObject(g_brBackground);
    g_brBackground = CreateSolidBrush(g_theme.bg);

    BOOL useDark = g_dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));

    InvalidateRect(hwnd, nullptr, TRUE);
}

// Отрисовка owner-draw кнопок
void DrawButton(LPDRAWITEMSTRUCT dis)
{
    std::wstring text;
    int len = GetWindowTextLength(dis->hwndItem);
    text.resize(len);
    GetWindowText(dis->hwndItem, text.data(), len + 1);

    const Theme& t = g_theme;
    bool pressed = (dis->itemState & ODS_SELECTED);

    COLORREF bg = pressed ? t.accent : t.buttonBg;
    COLORREF fg = pressed ? RGB(255,255,255) : t.buttonText;

    HBRUSH br = CreateSolidBrush(bg);
    FillRect(dis->hDC, &dis->rcItem, br);
    DeleteObject(br);

    HPEN pen = CreatePen(PS_SOLID, 1, t.accent);
    HPEN oldPen = (HPEN)SelectObject(dis->hDC, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
    RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
              dis->rcItem.right, dis->rcItem.bottom, 6, 6);
    SelectObject(dis->hDC, oldBrush);
    SelectObject(dis->hDC, oldPen);
    DeleteObject(pen);

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, fg);
    DrawText(dis->hDC, text.c_str(), -1, const_cast<RECT*>(&dis->rcItem),
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// -----------------------------------------------------------------------------
// Обновление отображаемого времени
// -----------------------------------------------------------------------------
void UpdateClock()
{
    ULONGLONG now100 = GetTime100ns();
    double elapsed = (now100 - g_start) / 10000000.0;
    double gameMinutes = (elapsed + g_offset) / GAME_MINUTE_REAL_SECONDS;

    int totalMinutes = static_cast<int>(gameMinutes) % MINUTES_IN_DAY;
    SetWindowText(g_hTime, FormatTime(totalMinutes).c_str());

    int toMidnight = MINUTES_IN_DAY - totalMinutes;
    std::wstring cd = L"До полуночи: " + FormatTime(toMidnight);
    SetWindowText(g_hCountdown, cd.c_str());

    double realSeconds = toMidnight * GAME_MINUTE_REAL_SECONDS;
    int rMin = static_cast<int>(realSeconds) / 60;
    int rSec = static_cast<int>(realSeconds) % 60;
    std::wostringstream ss;
    ss << L"Реальное время: " << std::setw(2) << std::setfill(L'0') << rMin
       << L":" << std::setw(2) << std::setfill(L'0') << rSec;
    SetWindowText(g_hRealCountdown, ss.str().c_str());
}

// -----------------------------------------------------------------------------
// Диалог установки времени
// -----------------------------------------------------------------------------
INT_PTR CALLBACK SetTimeDlg(HWND hDlg, UINT msg, WPARAM wParam, LPARAM)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        ApplyTheme(hDlg);
        return TRUE;
    case WM_CTLCOLORDLG:
        return (INT_PTR)g_brBackground;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, g_theme.bg);
        SetTextColor(hdc, g_theme.text);
        return (INT_PTR)g_brBackground;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            wchar_t buf[16];
            GetWindowText(GetDlgItem(hDlg, IDC_EDIT_TIME), buf, 16);
            int h,m;
            if (swscanf(buf, L"%d%*[^0-9]%d", &h, &m) == 2 &&
                h >=0 && h <24 && m>=0 && m<60)
            {
                g_start = GetTime100ns();
                g_offset = (h*60 + m) * GAME_MINUTE_REAL_SECONDS;
                EndDialog(hDlg, IDOK);
                return TRUE;
            }
            MessageBox(hDlg, L"Неверный формат времени", L"Ошибка", MB_OK | MB_ICONERROR);
            return TRUE;
        }
        else if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// -----------------------------------------------------------------------------
// Диалог "О программе"
// -----------------------------------------------------------------------------
INT_PTR CALLBACK AboutDlg(HWND hDlg, UINT msg, WPARAM wParam, LPARAM)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        ApplyTheme(hDlg);
        return TRUE;
    case WM_CTLCOLORDLG:
        return (INT_PTR)g_brBackground;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, g_theme.bg);
        SetTextColor(hdc, g_theme.text);
        return (INT_PTR)g_brBackground;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// -----------------------------------------------------------------------------
// Основная оконная процедура
// -----------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hInst = ((LPCREATESTRUCT)lParam)->hInstance;
        g_start = GetTime100ns();
        LoadGameTime();

        g_fontLarge = CreateFont(36,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,
                                 DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
        g_fontSmall = CreateFont(18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                                 DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");

        g_hTime = CreateWindow(L"STATIC", L"00:00", WS_CHILD|WS_VISIBLE|SS_CENTER,
                               10,10,230,40, hwnd, nullptr, g_hInst, nullptr);
        SendMessage(g_hTime, WM_SETFONT, (WPARAM)g_fontLarge, TRUE);

        g_hCountdown = CreateWindow(L"STATIC", L"", WS_CHILD|WS_VISIBLE|SS_CENTER,
                                    10,60,230,20, hwnd, nullptr, g_hInst, nullptr);
        SendMessage(g_hCountdown, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

        g_hRealCountdown = CreateWindow(L"STATIC", L"", WS_CHILD|WS_VISIBLE|SS_CENTER,
                                        10,85,230,20, hwnd, nullptr, g_hInst, nullptr);
        SendMessage(g_hRealCountdown, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

        g_hSetTime = CreateWindow(L"BUTTON", L"Установить время", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                                  10,120,230,26, hwnd, (HMENU)IDC_SET_TIME, g_hInst, nullptr);

        g_hCopy = CreateWindow(L"BUTTON", L"Копировать", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                               10,152,110,26, hwnd, (HMENU)IDC_CLIP, g_hInst, nullptr);

        g_hTheme = CreateWindow(L"BUTTON", L"Тёмная тема", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                                130,152,110,26, hwnd, (HMENU)IDC_THEME_SWITCH, g_hInst, nullptr);

        g_hAbout = CreateWindow(L"BUTTON", L"О программе", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                                10,184,230,26, hwnd, (HMENU)IDC_ABOUT, g_hInst, nullptr);

        ApplyTheme(hwnd);
        UpdateClock();
        SetTimer(hwnd, 1, 1000, nullptr);
        return 0;
    }
    case WM_TIMER:
        UpdateClock();
        return 0;
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, g_theme.bg);
        SetTextColor(hdc, g_theme.text);
        return (LRESULT)g_brBackground;
    }
    case WM_CTLCOLORDLG:
        return (LRESULT)g_brBackground;
    case WM_DRAWITEM:
        DrawButton((LPDRAWITEMSTRUCT)lParam);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_SET_TIME:
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_SET_TIME), hwnd, SetTimeDlg);
            UpdateClock();
            break;
        case IDC_CLIP:
        {
            int len = GetWindowTextLength(g_hTime);
            std::wstring txt(len, L'\0');
            GetWindowText(g_hTime, txt.data(), len+1);
            if (OpenClipboard(hwnd))
            {
                EmptyClipboard();
                size_t size = (txt.size()+1)*sizeof(wchar_t);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
                memcpy(GlobalLock(hMem), txt.c_str(), size);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
                CloseClipboard();
            }
            break;
        }
        case IDC_THEME_SWITCH:
            g_dark = !g_dark;
            g_theme = g_dark ? DARK_THEME : LIGHT_THEME;
            SetWindowText(g_hTheme, g_dark ? L"Светлая тема" : L"Тёмная тема");
            ApplyTheme(hwnd);
            break;
        case IDC_ABOUT:
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_ABOUT), hwnd, AboutDlg);
            break;
        }
        return 0;
    case WM_ERASEBKGND:
    {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, g_brBackground);
        return 1;
    }
    case WM_DESTROY:
        KillTimer(hwnd,1);
        SaveGameTime();
        if (g_brBackground) DeleteObject(g_brBackground);
        if (g_fontLarge) DeleteObject(g_fontLarge);
        if (g_fontSmall) DeleteObject(g_fontSmall);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

// -----------------------------------------------------------------------------
// Точка входа
// -----------------------------------------------------------------------------
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    const wchar_t CLASS_NAME[] = L"WRClockWindow";
    WNDCLASSEX wc{ sizeof(WNDCLASSEX) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CLOCK));
    wc.hIconSm       = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"Игровые часы",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 250, 240,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

