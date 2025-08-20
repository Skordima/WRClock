#define NOMINMAX
#include <windows.h>
#include <tchar.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <mmsystem.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include "Resource.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")

// Предварительные объявления функций
void UpdateFonts(int newHeight);
void ResizeChildControls(HWND hwnd, int newWidth, int newHeight);
void ApplyTheme(HWND hwnd);
INT_PTR CALLBACK SetTimeDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK AboutDlgProc(HWND, UINT, WPARAM, LPARAM); // новая процедура диалога «О программе»
ULONGLONG GetSystemTimeULongLong();

// Идентификаторы таймера и кнопок
#define IDT_TIMER       1
#define IDC_SET_TIME    101
#define IDC_CLIP        102
#define IDC_THEME_SWITCH 110

// Константы преобразования времени:
// 1 игровая минута = 8.75 секунд реального времени
// 1 игровый час = 525 секунд реального времени (8.75*60)
// 1 игровой день = 12600 секунд реального времени (525*24)
const double GAME_MINUTE_REAL_SECONDS = 8.75;
const int MINUTES_IN_DAY = 1440;

// Размеры клиентской области
const int BASE_WIDTH = 250;
const int BASE_HEIGHT = 210;

// Структура размещения элементов управления
struct ControlLayout {
    int x, y, width, height;
};

const ControlLayout LAYOUT_TIME = { 10, 40, 230, 40 };
const ControlLayout LAYOUT_COUNTDOWN = { 10, 90, 235, 20 };
const ControlLayout LAYOUT_REALCOUNTDOWN = { 10, 115, 230, 20 };
const ControlLayout LAYOUT_SETTIME = { 10, 145, 230, 25 };
const ControlLayout LAYOUT_CLIP = { BASE_WIDTH - 35, 5, 30, 30 };
const ControlLayout LAYOUT_THEME = { 10, 5, 120, 26 };
const ControlLayout LAYOUT_ABOUT = { 10, 180, 230, 25 };

// Глобальные переменные элементов управления
HWND hTimeLabel, hCountdownLabel, hRealCountdownLabel, hSetTimeButton, hClipButton, hAboutButton, hThemeSwitch;
HWND hTimeLabel, hCountdownLabel, hRealCountdownLabel, hSetTimeButton, hClipButton, hAboutButton;
double g_offset = 0.0;       // смещение в секундах для расчёта игрового времени
ULONGLONG g_realStart = 0;   // момент запуска (в единицах 100 нс)
int g_lastBeepMarker = -1;
bool g_alwaysOnTop = false;
bool g_darkTheme = false;

// Глобальные кисти и шрифты
HBRUSH hBrushBackground = NULL;
HFONT g_hFontLarge = NULL;
HFONT g_hFontMedium = NULL;
HFONT g_hFontSmall = NULL;

// Глобальная переменная для извлечённого ресурса (WAV)
std::wstring g_notificationTempPath = L"";

//
// Функция для получения системного времени как ULONGLONG (FILETIME, единицы 100 нс)
//
ULONGLONG GetSystemTimeULongLong()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (((ULONGLONG)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

//
// Запись сообщений в файл log.txt
//
#ifndef NDEBUG
void LogMessage(const std::wstring& message)
{
    // В режиме Debug записываем журнал
    std::wofstream logFile("log.txt", std::ios::app);
    if (logFile)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        logFile << st.wYear << L"-" << st.wMonth << L"-" << st.wDay
            << L" " << st.wHour << L":" << st.wMinute << L":" << st.wSecond
            << L": " << message << std::endl;
    }
}
#else
// В режиме Release функция ничего не делает
void LogMessage(const std::wstring& message) {}
#endif

//
// Форматирование времени (HH:MM)
//
std::wstring FormatTime(int totalMinutes)
{
    int hours = totalMinutes / 60;
    int minutes = totalMinutes % 60;
    std::wstringstream ss;
    ss << std::setw(2) << std::setfill(L'0') << hours << L":"
        << std::setw(2) << std::setfill(L'0') << minutes;
    return ss.str();
}

//
// Извлечение ресурса во временный файл
//
std::wstring ExtractResourceToTempFile(HINSTANCE hInst, LPCTSTR lpName, LPCTSTR lpType)
{
    HRSRC hRes = FindResource(hInst, lpName, lpType);
    if (!hRes)
    {
        LogMessage(L"FindResource завершилась неудачно");
        return L"";
    }
    DWORD size = SizeofResource(hInst, hRes);
    HGLOBAL hResData = LoadResource(hInst, hRes);
    if (!hResData)
    {
        LogMessage(L"LoadResource завершилась неудачно");
        return L"";
    }
    LPVOID pResData = LockResource(hResData);
    if (!pResData)
    {
        LogMessage(L"LockResource завершилась неудачно");
        return L"";
    }

    TCHAR tempPath[MAX_PATH];
    if (!GetTempPath(MAX_PATH, tempPath))
    {
        LogMessage(L"GetTempPath завершилась неудачно");
        return L"";
    }

    TCHAR tempFileName[MAX_PATH];
    if (!GetTempFileName(tempPath, _T("WAV"), 0, tempFileName))
    {
        LogMessage(L"GetTempFileName завершилась неудачно");
        return L"";
    }

    HANDLE hFile = CreateFile(tempFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        LogMessage(L"CreateFile завершилась неудачно");
        return L"";
    }

    DWORD written = 0;
    WriteFile(hFile, pResData, size, &written, NULL);
    CloseHandle(hFile);

    LogMessage(std::wstring(L"Ресурс извлечён в ") + tempFileName);
    return std::wstring(tempFileName);
}


//
// Получение пути к INI-файлу в папке AppData пользователя
//

std::wstring GetIniFilePath()
{
    TCHAR appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, appDataPath)))
    {
        std::wstring folder = std::wstring(appDataPath) + L"\\WRClock";
        CreateDirectory(folder.c_str(), NULL);
        return folder + L"\\gameclock.ini";
    }
    return L"gameclock.ini";
}


//
// Сохранение игрового времени в INI-файл (gameclock.ini)
//

void SaveGameTime()
{
    ULONGLONG closeTime = GetSystemTimeULongLong();
    double elapsed = (closeTime - g_realStart) / 10000000.0; // реальные секунды
    double gameTime = (elapsed + g_offset) / GAME_MINUTE_REAL_SECONDS; // игровые минуты
    int gameTimeInt = (int)gameTime;

    TCHAR buffer[64];
    _stprintf_s(buffer, _T("%d"), gameTimeInt);
    std::wstring iniPath = GetIniFilePath();
    WritePrivateProfileString(_T("Game"), _T("GameTime"), buffer, iniPath.c_str());

    _stprintf_s(buffer, _T("%I64u"), closeTime);
    WritePrivateProfileString(_T("Game"), _T("CloseTime"), buffer, iniPath.c_str());

    LogMessage(L"Игровое время сохранено.");
}

//
// Загрузка игрового времени из INI-файла
//
void LoadGameTime()
{
    TCHAR buffer[64] = { 0 };
    int storedGameTime = 0;
    ULONGLONG storedCloseTime = 0;
    std::wstring iniPath = GetIniFilePath();
    if (GetPrivateProfileString(_T("Game"), _T("GameTime"), _T(""), buffer, 64, iniPath.c_str()) > 0)
    {
        storedGameTime = _ttoi(buffer);
    }
    if (GetPrivateProfileString(_T("Game"), _T("CloseTime"), _T(""), buffer, 64, iniPath.c_str()) > 0)
    {
        storedCloseTime = _ttoi64(buffer);
    }

    ULONGLONG currentTime = GetSystemTimeULongLong();
    if (storedCloseTime > 0 && currentTime > storedCloseTime)
    {
        double realElapsed = (currentTime - storedCloseTime) / 10000000.0; // реальные секунды
        double additionalGameMinutes = realElapsed / GAME_MINUTE_REAL_SECONDS;
        double newGameTime = storedGameTime + additionalGameMinutes;
        g_offset = newGameTime * GAME_MINUTE_REAL_SECONDS;
        LogMessage(L"Игровое время загружено из INI.");
    }
    else
    {
        // Если данных нет, устанавливаем игровое время равным системному (в минутах)
        SYSTEMTIME st;
        GetLocalTime(&st);
        int sysMinutes = st.wHour * 60 + st.wMinute;
        g_offset = sysMinutes * GAME_MINUTE_REAL_SECONDS;
        LogMessage(L"Сохранённое игровое время не найдено; используется системное время.");
    }
}

//
// Применение выбранной темы (тёмной или светлой)
//
void ApplyTheme(HWND hwnd)
{
    if (hBrushBackground)
        DeleteObject(hBrushBackground);

    COLORREF bg = g_darkTheme ? RGB(32, 32, 32) : RGB(245, 245, 245);
    hBrushBackground = CreateSolidBrush(bg);
    InvalidateRect(hwnd, NULL, TRUE);
}

//
// Диалоговая процедура для ручной установки игрового времени
//
INT_PTR CALLBACK SetTimeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HWND hEdit;
    switch (message)
    {
    case WM_INITDIALOG:
        SetWindowText(hDlg, _T("Настройка игрового времени"));
        hEdit = GetDlgItem(hDlg, IDC_EDIT_TIME);
        {
            HWND hOk = GetDlgItem(hDlg, IDOK);
            HWND hCancel = GetDlgItem(hDlg, IDCANCEL);
            SetWindowTheme(hOk, L"", L"");
            SetWindowTheme(hCancel, L"", L"");
        }
        return (INT_PTR)TRUE;
    case WM_CTLCOLORDLG:
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(245, 245, 245));
        static HBRUSH hbrDialog = CreateSolidBrush(RGB(245, 245, 245));
        return (INT_PTR)hbrDialog;
    }
    case WM_CTLCOLOREDIT:
    {
        HDC hdcEdit = (HDC)wParam;
        SetBkColor(hdcEdit, RGB(245, 245, 245));
        SetTextColor(hdcEdit, RGB(0, 0, 0));
        static HBRUSH hbrEdit = CreateSolidBrush(RGB(245, 245, 245));
        return (INT_PTR)hbrEdit;
    }
    case WM_CTLCOLORBTN:
    {
        HDC hdcBtn = (HDC)wParam;
        SetBkColor(hdcBtn, RGB(245, 245, 245));
        SetTextColor(hdcBtn, RGB(0, 0, 0));
        static HBRUSH hbrBtn = CreateSolidBrush(RGB(245, 245, 245));
        return (INT_PTR)hbrBtn;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            wchar_t buffer[16];
            GetWindowText(hEdit, buffer, 16);
            int hours = 0, minutes = 0;
            if (swscanf_s(buffer, L"%d%*[^0-9]%d", &hours, &minutes) == 2)
            {
                if (hours >= 0 && hours < 24 && minutes >= 0 && minutes < 60)
                {
                    int desiredTotalMinutes = hours * 60 + minutes;
                    // Сбрасываем базовую отметку времени, чтобы при вводе, например, 20:00, сразу отображалось 20:00
                    g_realStart = GetSystemTimeULongLong();
                    g_offset = desiredTotalMinutes * GAME_MINUTE_REAL_SECONDS;
                    g_lastBeepMarker = -1;
                    SaveGameTime();
                    EndDialog(hDlg, IDOK);
                    return (INT_PTR)TRUE;
                }
            }
            MessageBox(hDlg, _T("Неверный формат времени. Введите ЧЧ:ММ или ЧЧ ММ"), _T("Ошибка"), MB_OK | MB_ICONERROR);
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

//
// Новая диалоговая процедура «О программе»
//
INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HFONT hFontGameClock = NULL;
    switch (message)
    {
    case WM_INITDIALOG:
        // Фиксируем размер окна 420x300 пикселей
        SetWindowPos(hDlg, NULL, 0, 0, 420, 300, SWP_NOMOVE | SWP_NOZORDER);

        // Создаём шрифт размера 35 для текста «Игровые часы»
        hFontGameClock = CreateFont(35, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, _T("MS Shell Dlg"));
        SendMessage(GetDlgItem(hDlg, IDC_GAMECLOCK), WM_SETFONT, (WPARAM)hFontGameClock, TRUE);
        return (INT_PTR)TRUE;

    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
        if (pdis->CtlID == IDC_APPICON)
        {
            // Загружаем значок ресурса IDI_CLOCK (размер ресурса 110x110)
            HICON hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_CLOCK),
                IMAGE_ICON, 110, 110, LR_DEFAULTCOLOR);
            if (hIcon)
            {
                // Масштабируем значок до 95x95
                DrawIconEx(pdis->hDC, pdis->rcItem.left, pdis->rcItem.top,
                    hIcon, 95, 95, 0, NULL, DI_NORMAL);
                DestroyIcon(hIcon);
            }
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

//
// Функция обновления шрифтов с динамическим масштабированием
//
void UpdateFonts(int newHeight)
{
    const int baseLarge = 36, baseMedium = 24, baseSmall = 18;
    double scaleY = (double)newHeight / BASE_HEIGHT;
    int newLarge = std::max(1, (int)(baseLarge * scaleY));
    int newMedium = std::max(1, (int)(baseMedium * scaleY));
    int newSmall = std::max(1, (int)(baseSmall * scaleY));

    HFONT hFontLargeNew = CreateFont(newLarge, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, _T("Segoe UI"));
    HFONT hFontMediumNew = CreateFont(newMedium, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, _T("Segoe UI"));
    HFONT hFontSmallNew = CreateFont(newSmall, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, _T("Segoe UI"));

    SendMessage(hTimeLabel, WM_SETFONT, (WPARAM)hFontLargeNew, TRUE);
    SendMessage(hCountdownLabel, WM_SETFONT, (WPARAM)hFontMediumNew, TRUE);
    SendMessage(hRealCountdownLabel, WM_SETFONT, (WPARAM)hFontSmallNew, TRUE);
    SendMessage(hSetTimeButton, WM_SETFONT, (WPARAM)hFontSmallNew, TRUE);
    SendMessage(hClipButton, WM_SETFONT, (WPARAM)hFontSmallNew, TRUE);
    SendMessage(hAboutButton, WM_SETFONT, (WPARAM)hFontSmallNew, TRUE);
    SendMessage(hThemeSwitch, WM_SETFONT, (WPARAM)hFontSmallNew, TRUE);

    if (g_hFontLarge) DeleteObject(g_hFontLarge);
    if (g_hFontMedium) DeleteObject(g_hFontMedium);
    if (g_hFontSmall) DeleteObject(g_hFontSmall);

    g_hFontLarge = hFontLargeNew;
    g_hFontMedium = hFontMediumNew;
    g_hFontSmall = hFontSmallNew;
}

//
// Функция перемещения элементов управления при изменении размера окна
//
void ResizeChildControls(HWND hwnd, int newWidth, int newHeight)
{
    double scaleX = (double)newWidth / BASE_WIDTH;
    double scaleY = (double)newHeight / BASE_HEIGHT;

    MoveWindow(hTimeLabel,
        (int)(LAYOUT_TIME.x * scaleX),
        (int)(LAYOUT_TIME.y * scaleY),
        (int)(LAYOUT_TIME.width * scaleX),
        (int)(LAYOUT_TIME.height * scaleY), TRUE);

    MoveWindow(hCountdownLabel,
        (int)(LAYOUT_COUNTDOWN.x * scaleX),
        (int)(LAYOUT_COUNTDOWN.y * scaleY),
        (int)(LAYOUT_COUNTDOWN.width * scaleX),
        (int)(LAYOUT_COUNTDOWN.height * scaleY), TRUE);

    MoveWindow(hRealCountdownLabel,
        (int)(LAYOUT_REALCOUNTDOWN.x * scaleX),
        (int)(LAYOUT_REALCOUNTDOWN.y * scaleY),
        (int)(LAYOUT_REALCOUNTDOWN.width * scaleX),
        (int)(LAYOUT_REALCOUNTDOWN.height * scaleY), TRUE);

    MoveWindow(hSetTimeButton,
        (int)(LAYOUT_SETTIME.x * scaleX),
        (int)(LAYOUT_SETTIME.y * scaleY),
        (int)(LAYOUT_SETTIME.width * scaleX),
        (int)(LAYOUT_SETTIME.height * scaleY), TRUE);

    MoveWindow(hClipButton,
        (int)(LAYOUT_CLIP.x * scaleX),
        (int)(LAYOUT_CLIP.y * scaleY),
        (int)(LAYOUT_CLIP.width * scaleX),
        (int)(LAYOUT_CLIP.height * scaleY), TRUE);


    MoveWindow(hThemeSwitch,
        (int)(LAYOUT_THEME.x * scaleX),
        (int)(LAYOUT_THEME.y * scaleY),
        (int)(LAYOUT_THEME.width * scaleX),
        (int)(LAYOUT_THEME.height * scaleY), TRUE);

    // Перемещение кнопки «О программе»
    MoveWindow(hAboutButton,
        (int)(LAYOUT_ABOUT.x * scaleX),
        (int)(LAYOUT_ABOUT.y * scaleY),
        (int)(LAYOUT_ABOUT.width * scaleX),
        (int)(LAYOUT_ABOUT.height * scaleY), TRUE);

    UpdateFonts(newHeight);
}

//
// Основная процедура окна
//
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        hBrushBackground = CreateSolidBrush(RGB(245, 245, 245));

        g_hFontLarge = CreateFont(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, _T("Segoe UI"));
        g_hFontMedium = CreateFont(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, _T("Segoe UI"));
        g_hFontSmall = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, _T("Segoe UI"));

        hTimeLabel = CreateWindowEx(0, _T("STATIC"), _T("00:00"), WS_CHILD | WS_VISIBLE | SS_CENTER,
            LAYOUT_TIME.x, LAYOUT_TIME.y, LAYOUT_TIME.width, LAYOUT_TIME.height,
            hwnd, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        SendMessage(hTimeLabel, WM_SETFONT, (WPARAM)g_hFontLarge, TRUE);

        hCountdownLabel = CreateWindowEx(0, _T("STATIC"), _T("До начала горы: 00:00"), WS_CHILD | WS_VISIBLE | SS_CENTER,
            LAYOUT_COUNTDOWN.x, LAYOUT_COUNTDOWN.y, LAYOUT_COUNTDOWN.width, LAYOUT_COUNTDOWN.height,
            hwnd, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        SendMessage(hCountdownLabel, WM_SETFONT, (WPARAM)g_hFontMedium, TRUE);

        hRealCountdownLabel = CreateWindowEx(0, _T("STATIC"), _T("Или в реальном времени: 0 мин"), WS_CHILD | WS_VISIBLE | SS_CENTER,
            LAYOUT_REALCOUNTDOWN.x, LAYOUT_REALCOUNTDOWN.y, LAYOUT_REALCOUNTDOWN.width, LAYOUT_REALCOUNTDOWN.height,
            hwnd, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        SendMessage(hRealCountdownLabel, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

        hSetTimeButton = CreateWindowEx(0, _T("BUTTON"), _T("Установить игровое время"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            LAYOUT_SETTIME.x, LAYOUT_SETTIME.y, LAYOUT_SETTIME.width, LAYOUT_SETTIME.height,
            hwnd, (HMENU)IDC_SET_TIME, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        SetWindowTheme(hSetTimeButton, L"", L"");

        hClipButton = CreateWindowEx(0, _T("BUTTON"), _T("📌"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            LAYOUT_CLIP.x, LAYOUT_CLIP.y, LAYOUT_CLIP.width, LAYOUT_CLIP.height,
            hwnd, (HMENU)IDC_CLIP, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        SetWindowTheme(hClipButton, L"", L"");


        hThemeSwitch = CreateWindowEx(0, L"BUTTON", L"Тёмная тема",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            LAYOUT_THEME.x, LAYOUT_THEME.y, LAYOUT_THEME.width, LAYOUT_THEME.height,
            hwnd, (HMENU)IDC_THEME_SWITCH, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        SetWindowTheme(hThemeSwitch, L"Switch", NULL);
        SendMessage(hThemeSwitch, BM_SETCHECK, g_darkTheme ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessage(hThemeSwitch, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

        // Создание кнопки «О программе»
        hAboutButton = CreateWindowEx(0, _T("BUTTON"), _T("О программе"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            LAYOUT_ABOUT.x, LAYOUT_ABOUT.y, LAYOUT_ABOUT.width, LAYOUT_ABOUT.height,
            hwnd, (HMENU)IDC_ABOUT, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        SetWindowTheme(hAboutButton, L"", L"");

        // Загружаем сохранённое игровое время
        LoadGameTime();

        g_realStart = GetSystemTimeULongLong();
        SetTimer(hwnd, IDT_TIMER, 200, NULL);

        ApplyTheme(hwnd);

        g_notificationTempPath = ExtractResourceToTempFile(((LPCREATESTRUCT)lParam)->hInstance,
            MAKEINTRESOURCE(IDR_NOTIFICATION),
            RT_RCDATA);
        if (g_notificationTempPath.empty())
            LogMessage(L"Не удалось извлечь ресурс уведомления.");
        else
            LogMessage(L"Ресурс уведомления успешно извлечён.");
    }
    break;
    case WM_SIZE:
    {
        int newWidth = LOWORD(lParam);
        int newHeight = HIWORD(lParam);
        ResizeChildControls(hwnd, newWidth, newHeight);
    }
    break;
    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
        if (pdis->CtlID == IDC_SET_TIME)
        {
            COLORREF accent = g_darkTheme ? RGB(10, 132, 255) : RGB(41, 121, 255);
            HBRUSH hAccentBrush = CreateSolidBrush(accent);
            FillRect(pdis->hDC, &pdis->rcItem, hAccentBrush);
            DeleteObject(hAccentBrush);

            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
            HPEN hOldPen = (HPEN)SelectObject(pdis->hDC, hPen);
            Rectangle(pdis->hDC, pdis->rcItem.left, pdis->rcItem.top, pdis->rcItem.right, pdis->rcItem.bottom);
            SelectObject(pdis->hDC, hOldPen);
            DeleteObject(hPen);

            SetBkMode(pdis->hDC, TRANSPARENT);
            SetTextColor(pdis->hDC, RGB(255, 255, 255));
            RECT rc = pdis->rcItem;
            DrawText(pdis->hDC, _T("Установить игровое время"), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        else if (pdis->CtlID == IDC_CLIP)
        {
            COLORREF bgColor = g_alwaysOnTop ? (g_darkTheme ? RGB(90, 90, 90) : RGB(200, 200, 200))
                                            : (g_darkTheme ? RGB(60, 60, 60) : RGB(245, 245, 245));
            HBRUSH hBrush = CreateSolidBrush(bgColor);
            FillRect(pdis->hDC, &pdis->rcItem, hBrush);
            DeleteObject(hBrush);

            SetBkMode(pdis->hDC, TRANSPARENT);
            SetTextColor(pdis->hDC, g_darkTheme ? RGB(255, 255, 255) : RGB(0, 0, 0));
            RECT rc = pdis->rcItem;
            DrawText(pdis->hDC, _T("📌"), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        else if (pdis->CtlID == IDC_ABOUT)
        {
            COLORREF accent = g_darkTheme ? RGB(10, 132, 255) : RGB(41, 121, 255);
            HBRUSH hAccentBrush = CreateSolidBrush(accent);
            FillRect(pdis->hDC, &pdis->rcItem, hAccentBrush);
            DeleteObject(hAccentBrush);

            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
            HPEN hOldPen = (HPEN)SelectObject(pdis->hDC, hPen);
            Rectangle(pdis->hDC, pdis->rcItem.left, pdis->rcItem.top, pdis->rcItem.right, pdis->rcItem.bottom);
            SelectObject(pdis->hDC, hOldPen);
            DeleteObject(hPen);

            SetBkMode(pdis->hDC, TRANSPARENT);
            SetTextColor(pdis->hDC, RGB(255, 255, 255));
            RECT rc = pdis->rcItem;
            DrawText(pdis->hDC, _T("О программе"), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
    }
    break;
    case WM_TIMER:
    {
        ULONGLONG currentTime = GetSystemTimeULongLong();
        double elapsed = (currentTime - g_realStart) / 10000000.0; // реальные секунды
        double gameTimeTotalMinutes = (elapsed + g_offset) / GAME_MINUTE_REAL_SECONDS;
        while (gameTimeTotalMinutes < 0)
            gameTimeTotalMinutes += MINUTES_IN_DAY;
        gameTimeTotalMinutes = std::fmod(gameTimeTotalMinutes, MINUTES_IN_DAY);

        int totalMinutes = (int)gameTimeTotalMinutes;
        std::wstring gameTimeStr = FormatTime(totalMinutes);
        SetWindowText(hTimeLabel, gameTimeStr.c_str());

        int remainder = totalMinutes % 360;
        int countdownMinutes;
        int nextMarker;
        if (remainder == 0)
        {
            countdownMinutes = 360;
            nextMarker = (totalMinutes + 360) % MINUTES_IN_DAY;
        }
        else
        {
            countdownMinutes = 360 - remainder;
            nextMarker = totalMinutes + countdownMinutes;
            if (nextMarker >= MINUTES_IN_DAY)
                nextMarker -= MINUTES_IN_DAY;
        }
        std::wstring countdownStr = L"До начала горы: " + FormatTime(countdownMinutes);
        SetWindowText(hCountdownLabel, countdownStr.c_str());

        double realCountdownSeconds = countdownMinutes * GAME_MINUTE_REAL_SECONDS;
        int realCountdownMinutes = (int)(realCountdownSeconds / 60);
        std::wstring realCountdownStr = L"Или в реальном времени: " + std::to_wstring(realCountdownMinutes) + L" мин";
        SetWindowText(hRealCountdownLabel, realCountdownStr.c_str());

        int beepTarget = (nextMarker - 10 + MINUTES_IN_DAY) % MINUTES_IN_DAY;
        if (g_lastBeepMarker != nextMarker && gameTimeTotalMinutes >= beepTarget && gameTimeTotalMinutes < beepTarget + 0.5)
        {
            mciSendString(_T("close mySound"), NULL, 0, NULL);
            if (!g_notificationTempPath.empty())
            {
                std::wstring commandOpen = _T("open \"") + g_notificationTempPath + _T("\" type mpegvideo alias mySound");
                mciSendString(commandOpen.c_str(), NULL, 0, NULL);

                TCHAR errBuffer[128] = { 0 };
                mciSendString(_T("play mySound"), errBuffer, 128, NULL);
                if (_tcslen(errBuffer) > 0)
                {
                    std::wstring errStr = _T("Ошибка MCI: ") + std::wstring(errBuffer);
                    MessageBox(hwnd, errStr.c_str(), _T("Ошибка MCI"), MB_OK);
                    LogMessage(errStr);
                }
                else
                {
                    LogMessage(L"Звук успешно воспроизведён.");
                }
            }
            else
            {
                LogMessage(L"Путь к файлу уведомления пуст. Невозможно воспроизвести звук.");
            }
            g_lastBeepMarker = nextMarker;
        }
    }
    break;
    case WM_CTLCOLORSTATIC:
    {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, g_darkTheme ? RGB(255, 255, 255) : RGB(0, 0, 0));
        SetBkColor(hdcStatic, g_darkTheme ? RGB(32, 32, 32) : RGB(245, 245, 245));
        return (LRESULT)hBrushBackground;
    }
    case WM_CTLCOLORBTN:
    {
        HDC hdcBtn = (HDC)wParam;
        SetTextColor(hdcBtn, g_darkTheme ? RGB(255, 255, 255) : RGB(0, 0, 0));
        SetBkColor(hdcBtn, g_darkTheme ? RGB(32, 32, 32) : RGB(245, 245, 245));
        return (LRESULT)hBrushBackground;
    }
    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_SET_TIME:
            if (DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SET_TIME), hwnd, SetTimeDlgProc) == IDOK)
            {
                LogMessage(L"Игровое время обновлено через диалог.");
            }
            break;
        case IDC_CLIP:
            g_alwaysOnTop = !g_alwaysOnTop;
            if (g_alwaysOnTop)
            {
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                LogMessage(L"Всегда поверх включено.");
            }
            else
            {
                SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                LogMessage(L"Всегда поверх отключено.");
            }
            InvalidateRect(hClipButton, NULL, TRUE);
            break;
        case IDC_THEME_SWITCH:
            g_darkTheme = (SendMessage(hThemeSwitch, BM_GETCHECK, 0, 0) == BST_CHECKED);
            ApplyTheme(hwnd);
            break;
        case IDC_ABOUT:
            // Запуск пользовательского диалога «О программе»
            DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_CUSTOM_ABOUTBOX), hwnd, AboutDlgProc);
            break;
        }
    }
    break;
    case WM_DESTROY:
        KillTimer(hwnd, IDT_TIMER);
        SaveGameTime();
        if (hBrushBackground) DeleteObject(hBrushBackground);
        if (g_hFontLarge) DeleteObject(g_hFontLarge);
        if (g_hFontMedium) DeleteObject(g_hFontMedium);
        if (g_hFontSmall) DeleteObject(g_hFontSmall);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

//
// Точка входа в приложение
//
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    const wchar_t CLASS_NAME[] = L"GameClockWindowClass";
    WNDCLASSEX wc = { };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(245, 245, 245));
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CLOCK));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));
    if (!RegisterClassEx(&wc))
    {
        return 0;
    }
    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        _T("Игровые часы"),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, BASE_WIDTH, BASE_HEIGHT,
        NULL,
        NULL,
        hInstance,
        NULL
    );
    if (hwnd == NULL)
    {
        return 0;
    }
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
