/* inject.exe <dll path>: LoadLibrary a DLL into the running GTA5_Enhanced.exe (research use, single player only).
 * Refuses if a BattlEye process is running. */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: %s <dll>\n", argv[0]); return 2; }
    PROCESSENTRY32 pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    DWORD pid = 0;
    if (Process32First(snap, &pe))
        do {
            if (strstr(pe.szExeFile, "BE.exe") || strstr(pe.szExeFile, "BEService")) { printf("BattlEye running (%s), refusing\n", pe.szExeFile); return 1; }
            if (!_stricmp(pe.szExeFile, "GTA5_Enhanced.exe")) pid = pe.th32ProcessID;
        } while (Process32Next(snap, &pe));
    CloseHandle(snap);
    if (!pid) { printf("game not running\n"); return 1; }

    char full[MAX_PATH];
    GetFullPathNameA(argv[1], MAX_PATH, full, NULL);
    HANDLE p = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    void *mem = VirtualAllocEx(p, NULL, sizeof(full), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(p, mem, full, strlen(full) + 1, NULL);
    HANDLE t = CreateRemoteThread(p, NULL, 0, (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32"), "LoadLibraryA"), mem, 0, NULL);
    WaitForSingleObject(t, 10000);
    DWORD code = 0; GetExitCodeThread(t, &code);
    printf("injected %s into pid %lu, LoadLibrary -> %lx\n", full, pid, code);
    return 0;
}
