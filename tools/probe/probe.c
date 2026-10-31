/* openv-probe.dll: validate crossmap entries by calling native handlers directly with a fake call context.
 * Injected into a running single-player GTA5_Enhanced.exe by inject.exe. Each call is fault-guarded
 * (vectored handler + __builtin_longjmp) so a wrong mapping logs FAULT instead of crashing the game.
 * Results go to research/probe.log. Research tool only; not part of OpenV itself. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "probe_natives.h"

typedef struct {
    void *ret;
    uint32_t argc, pad0;
    uint64_t *args;
    uint32_t datacount, pad1;
    uint8_t fixup[0x1e0];
} ctx_t;

typedef void (*handler_t)(ctx_t *);

static uint8_t *game;
static FILE *out;
static void *jb[5];
static volatile int probing;
static int pass, fail, faults;

static void fault_trampoline(void) { __builtin_longjmp(jb, 1); }

static LONG CALLBACK veh(EXCEPTION_POINTERS *e)
{
    if (!probing || e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    probing = 0;
    e->ContextRecord->Rip = (DWORD64)fault_trampoline;
    e->ContextRecord->Rsp &= ~0xfULL;
    e->ContextRecord->Rsp -= 8;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static uint64_t ret[4], argv[16];
static int nargs;

static void arg_i(int64_t v) { argv[nargs++] = (uint64_t)v; }
static void arg_f(float v) { argv[nargs] = 0; memcpy(&argv[nargs++], &v, 4); }
static void arg_p(const void *p) { argv[nargs++] = (uint64_t)(uintptr_t)p; }

/* returns 0 on success, -1 on fault */
static int call(uint32_t rva)
{
    static ctx_t c;
    memset(&c, 0, sizeof(c)); memset(ret, 0, sizeof(ret));
    c.ret = ret; c.argc = nargs; c.args = argv;
    int r = 0;
    if (__builtin_setjmp(jb) == 0) {
        probing = 1;
        ((handler_t)(game + rva))(&c);
        probing = 0;
    } else r = -1;
    nargs = 0;
    return r;
}

static int32_t ri(void) { return (int32_t)ret[0]; }
static float rf(void) { float f; memcpy(&f, &ret[0], 4); return f; }

static void check(const char *name, int ok, const char *fmt, ...)
{
    char buf[256]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    fprintf(out, "%-32s %s  %s\n", name, ok ? "PASS" : "FAIL", buf); fflush(out);
    ok ? pass++ : fail++;
}

#define GUARD(name, rva) if (call(rva)) { fprintf(out, "%-32s FAULT\n", name); fflush(out); faults++; } else

static uint32_t joaat(const char *s)
{
    uint32_t h = 0;
    for (; *s; s++) { char c = *s >= 'A' && *s <= 'Z' ? *s + 32 : *s; h += (uint8_t)c; h += h << 10; h ^= h >> 6; }
    h += h << 3; h ^= h >> 11; h += h << 15;
    return h;
}

static DWORD WINAPI run(void *unused)
{
    game = (uint8_t *)GetModuleHandleA("GTA5_Enhanced.exe");
    out = fopen("Z:\\Users\\ivan\\Desktop\\vodka\\OpenV\\research\\probe.log", "w");
    if (!out || !game) return 1;
    AddVectoredExceptionHandler(1, veh);
    fprintf(out, "openv-probe: game base %p\n", game);

    /* pure functions */
    arg_p("PLAYER_ZERO"); GUARD("GET_HASH_KEY", N_GET_HASH_KEY) check("GET_HASH_KEY", (uint32_t)ri() == joaat("PLAYER_ZERO"), "got %08x want %08x", (uint32_t)ri(), joaat("PLAYER_ZERO"));
    arg_f(-2.5f); GUARD("ABSF", N_ABSF) check("ABSF", rf() == 2.5f, "got %g", rf());
    arg_i(-7); GUARD("ABSI", N_ABSI) check("ABSI", ri() == 7, "got %d", ri());
    arg_f(45.0f); GUARD("TAN", N_TAN) check("TAN", fabsf(rf() - 1.0f) < 1e-3f, "tan(45deg) got %g", rf());
    arg_f(1.0f); arg_f(1.0f); GUARD("ATAN2", N_ATAN2) check("ATAN2", fabsf(rf() - 45.0f) < 1e-2f, "got %g", rf());
    arg_f(1); arg_f(0); arg_f(0); arg_f(1); GUARD("GET_ANGLE_BETWEEN_2D_VECTORS", N_GET_ANGLE_BETWEEN_2D_VECTORS) check("GET_ANGLE_BETWEEN_2D_VECTORS", fabsf(rf() - 90.0f) < 1e-2f, "got %g", rf());
    arg_f(0); arg_f(0); arg_f(0); arg_f(3); arg_f(4); arg_f(0); arg_i(1);
    GUARD("GET_DISTANCE_BETWEEN_COORDS", N_GET_DISTANCE_BETWEEN_COORDS) check("GET_DISTANCE_BETWEEN_COORDS", fabsf(rf() - 5.0f) < 1e-3f, "got %g", rf());
    arg_p("hello"); GUARD("GET_LENGTH_OF_LITERAL_STRING", N_GET_LENGTH_OF_LITERAL_STRING) check("GET_LENGTH_OF_LITERAL_STRING", ri() == 5, "got %d", ri());
    arg_p(""); GUARD("IS_STRING_NULL_OR_EMPTY", N_IS_STRING_NULL_OR_EMPTY) { int a = ri(); arg_p("x"); call(N_IS_STRING_NULL_OR_EMPTY); check("IS_STRING_NULL_OR_EMPTY", a == 1 && ri() == 0, "\"\"->%d \"x\"->%d", a, ri()); }
    arg_p(NULL); GUARD("IS_STRING_NULL", N_IS_STRING_NULL) { int a = ri(); arg_p("x"); call(N_IS_STRING_NULL); check("IS_STRING_NULL", a == 1 && ri() == 0, "NULL->%d \"x\"->%d", a, ri()); }
    arg_p("abc"); arg_p("abc"); GUARD("ARE_STRINGS_EQUAL", N_ARE_STRINGS_EQUAL) { int a = ri(); arg_p("abc"); arg_p("abd"); call(N_ARE_STRINGS_EQUAL); check("ARE_STRINGS_EQUAL", a == 1 && ri() == 0, "same->%d diff->%d", a, ri()); }
    { static int v; arg_p("42"); arg_p(&v); GUARD("STRING_TO_INT", N_STRING_TO_INT) check("STRING_TO_INT", v == 42, "ret %d out %d", ri(), v); }
    { int lo = 1 << 30, hi = -1; for (int i = 0; i < 50; i++) { arg_i(10); arg_i(20); if (call(N_GET_RANDOM_INT_IN_RANGE)) { lo = -1; break; } if (ri() < lo) lo = ri(); if (ri() > hi) hi = ri(); }
      check("GET_RANDOM_INT_IN_RANGE", lo >= 10 && hi < 20 && hi > lo, "50 draws in [%d,%d]", lo, hi); }

    /* platform / state */
    GUARD("IS_PC_VERSION", N_IS_PC_VERSION) check("IS_PC_VERSION", ri() == 1, "got %d", ri());
    GUARD("IS_DURANGO_VERSION", N_IS_DURANGO_VERSION) check("IS_DURANGO_VERSION", ri() == 0, "got %d", ri());
    GUARD("IS_STEAM_VERSION", N_IS_STEAM_VERSION) check("IS_STEAM_VERSION", ri() == 1, "got %d", ri());
    GUARD("GET_GAME_TIMER", N_GET_GAME_TIMER) { int a = ri(); Sleep(250); call(N_GET_GAME_TIMER); check("GET_GAME_TIMER", ri() > a && ri() - a < 2000, "%d -> %d after 250ms", a, ri()); }
    GUARD("GET_FRAME_COUNT", N_GET_FRAME_COUNT) { int a = ri(); Sleep(250); call(N_GET_FRAME_COUNT); check("GET_FRAME_COUNT", ri() > a, "%d -> %d after 250ms", a, ri()); }
    GUARD("GET_CLOCK_HOURS", N_GET_CLOCK_HOURS) { int h = ri(); call(N_GET_CLOCK_MINUTES); check("GET_CLOCK_HOURS/MINUTES", h >= 0 && h < 24 && ri() >= 0 && ri() < 60, "%02d:%02d", h, ri()); }
    GUARD("GET_CURRENT_LANGUAGE", N_GET_CURRENT_LANGUAGE) check("GET_CURRENT_LANGUAGE", ri() >= 0 && ri() < 13, "got %d (0 = english)", ri());
    GUARD("NETWORK_IS_SESSION_STARTED", N_NETWORK_IS_SESSION_STARTED) check("NETWORK_IS_SESSION_STARTED", ri() == 0, "got %d (single player)", ri());
    GUARD("NETWORK_IS_GAME_IN_PROGRESS", N_NETWORK_IS_GAME_IN_PROGRESS) check("NETWORK_IS_GAME_IN_PROGRESS", ri() == 0, "got %d", ri());

    /* player / entity: cross-checks between independently mapped natives */
    GUARD("PLAYER_ID", N_PLAYER_ID) {
        int pid = ri();
        check("PLAYER_ID", pid >= 0 && pid < 32, "got %d", pid);
        GUARD("PLAYER_PED_ID", N_PLAYER_PED_ID) {
            int ped = ri();
            arg_i(pid); GUARD("GET_PLAYER_PED", N_GET_PLAYER_PED) check("PLAYER_PED_ID == GET_PLAYER_PED(PLAYER_ID)", ped > 0 && ri() == ped, "ped %d, via player %d", ped, ri());
            arg_i(ped); GUARD("DOES_ENTITY_EXIST", N_DOES_ENTITY_EXIST) check("DOES_ENTITY_EXIST(player ped)", ri() == 1, "got %d", ri());
            arg_i(ped); GUARD("IS_PED_A_PLAYER", N_IS_PED_A_PLAYER) check("IS_PED_A_PLAYER(player ped)", ri() == 1, "got %d", ri());
            arg_i(ped); GUARD("GET_ENTITY_HEALTH", N_GET_ENTITY_HEALTH) check("GET_ENTITY_HEALTH(player ped)", ri() > 0 && ri() <= 1000, "got %d", ri());
            arg_i(ped); GUARD("GET_ENTITY_MODEL", N_GET_ENTITY_MODEL) {
                uint32_t m = (uint32_t)ri();
                int known = m == joaat("player_zero") || m == joaat("player_one") || m == joaat("player_two");
                check("GET_ENTITY_MODEL(player ped)", known, "got %08x (michael/franklin/trevor = %08x/%08x/%08x)", m, joaat("player_zero"), joaat("player_one"), joaat("player_two"));
            }
            arg_i(ped); GUARD("GET_PED_TYPE", N_GET_PED_TYPE) check("GET_PED_TYPE(player ped)", ri() >= 0 && ri() <= 2, "got %d (0-2 = story protagonists)", ri());
            arg_i(ped); arg_i(1); GUARD("GET_ENTITY_COORDS", N_GET_ENTITY_COORDS) {
                float x, y, z; memcpy(&x, &ret[0], 4); memcpy(&y, &ret[1], 4); memcpy(&z, &ret[2], 4);
                check("GET_ENTITY_COORDS(player ped)", fabsf(x) < 10000 && fabsf(y) < 10000 && fabsf(z) < 3000 && (x != 0 || y != 0), "(%.1f, %.1f, %.1f)", x, y, z);
            }
            arg_i(pid); GUARD("GET_PLAYER_NAME", N_GET_PLAYER_NAME) { const char *s = (const char *)(uintptr_t)ret[0]; check("GET_PLAYER_NAME", s && strlen(s) > 0 && strlen(s) < 32, "non-empty string, length %d", s ? (int)strlen(s) : -1); }
        }
    }
    fprintf(out, "pass %d fail %d fault %d\n", pass, fail, faults);
    fclose(out);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, void *r)
{
    if (reason == DLL_PROCESS_ATTACH) CloseHandle(CreateThread(NULL, 0, run, NULL, 0, NULL));
    return TRUE;
}
