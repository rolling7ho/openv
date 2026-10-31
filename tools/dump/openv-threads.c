/* openv-threads: dump the live script thread array of GTA5_Enhanced.exe.
 * usage: openv-threads.exe <array rva hex> <bytes per thread hex> <out.bin>
 * Output records: u64 thread ptr, u64 vtable ptr, then <bytes> of the thread object. */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    PROCESSENTRY32 pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    DWORD pid = 0;
    if (Process32First(snap, &pe))
        do if (!_stricmp(pe.szExeFile, "GTA5_Enhanced.exe")) pid = pe.th32ProcessID; while (!pid && Process32Next(snap, &pe));
    HANDLE p = pid ? OpenProcess(PROCESS_VM_READ, FALSE, pid) : NULL;
    if (!p) { printf("no game\n"); return 1; }
    ULONG64 base = 0x140000000, arr[2] = { 0 };
    SIZE_T got, n = strtoull(argv[2], 0, 16);
    ReadProcessMemory(p, (void *)(base + strtoull(argv[1], 0, 16)), arr, 16, &got);
    unsigned count = (unsigned)(arr[1] & 0xffff);
    printf("threads data %llx count %u capacity %u\n", arr[0], count, (unsigned)((arr[1] >> 16) & 0xffff));
    FILE *f = fopen(argv[3], "wb");
    BYTE *buf = malloc(n);
    for (unsigned i = 0; i < count; i++) {
        ULONG64 t = 0, vt = 0;
        ReadProcessMemory(p, (void *)(arr[0] + i * 8), &t, 8, &got);
        memset(buf, 0, n);
        if (t) { ReadProcessMemory(p, (void *)t, &vt, 8, &got); ReadProcessMemory(p, (void *)t, buf, n, &got); }
        fwrite(&t, 8, 1, f); fwrite(&vt, 8, 1, f); fwrite(buf, 1, n, f);
    }
    fclose(f);
    return 0;
}
