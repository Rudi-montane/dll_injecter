#include <windows.h>
#include <tlhelp32.h>
#include <commdlg.h>
#include <string>
#include <vector>

#pragma comment(lib, "Comdlg32.lib")

// globale variablen für gui elemente
HWND hProcessList, hDllPath, hInjectButton, hBrowseButton;

// funktion zum holen aller laufenden prozesse
std::vector<std::wstring> GetProcessList() {
    std::vector<std::wstring> processList;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return processList;

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    if (Process32First(hSnapshot, &pe32)) {
        do {
            processList.push_back(pe32.szExeFile);
        } while (Process32Next(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
    return processList;
}

// prozess id anhand des namens finden
DWORD GetProcessID(const std::wstring& processName) {
    DWORD processID = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    if (Process32First(hSnapshot, &pe32)) {
        do {
            if (processName == pe32.szExeFile) {
                processID = pe32.th32ProcessID;
                break;
            }
        } while (Process32Next(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
    return processID;
}

// dll in prozess injizieren
bool InjectDLL(DWORD processID, const std::wstring& dllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processID);
    if (!hProcess) return false;

    void* pRemoteMemory = VirtualAllocEx(hProcess, nullptr, dllPath.size() * sizeof(wchar_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteMemory) {
        CloseHandle(hProcess);
        return false;
    }

    WriteProcessMemory(hProcess, pRemoteMemory, dllPath.c_str(), dllPath.size() * sizeof(wchar_t), nullptr);
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
        (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "LoadLibraryW"),
        pRemoteMemory, 0, nullptr);

    if (!hThread) {
        VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, INFINITE);
    VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);
    return true;
}

// datei dialog für dll auswahl
std::wstring OpenFileDialog(HWND hwnd) {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAME ofn = { sizeof(OPENFILENAME) };
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"dll files\0*.dll\0all files\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileName(&ofn)) {
        return std::wstring(szFile);
    }
    return L"";
}

// fensterprozedur für gui
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // dropdown mit prozessen
        hProcessList = CreateWindow(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            20, 20, 300, 200, hwnd, NULL, NULL, NULL);

        // prozesse laden
        std::vector<std::wstring> processes = GetProcessList();
        for (const auto& process : processes) {
            SendMessage(hProcessList, CB_ADDSTRING, 0, (LPARAM)process.c_str());
        }

        // dll pfad eingabefeld
        hDllPath = CreateWindow(L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            20, 60, 300, 20, hwnd, NULL, NULL, NULL);

        // browse button
        hBrowseButton = CreateWindow(L"BUTTON", L"browse", WS_CHILD | WS_VISIBLE,
            330, 60, 80, 20, hwnd, (HMENU)1, NULL, NULL);

        // inject button
        hInjectButton = CreateWindow(L"BUTTON", L"inject", WS_CHILD | WS_VISIBLE,
            20, 100, 80, 30, hwnd, (HMENU)2, NULL, NULL);

        break;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 1) {  // browse button geklickt
            std::wstring dllPath = OpenFileDialog(hwnd);
            if (!dllPath.empty()) {
                SetWindowText(hDllPath, dllPath.c_str());
            }
        }
        else if (LOWORD(wParam) == 2) {  // inject button geklickt
            wchar_t processName[MAX_PATH];
            wchar_t dllPath[MAX_PATH];
            GetWindowText(hProcessList, processName, MAX_PATH);
            GetWindowText(hDllPath, dllPath, MAX_PATH);

            if (wcslen(processName) == 0 || wcslen(dllPath) == 0) {
                MessageBox(hwnd, L"bitte wähle einen prozess und eine dll aus", L"fehler", MB_OK | MB_ICONERROR);
                break;
            }

            DWORD processID = GetProcessID(processName);
            if (processID == 0) {
                MessageBox(hwnd, L"prozess nicht gefunden", L"fehler", MB_OK | MB_ICONERROR);
                break;
            }

            if (InjectDLL(processID, dllPath)) {
                MessageBox(hwnd, L"dll erfolgreich injiziert", L"erfolg", MB_OK | MB_ICONINFORMATION);
            }
            else {
                MessageBox(hwnd, L"fehler bei der dll injektion", L"fehler", MB_OK | MB_ICONERROR);
            }
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// hauptprogramm gui fenster erstellen
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"dllinjectorclass";
    WNDCLASS wc = { };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    HWND hwnd = CreateWindow(CLASS_NAME, L"dave juice injector 🚀", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 200, NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) return 0;
    ShowWindow(hwnd, nCmdShow);
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
