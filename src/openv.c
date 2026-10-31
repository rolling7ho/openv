/* OpenV — open-source, drop-in ScriptHookV.dll for GTA V Enhanced.
 *
 * How it works (details in docs/re/):
 *  - Finds game internals by byte pattern (no hardcoded addresses), fails closed if any is missing.
 *  - Natives: universal hash -> handler via the per-build crossmap data file (OpenV\natives-<build>.txt),
 *    plus the game's own live registration table for current-build hashes.
 *  - Scripts run as fibers on the game's main thread, from a data-only hook: main_persistent's
 *    vtable pointer is swapped to a copy whose Update slot runs our tick. No game code is patched.
 *  - Single player only: refuses to start with BattlEye loaded and closes the game when a network
 *    session starts, as ScriptHookV does.
 *
 * MIT License. */
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>


/* ---------------------------------------------------------------- log */

static CRITICAL_SECTION g_log_lock;
static FILE *g_log;
static char g_dir[MAX_PATH];        /* game directory, with trailing backslash */

static void ov_log(const char *fmt, ...)
{
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    EnterCriticalSection(&g_log_lock);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_log_lock);
}

/* ---------------------------------------------------------------- patterns */

static uint8_t *g_base;
static uint8_t *g_text;
static size_t g_text_size;

static void find_text(void)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)g_base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(g_base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    /* the first executable section holds the plain (non-relocated) game code */
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
        if (s->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            g_text = g_base + s->VirtualAddress;
            g_text_size = s->Misc.VirtualSize;
            return;
        }
}

/* pattern: hex bytes separated by spaces, "??" = wildcard. Returns the unique match or NULL. */
static uint8_t *scan(const char *name, const char *pattern, int quiet)
{
    uint8_t bytes[128], mask[128];
    int n = 0;
    for (const char *p = pattern; *p && n < 128;) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (p[0] == '?') { mask[n] = 0; bytes[n++] = 0; p += 2; }
        else { mask[n] = 1; bytes[n++] = (uint8_t)strtoul(p, NULL, 16); p += 2; }
    }
    uint8_t *hit = NULL;
    int hits = 0;
    for (size_t i = 0; i + n <= g_text_size; i++) {
        if (g_text[i] != bytes[0]) continue;
        int j = 1;
        while (j < n && (!mask[j] || g_text[i + j] == bytes[j])) j++;
        if (j == n && !hits++) hit = g_text + i;
    }
    if (hits != 1) { if (!quiet) ov_log("PATTERN: %s: %d matches (need exactly 1)", name, hits); return NULL; }
    if (!quiet) ov_log("PATTERN: %s at rva %llx", name, (unsigned long long)(hit - g_base));
    return hit;
}

static uint8_t *rip(uint8_t *at, int disp_off, int ins_len) { return at + ins_len + *(int32_t *)(at + disp_off); }

/* ---------------------------------------------------------------- game layout (found by patterns) */

static struct {
    uint32_t *tls_index;        /* the game's _tls_index */
    uint32_t active_off;        /* TLS block offset of the active scrThread* */
    uint32_t flag_off;          /* TLS block byte set to 1 while a thread runs */
    void **current_thread;      /* global "current thread" pointer set by scrThread::Run */
    uint8_t ***threads;         /* rage::scrThread::sm_Threads data */
    uint16_t *threads_count;
    uint8_t **native_table;     /* 256 buckets */
} G;

enum {                          /* GtaThread layout (1158.16; verified live, docs/re/03-script-threads.md) */
    THREAD_ID = 0x08, THREAD_SCRIPT_HASH = 0x10, THREAD_STATE = 0x18,
    THREAD_STATE_RUNNING = 1, VT_UPDATE = 3, VT_COPY = 32,
};

static int resolve_patterns(int quiet)
{
    uint8_t *p;
    if (!(p = scan("scrThread::Run context",
        "8B 05 ?? ?? ?? ?? 65 48 8B 0C 25 58 00 00 00 48 8B 04 C1 4C 8B A0 ?? ?? ?? ?? 4C 8B 3D ?? ?? ?? ?? "
        "48 89 B0 ?? ?? ?? ?? 48 89 35 ?? ?? ?? ?? C6 80 ?? ?? ?? ?? 01", quiet))) return 0;
    G.tls_index = (uint32_t *)rip(p, 2, 6);
    G.active_off = *(uint32_t *)(p + 22);
    G.current_thread = (void **)rip(p + 40, 3, 7);
    G.flag_off = *(uint32_t *)(p + 49);
    if (!(p = scan("scrThread::sm_Threads",
        "85 C9 74 ?? 44 0F B7 05 ?? ?? ?? ?? 4D 85 C0 74 ?? 4C 8B 0D ?? ?? ?? ?? 31 D2 66 0F 1F 44 00 00 49 8B 04 D1 39 48 08", quiet))) return 0;
    G.threads_count = (uint16_t *)rip(p + 4, 4, 8);
    G.threads = (uint8_t ***)rip(p + 17, 3, 7);
    if (!(p = scan("native registration table",
        "48 8D 35 ?? ?? ?? ?? 4C 8D 05 ?? ?? ?? ?? 48 BA A6 67 D9 AD FB 34 DE 4E", quiet))) return 0;
    G.native_table = (uint8_t **)rip(p, 3, 7);
    if (!quiet) ov_log("INIT: tls index %u, active thread +%x, run flag +%x, current thread global rva %llx",
           *G.tls_index, G.active_off, G.flag_off, (unsigned long long)((uint8_t *)G.current_thread - g_base));
    return 1;
}

/* ---------------------------------------------------------------- natives */

typedef struct {
    uint64_t *ret;
    uint32_t argc, pad0;
    uint64_t *args;
    int32_t data_count, pad1;
    float *vec_orig[4];         /* Vector3* out-args the handler copied */
    struct { float x, y, z, w; } vec_temp[4];
} native_ctx;                   /* layout from the handler helper at rva 0x923590 (docs/re/03) */

typedef void (*native_handler)(native_ctx *);

#define NMAP_SIZE 32768         /* power of two, > 2 * natives */
static struct { uint64_t hash; native_handler fn; uint8_t flags; } g_nmap[NMAP_SIZE];
enum { NF_SOLID = 1, NF_WARNED = 2, NF_DISABLED = 4 };

static int nmap_slot(uint64_t hash)
{
    uint32_t i = (uint32_t)(hash ^ (hash >> 29)) & (NMAP_SIZE - 1);
    while (g_nmap[i].hash && g_nmap[i].hash != hash) i = (i + 1) & (NMAP_SIZE - 1);
    return i;
}

static void nmap_put(uint64_t hash, native_handler fn, uint8_t flags)
{
    int i = nmap_slot(hash);
    if (g_nmap[i].hash) return;     /* first source wins: crossmap before live table */
    g_nmap[i].hash = hash; g_nmap[i].fn = fn; g_nmap[i].flags = flags;
}

/* live table: same node encryption the game's register function uses (docs/re/01-native-table.md) */
static int load_live_table(int store)
{
    int total = 0;
    for (int b = 0; b < 256; b++) {
        int guard = 0;
        for (uint8_t *node = G.native_table[b]; node && guard++ < 10000;) {
            uint32_t *d = (uint32_t *)node;
            uint32_t count = d[0x48 / 4] ^ d[0x4c / 4] ^ (uint32_t)(uintptr_t)(node + 0x48);
            if (count > 7) break;
            for (uint32_t i = 0; i < count; i++) {
                uint32_t off = 0x54 + i * 16, key = (uint32_t)(uintptr_t)(node + off) ^ d[(off + 8) / 4];
                uint64_t hash = (uint64_t)(d[off / 4] ^ key) | (uint64_t)(d[(off + 4) / 4] ^ key) << 32;
                if (store) nmap_put(hash, *(native_handler *)(node + 0x10 + i * 8), NF_SOLID);
                total++;
            }
            uint32_t key = (uint32_t)(uintptr_t)node ^ d[2];
            node = (uint8_t *)(uintptr_t)((uint64_t)(d[0] ^ key) | (uint64_t)(d[1] ^ key) << 32);
        }
    }
    return total;
}

static int load_crossmap(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { ov_log("NATIVES: cannot open %s", path); return -1; }
    char line[256];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long long hash, rva;
        char conf[32];
        if (line[0] == '#' || sscanf(line, "%llx %llx %31s", &hash, &rva, conf) != 3) continue;
        int solid = !strcmp(conf, "anchor") || !strcmp(conf, "solid");
        nmap_put(hash, (native_handler)(g_base + rva), solid ? NF_SOLID : 0);
        n++;
    }
    fclose(f);
    return n;
}

/* fault isolation for native calls: a bad mapping logs and returns zeros instead of crashing */
static void *g_native_jb[5];
static volatile LONG g_in_native;
static DWORD g_main_thread_id;

static void native_fault_trampoline(void) { __builtin_longjmp(g_native_jb, 1); }

static LONG CALLBACK native_veh(EXCEPTION_POINTERS *e)
{
    if (!g_in_native || GetCurrentThreadId() != g_main_thread_id ||
        e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    g_in_native = 0;
    e->ContextRecord->Rip = (DWORD64)native_fault_trampoline;
    e->ContextRecord->Rsp = (e->ContextRecord->Rsp & ~0xfULL) - 8;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static uint64_t g_call_hash;
static uint64_t g_call_args[32];
static uint32_t g_call_argc;
static uint64_t g_call_ret[4];

static uint64_t *invoke(uint64_t hash)
{
    static native_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    memset(g_call_ret, 0, sizeof(g_call_ret));
    ctx.ret = g_call_ret; ctx.argc = g_call_argc; ctx.args = g_call_args;
    int slot = nmap_slot(hash);
    if (!g_nmap[slot].hash) {
        ov_log("NATIVE: %016llX is not mapped for this game build, returning 0", (unsigned long long)hash);
        g_nmap[slot].hash = hash; g_nmap[slot].flags = NF_DISABLED;
    }
    if (g_nmap[slot].flags & NF_DISABLED) return g_call_ret;
    if (!(g_nmap[slot].flags & (NF_SOLID | NF_WARNED))) {
        g_nmap[slot].flags |= NF_WARNED;
        ov_log("NATIVE: first call to %016llX (lower-confidence crossmap entry)", (unsigned long long)hash);
    }
    if (__builtin_setjmp(g_native_jb) == 0) {
        g_in_native = 1;
        g_nmap[slot].fn(&ctx);
        g_in_native = 0;
    } else {
        g_nmap[slot].flags |= NF_DISABLED;
        ov_log("NATIVE: %016llX faulted; disabled for this session", (unsigned long long)hash);
        memset(g_call_ret, 0, sizeof(g_call_ret));
        return g_call_ret;
    }
    for (int i = 0; i < ctx.data_count && i < 4; i++) {      /* copy Vector3 out-args back */
        float *o = ctx.vec_orig[i];
        o[0] = ctx.vec_temp[i].x; o[2] = ctx.vec_temp[i].y; o[4] = ctx.vec_temp[i].z;
    }
    return g_call_ret;
}

/* ---------------------------------------------------------------- scripts */

typedef struct {
    HMODULE module;
    void (*main)(void);
    void *fiber;
    DWORD wake;
    int done;
} script_t;

#define MAX_SCRIPTS 256
static script_t g_scripts[MAX_SCRIPTS];
static volatile LONG g_nscripts;
static CRITICAL_SECTION g_scripts_lock;
static void *g_main_fiber;
static script_t *g_current;     /* script whose fiber is running, NULL outside */
static int g_in_tick;

static void WINAPI script_fiber(void *param)
{
    script_t *s = param;
    ov_log("SCRIPT: starting %p (module %p)", s->main, s->module);
    s->main();
    ov_log("SCRIPT: %p returned", s->main);
    s->done = 1;
    for (;;) SwitchToFiber(g_main_fiber);
}

static void add_script(HMODULE module, void (*fn)(void), const char *kind)
{
    char name[MAX_PATH] = "?";
    GetModuleFileNameA(module, name, sizeof(name));
    EnterCriticalSection(&g_scripts_lock);
    if (g_nscripts < MAX_SCRIPTS) {
        script_t *s = &g_scripts[g_nscripts];
        s->module = module; s->main = fn; s->wake = 0; s->done = 0; s->fiber = NULL;
        g_nscripts++;
        ov_log("INIT: registered %s '%s' (%p)", kind, strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name, fn);
    }
    LeaveCriticalSection(&g_scripts_lock);
}

static uint8_t *g_hook_thread;          /* main_persistent GtaThread we run inside */
static volatile DWORD g_slice_start;    /* GetTickCount when the running script got control, 0 if none */
static HANDLE g_main_thread;
static int g_online;
static int g_launched, g_ready_ticks;

static void tick(void)
{
    if (!g_main_fiber) {
        g_main_fiber = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(NULL);
        g_main_thread_id = GetCurrentThreadId();
        g_main_thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_main_thread_id);
        ov_log("TICK: first tick on thread %lu", g_main_thread_id);
    }
    /* become the active script thread, exactly as scrThread::Run does */
    uint8_t *tls = ((uint8_t **)__readgsqword(0x58))[*G.tls_index];
    void *prev_active = *(void **)(tls + G.active_off), *prev_current = *G.current_thread;
    uint8_t prev_flag = tls[G.flag_off];
    *(void **)(tls + G.active_off) = g_hook_thread;
    tls[G.flag_off] = 1;
    *G.current_thread = g_hook_thread;
    g_in_tick = 1;

    /* two independent signals over 10 ticks, so one mis-mapped native cannot close a story-mode game */
    g_call_argc = 0;
    int started = *(int32_t *)invoke(0x9DE624D2FC4B603FULL);              /* NETWORK_IS_SESSION_STARTED */
    g_call_argc = 0;
    int in_session = *(int32_t *)invoke(0xCA97246103B63917ULL);           /* NETWORK_IS_IN_SESSION */
    g_online = started && in_session ? g_online + 1 : 0;
    if (g_online >= 10) {
        ov_log("GUARD: network session started; closing the game (OpenV is single-player only)");
        TerminateProcess(GetCurrentProcess(), 0);
    }

    /* like ScriptHookV, launch scripts only once the game has finished loading: some mods (Menyoo)
       block with Sleep() until game scripts exist, which would deadlock the loading screen */
    if (!g_launched) {
        g_call_argc = 0;
        int loading = *(int32_t *)invoke(0x10D0A8F259E93EC9ULL);              /* GET_IS_LOADING_SCREEN_ACTIVE */
        g_call_argc = 0;
        int player = *(int32_t *)invoke(0x4F8644AF03D0E0D6ULL);               /* PLAYER_ID */
        g_call_argc = 1; g_call_args[0] = (uint64_t)player;
        int playing = *(int32_t *)invoke(0x5E9564D8246B909AULL);              /* IS_PLAYER_PLAYING */
        g_ready_ticks = !loading && playing ? g_ready_ticks + 1 : 0;
        if (g_ready_ticks >= 30) { g_launched = 1; ov_log("TICK: game loaded, launching %ld scripts", g_nscripts); }
    }

    DWORD now = GetTickCount();
    for (int i = 0; g_launched && i < g_nscripts; i++) {
        script_t *s = &g_scripts[i];
        if (s->done || (int32_t)(now - s->wake) < 0) continue;
        if (!s->fiber && !(s->fiber = CreateFiberEx(0x10000, 0x800000, FIBER_FLAG_FLOAT_SWITCH, script_fiber, s))) {
            ov_log("SCRIPT: CreateFiber failed (%lu)", GetLastError()); s->done = 1; continue;
        }
        g_current = s;
        g_slice_start = GetTickCount() | 1;
        SwitchToFiber(s->fiber);
        g_slice_start = 0;
        g_current = NULL;
    }

    g_in_tick = 0;
    *(void **)(tls + G.active_off) = prev_active;
    tls[G.flag_off] = prev_flag;
    *G.current_thread = prev_current;
}

/* ---------------------------------------------------------------- data-only Update hook */

typedef uint32_t (*update_fn)(uint8_t *thread, uint32_t ops);
static update_fn g_orig_update;
static void *g_vtable_copy[VT_COPY];

static uint32_t hooked_update(uint8_t *thread, uint32_t ops)
{
    uint32_t r = g_orig_update(thread, ops);
    if (thread == g_hook_thread && *(uint32_t *)(thread + THREAD_STATE) == THREAD_STATE_RUNNING) tick();
    return r;
}

static uint32_t joaat(const char *s)
{
    uint32_t h = 0;
    for (; *s; s++) { h += (uint8_t)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s); h += h << 10; h ^= h >> 6; }
    h += h << 3; h ^= h >> 11; h += h << 15;
    return h;
}

static uint8_t *find_thread(uint32_t script_hash)
{
    uint8_t **arr = *G.threads;
    for (int i = 0; arr && i < *G.threads_count; i++)
        if (arr[i] && *(uint32_t *)(arr[i] + THREAD_SCRIPT_HASH) == script_hash &&
            *(uint32_t *)(arr[i] + THREAD_STATE) == THREAD_STATE_RUNNING) return arr[i];
    return NULL;
}

/* (re)attach to main_persistent; called from the watchdog thread */
static void attach_hook(void)
{
    uint8_t *t = find_thread(joaat("main_persistent"));
    if (!t) return;
    void **vt = *(void ***)t;
    if (vt == (void **)g_vtable_copy) { g_hook_thread = t; return; }
    if (!g_orig_update) {
        memcpy(g_vtable_copy, vt, sizeof(g_vtable_copy));
        g_orig_update = (update_fn)vt[VT_UPDATE];
        g_vtable_copy[VT_UPDATE] = (void *)hooked_update;
    } else if (vt[VT_UPDATE] != (void *)g_orig_update) {
        ov_log("HOOK: main_persistent has an unexpected vtable, not attaching");
        return;
    }
    g_hook_thread = t;
    InterlockedExchangePointer((void **)t, g_vtable_copy);
    ov_log("HOOK: attached to main_persistent (thread %p, id %u)", t, *(uint32_t *)(t + THREAD_ID));
}

/* ---------------------------------------------------------------- stall reports */

static void describe(uint64_t addr, char *out, size_t n)
{
    HMODULE m;
    char path[MAX_PATH];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (char *)addr, &m)
        && GetModuleFileNameA(m, path, sizeof(path))) {
        char *b = strrchr(path, '\\');
        snprintf(out, n, "%s+%llx", b ? b + 1 : path, (unsigned long long)(addr - (uint64_t)m));
    } else snprintf(out, n, "%llx", (unsigned long long)addr);
}

/* a script that does not yield freezes the game: log where the main thread is */
static void check_stall(void)
{
    static DWORD reported_slice; static int reports;
    DWORD start = g_slice_start;
    if (!start || (LONG)(GetTickCount() - start) < 3000 || !g_main_thread) return;
    if (start != reported_slice) { reported_slice = start; reports = 0; }
    if (reports >= 5) return;
    reports++;
    CONTEXT c = { .ContextFlags = CONTEXT_FULL };
    if (SuspendThread(g_main_thread) == (DWORD)-1) return;
    if (GetThreadContext(g_main_thread, &c)) {
        char where[300], line[2048];
        describe(c.Rip, where, sizeof(where));
        int len = snprintf(line, sizeof(line), "STALL: script %p has not yielded for %lums, rip %s, stack:",
                           g_current ? (void *)g_current->main : NULL, GetTickCount() - start, where);
        uint64_t *sp = (uint64_t *)c.Rsp;
        for (int i = 0, found = 0; i < 512 && found < 12 && len < (int)sizeof(line) - 80; i++) {
            MEMORY_BASIC_INFORMATION mbi;
            uint64_t v = sp[i];
            if (v < 0x10000 || !VirtualQuery((void *)v, &mbi, sizeof(mbi)) || !(mbi.Protect & 0xf0) || mbi.Type != MEM_IMAGE) continue;
            describe(v, where, sizeof(where));
            len += snprintf(line + len, sizeof(line) - len, " %s", where);
            found++;
        }
        ov_log("%s", line);
    }
    ResumeThread(g_main_thread);
}

/* ---------------------------------------------------------------- keyboard */

typedef void (*keyboard_handler)(DWORD, WORD, BYTE, BOOL, BOOL, BOOL, BOOL);
static keyboard_handler g_kbd[64];
static int g_nkbd;
static WNDPROC g_orig_wndproc;

static LRESULT CALLBACK ov_wndproc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
        BOOL up = msg == WM_KEYUP || msg == WM_SYSKEYUP;
        static int logged;
        if (logged < 6) { logged++; ov_log("KEY: msg %x vk %llx (handlers %d)", msg, (unsigned long long)w, g_nkbd); }
        for (int i = 0; i < g_nkbd; i++)
            if (g_kbd[i]) g_kbd[i]((DWORD)w, l & 0xffff, (l >> 16) & 0xff, (l >> 24) & 1, (l >> 29) & 1, (l >> 30) & 1, up);
    }
    return CallWindowProcW(g_orig_wndproc, h, msg, w, l);
}

static BOOL CALLBACK find_game_window(HWND h, LPARAM out)
{
    DWORD pid;
    char title[128];
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h)) return TRUE;
    GetWindowTextA(h, title, sizeof(title));
    if (strstr(title, "Grand Theft Auto V")) { *(HWND *)out = h; return FALSE; }
    return TRUE;
}

static HWND g_game_window;

/* (re)subclass the game window; the game may recreate it or replace its window procedure */
static void attach_keyboard(void)
{
    HWND h = NULL;
    EnumWindows(find_game_window, (LPARAM)&h);
    if (!h || (h == g_game_window && GetWindowLongPtrW(h, GWLP_WNDPROC) == (LONG_PTR)ov_wndproc)) return;
    g_game_window = h;
    char cls[64];
    GetClassNameA(h, cls, sizeof(cls));
    g_orig_wndproc = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)ov_wndproc);
    ov_log("INIT: keyboard hooked on window %p class '%s'", h, cls);
}

/* ---------------------------------------------------------------- init */

static struct { WORD major, minor, build, build_minor; } g_version;

static void read_game_version(void)
{
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    DWORD dummy, size = GetFileVersionInfoSizeA(exe, &dummy);
    void *buf = size ? malloc(size) : NULL;
    VS_FIXEDFILEINFO *fi;
    UINT len;
    if (buf && GetFileVersionInfoA(exe, 0, size, buf) && VerQueryValueA(buf, "\\", (void **)&fi, &len)) {
        g_version.major = HIWORD(fi->dwFileVersionMS); g_version.minor = LOWORD(fi->dwFileVersionMS);
        g_version.build = HIWORD(fi->dwFileVersionLS); g_version.build_minor = LOWORD(fi->dwFileVersionLS);
    }
    free(buf);
}

static DWORD WINAPI init_thread(void *unused)
{
    if (GetModuleHandleA("BEClient_x64.dll") || GetModuleHandleA("BEClient.dll")) {
        ov_log("GUARD: BattlEye is loaded; OpenV will not run. Launch story mode without BattlEye.");
        return 0;
    }
    /* the executable is decrypted at startup and natives register during init: wait for both */
    int ok = 0;
    for (int i = 0; i < 1200 && !ok; i++) {
        ok = resolve_patterns(1) && *G.threads && load_live_table(0) > 6000;
        if (!ok) Sleep(250);
    }
    resolve_patterns(0);                    /* log the final result (or what is missing) */
    if (!ok) { ov_log("INIT: FAILED - game internals not found; scripts will not run (game unaffected)"); return 0; }

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sOpenV\\natives-%u.%u.txt", g_dir, g_version.build, g_version.build_minor);
    int cm = load_crossmap(path), live = load_live_table(1);
    ov_log("INIT: natives: %d from crossmap, %d in game table", cm, live);
    if (cm < 0) { ov_log("INIT: FAILED - no crossmap for game %u.%u; scripts will not run", g_version.build, g_version.build_minor); return 0; }
    AddVectoredExceptionHandler(1, native_veh);

    for (;; Sleep(250)) {                   /* watchdog: keep both hooks attached across loads */
        attach_keyboard();
        attach_hook();
        check_stall();
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    InitializeCriticalSection(&g_log_lock);
    InitializeCriticalSection(&g_scripts_lock);
    GetModuleFileNameA(NULL, g_dir, sizeof(g_dir));
    char *slash = strrchr(g_dir, '\\');
    if (slash) slash[1] = 0;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sopenv.log", g_dir);
    g_log = fopen(path, "w");
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    find_text();
    read_game_version();
    ov_log("OpenV (ScriptHookV-compatible) - game %u.%u.%u.%u", g_version.major, g_version.minor, g_version.build, g_version.build_minor);
    CloseHandle(CreateThread(NULL, 0, init_thread, NULL, 0, NULL));
    return TRUE;
}

/* ---------------------------------------------------------------- ScriptHookV ABI (24 exports) */

void scriptRegister(HMODULE m, void (*fn)(void));  /* exported as ?scriptRegister@@YAXPEAUHINSTANCE__@@P6AXXZ@Z via src/openv.def */
void scriptRegister(HMODULE m, void (*fn)(void)) { add_script(m, fn, "script"); }
void scriptRegisterAdditionalThread(HMODULE m, void (*fn)(void));  /* exported as ?scriptRegisterAdditionalThread@@YAXPEAUHINSTANCE__@@P6AXXZ@Z via src/openv.def */
void scriptRegisterAdditionalThread(HMODULE m, void (*fn)(void)) { add_script(m, fn, "additional thread"); }

void scriptUnregisterModule(HMODULE m);  /* exported as ?scriptUnregister@@YAXPEAUHINSTANCE__@@@Z via src/openv.def */
void scriptUnregisterModule(HMODULE m)
{
    for (int i = 0; i < g_nscripts; i++) if (g_scripts[i].module == m) g_scripts[i].done = 1;
    ov_log("SCRIPT: unregistered module %p", m);
}

void scriptUnregisterFn(void (*fn)(void));  /* exported as ?scriptUnregister@@YAXP6AXXZ@Z via src/openv.def */
void scriptUnregisterFn(void (*fn)(void))
{
    for (int i = 0; i < g_nscripts; i++) if (g_scripts[i].main == fn) g_scripts[i].done = 1;
}

void scriptWait(DWORD ms);  /* exported as ?scriptWait@@YAXK@Z via src/openv.def */
void scriptWait(DWORD ms)
{
    script_t *s = g_current;
    if (!s) { Sleep(ms == MAXDWORD ? INFINITE : ms); return; }   /* called outside a script fiber */
    if (ms == MAXDWORD) s->done = 1;
    s->wake = GetTickCount() + (ms == MAXDWORD ? 0 : ms);
    SwitchToFiber(g_main_fiber);
}

bool scriptsAreLaunchedUsingReloading(void);  /* exported as ?scriptsAreLaunchedUsingReloading@@YA_NXZ via src/openv.def */
bool scriptsAreLaunchedUsingReloading(void) { return false; }

void nativeInit(uint64_t hash);  /* exported as ?nativeInit@@YAX_K@Z via src/openv.def */
void nativeInit(uint64_t hash) { g_call_hash = hash; g_call_argc = 0; }
void nativePush64(uint64_t v);  /* exported as ?nativePush64@@YAX_K@Z via src/openv.def */
void nativePush64(uint64_t v) { if (g_call_argc < 32) g_call_args[g_call_argc++] = v; }
uint64_t *nativeCall(void);  /* exported as ?nativeCall@@YAPEA_KXZ via src/openv.def */
uint64_t *nativeCall(void) { return invoke(g_call_hash); }
bool nativeCanExecuteInThisContext(void);  /* exported as ?nativeCanExecuteInThisContext@@YA_NXZ via src/openv.def */
bool nativeCanExecuteInThisContext(void) { return g_in_tick && g_current; }

void keyboardHandlerRegister(keyboard_handler h);  /* exported as ?keyboardHandlerRegister@@YAXP6AXKGEHHHH@Z@Z via src/openv.def */
void keyboardHandlerRegister(keyboard_handler h) { if (g_nkbd < 64) g_kbd[g_nkbd++] = h; }
void keyboardHandlerUnregister(keyboard_handler h);  /* exported as ?keyboardHandlerUnregister@@YAXP6AXKGEHHHH@Z@Z via src/openv.def */
void keyboardHandlerUnregister(keyboard_handler h)
{
    for (int i = 0; i < g_nkbd; i++) if (g_kbd[i] == h) g_kbd[i] = NULL;
}

/* Not implemented yet: log once and return safe values */
#define TODO(name) do { static int once; if (!once++) ov_log("TODO: %s is not implemented yet", name); } while (0)

/* placeholder until script globals are located: a scratch area so callers never dereference NULL */
static uint64_t g_fake_globals[0x10000];
uint64_t *getGlobalPtr(int id);  /* exported as ?getGlobalPtr@@YAPEA_KH@Z via src/openv.def */
uint64_t *getGlobalPtr(int id)
{
    TODO("getGlobalPtr (returning scratch memory)");
    return &g_fake_globals[(unsigned)id % 0x10000];
}
int worldGetAllVehicles(int *a, int n);  /* exported as ?worldGetAllVehicles@@YAHPEAHH@Z via src/openv.def */
int worldGetAllVehicles(int *a, int n) { TODO("worldGetAllVehicles"); return 0; }
int worldGetAllPeds(int *a, int n);  /* exported as ?worldGetAllPeds@@YAHPEAHH@Z via src/openv.def */
int worldGetAllPeds(int *a, int n) { TODO("worldGetAllPeds"); return 0; }
int worldGetAllObjects(int *a, int n);  /* exported as ?worldGetAllObjects@@YAHPEAHH@Z via src/openv.def */
int worldGetAllObjects(int *a, int n) { TODO("worldGetAllObjects"); return 0; }
int worldGetAllPickups(int *a, int n);  /* exported as ?worldGetAllPickups@@YAHPEAHH@Z via src/openv.def */
int worldGetAllPickups(int *a, int n) { TODO("worldGetAllPickups"); return 0; }
uint8_t *getScriptHandleBaseAddress(int h);  /* exported as ?getScriptHandleBaseAddress@@YAPEAEH@Z via src/openv.def */
uint8_t *getScriptHandleBaseAddress(int h) { TODO("getScriptHandleBaseAddress"); return NULL; }
int createTexture(const char *f);  /* exported as ?createTexture@@YAHPEBD@Z via src/openv.def */
int createTexture(const char *f) { TODO("createTexture"); return -1; }
void drawTexture(int id, int idx, int lvl, int t, float sx, float sy, float cx, float cy,
    float px, float py, float rot, float scale, float r, float g, float b, float a);  /* exported as ?drawTexture@@YAXHHHHMMMMMMMMMMMM@Z via src/openv.def */
void drawTexture(int id, int idx, int lvl, int t, float sx, float sy, float cx, float cy,
    float px, float py, float rot, float scale, float r, float g, float b, float a) { TODO("drawTexture"); }
void presentCallbackRegister(void (*cb)(void *));  /* exported as ?presentCallbackRegister@@YAXP6AXPEAX@Z@Z via src/openv.def */
void presentCallbackRegister(void (*cb)(void *)) { TODO("presentCallbackRegister"); }
void presentCallbackUnregister(void (*cb)(void *));  /* exported as ?presentCallbackUnregister@@YAXP6AXPEAX@Z@Z via src/openv.def */
void presentCallbackUnregister(void (*cb)(void *)) {}

/* ScriptHookV's eGameVersion has no Enhanced entries; VER_UNK. Use getGameVersionInfo. */
int getGameVersion(void);  /* exported as ?getGameVersion@@YA?AW4eGameVersion@@XZ via src/openv.def */
int getGameVersion(void) { return -1; }

typedef struct { WORD major, minor, build, build_minor; } GameVersionInfo;
GameVersionInfo getGameVersionInfo(void);  /* exported as ?getGameVersionInfo@@YA?AUGameVersionInfo@@XZ via src/openv.def */
GameVersionInfo getGameVersionInfo(void)
{
    GameVersionInfo v = { g_version.major, g_version.minor, g_version.build, g_version.build_minor };
    return v;
}
