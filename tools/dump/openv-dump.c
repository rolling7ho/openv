/* openv-dump: dump a running process's main module image (as mapped) for offline analysis.
 * usage: openv-dump.exe <exe name> <out.bin>
 * Writes <out.bin> (SizeOfImage bytes, unreadable pages zeroed) and <out.bin>.txt (base + region map). */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>

static DWORD find_pid(const char *name)
{
    PROCESSENTRY32 pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    DWORD pid = 0;
    if (Process32First(snap, &pe))
        do if (!_stricmp(pe.szExeFile, name)) pid = pe.th32ProcessID; while (!pid && Process32Next(snap, &pe));
    CloseHandle(snap);
    return pid;
}

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: %s <exe> <out.bin>\n", argv[0]); return 2; }
    DWORD pid = find_pid(argv[1]);
    if (!pid) { printf("process %s not found\n", argv[1]); return 1; }
    HANDLE p = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!p) { printf("OpenProcess failed %lu\n", GetLastError()); return 1; }

    MODULEENTRY32 me = { sizeof(me) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    BYTE *base = NULL;
    if (Module32First(snap, &me))
        do if (!_stricmp(me.szModule, argv[1])) base = me.modBaseAddr; while (!base && Module32Next(snap, &me));
    CloseHandle(snap);
    if (!base) { printf("module not found\n"); return 1; }

    IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt; SIZE_T got;
    ReadProcessMemory(p, base, &dos, sizeof(dos), &got);
    ReadProcessMemory(p, base + dos.e_lfanew, &nt, sizeof(nt), &got);
    SIZE_T size = nt.OptionalHeader.SizeOfImage;
    BYTE *buf = calloc(1, size);

    char map[MAX_PATH];
    snprintf(map, sizeof(map), "%s.txt", argv[2]);
    FILE *mf = fopen(map, "w");
    fprintf(mf, "pid %lu base %p size %zx\n", pid, base, size);

    SIZE_T unread = 0;
    for (SIZE_T off = 0; off < size;) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQueryEx(p, base + off, &mbi, sizeof(mbi))) break;
        SIZE_T len = (BYTE *)mbi.BaseAddress + mbi.RegionSize - (base + off);
        if (off + len > size) len = size - off;
        fprintf(mf, "%08zx %08zx state %lx protect %lx\n", off, len, mbi.State, mbi.Protect);
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            for (SIZE_T o = 0; o < len; o += 0x1000) {
                SIZE_T chunk = len - o < 0x1000 ? len - o : 0x1000;
                if (!ReadProcessMemory(p, base + off + o, buf + off + o, chunk, &got)) unread += chunk;
            }
        else unread += len;
        off += len;
    }
    fclose(mf);

    FILE *f = fopen(argv[2], "wb");
    fwrite(buf, 1, size, f);
    fclose(f);
    printf("dumped %s pid %lu base %p size %zx unread %zx\n", argv[1], pid, base, size, unread);
    return 0;
}
