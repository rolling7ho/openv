/* openv-natives: walk the live native registration table of a running GTA5_Enhanced.exe.
 * usage: openv-natives.exe <table rva hex> <out.tsv>
 * Node layout (from the register function, 1158.16):
 *   +0x00/+0x04 next ptr lo/hi, each ^ ((u32)node ^ +0x08)
 *   +0x10 + i*8   handler i (plain)
 *   +0x48         count ^ +0x4c ^ (u32)(node + 0x48)
 *   +0x54 + i*16  hash lo/hi, each ^ ((u32)(node + 0x54 + i*16) ^ +0x5c + i*16)
 * Output: hash \t handler_rva, one line per native. */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>

static HANDLE proc;

static void rd(ULONG64 addr, void *out, SIZE_T len)
{
    SIZE_T got;
    if (!ReadProcessMemory(proc, (void *)addr, out, len, &got)) memset(out, 0, len);
}

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: %s <table rva> <out.tsv>\n", argv[0]); return 2; }
    PROCESSENTRY32 pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    DWORD pid = 0;
    if (Process32First(snap, &pe))
        do if (!_stricmp(pe.szExeFile, "GTA5_Enhanced.exe")) pid = pe.th32ProcessID; while (!pid && Process32Next(snap, &pe));
    CloseHandle(snap);
    if (!pid || !(proc = OpenProcess(PROCESS_VM_READ, FALSE, pid))) { printf("no game\n"); return 1; }

    const ULONG64 base = 0x140000000;  /* image is not relocated under Wine; verify with openv-dump */
    ULONG64 table = base + strtoull(argv[1], 0, 16), buckets[256];
    rd(table, buckets, sizeof(buckets));

    FILE *f = fopen(argv[2], "w");
    int total = 0, nodes = 0, maxchain = 0;
    for (int b = 0; b < 256; b++) {
        int chain = 0;
        for (ULONG64 node = buckets[b]; node && chain < 10000; chain++, nodes++) {
            BYTE n[0x100];
            rd(node, n, sizeof(n));
            UINT32 *d = (UINT32 *)n;
            UINT32 count = d[0x48 / 4] ^ d[0x4c / 4] ^ (UINT32)(node + 0x48);
            if (count > 7) { printf("bucket %d node %llx bad count %u\n", b, node, count); break; }
            for (UINT32 i = 0; i < count; i++) {
                UINT32 off = 0x54 + i * 16, key = (UINT32)(node + off) ^ d[(off + 8) / 4];
                ULONG64 hash = (ULONG64)(d[off / 4] ^ key) | (ULONG64)(d[(off + 4) / 4] ^ key) << 32;
                ULONG64 handler = *(ULONG64 *)(n + 0x10 + i * 8);
                fprintf(f, "%016llX\t%llx\n", hash, handler - base);
                total++;
            }
            UINT32 key = (UINT32)node ^ d[2];
            node = (ULONG64)(d[0] ^ key) | (ULONG64)(d[1] ^ key) << 32;
        }
        if (chain > maxchain) maxchain = chain;
    }
    fclose(f);
    printf("natives %d nodes %d max chain %d\n", total, nodes, maxchain);
    return 0;
}
