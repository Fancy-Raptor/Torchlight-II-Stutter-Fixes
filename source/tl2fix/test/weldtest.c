/* Differential test of the paperdoll weld loop: original Torchlight2.exe code (rva 0x1d16da..0x1d1790)
 * versus the patched exe's replacement, both executed in place from the real images. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

static void apply_relocs(unsigned char *base) {
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    DWORD delta = (DWORD)base - nt->OptionalHeader.ImageBase, off = 0, probe;
    memcpy(&probe, base + 0x406cc5, 4);
    if (probe != 0x021a6458u) return;               /* loader already relocated */
    while (off < dd.Size) {
        IMAGE_BASE_RELOCATION *blk = (IMAGE_BASE_RELOCATION *)(base + dd.VirtualAddress + off);
        WORD *e = (WORD *)(blk + 1); DWORD k, cnt = (blk->SizeOfBlock - 8) / 2;
        if (!blk->SizeOfBlock) break;
        for (k = 0; k < cnt; k++) if ((e[k] >> 12) == 3) {
            DWORD *p = (DWORD *)(base + blk->VirtualAddress + (e[k] & 0xfff)), old;
            VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &old); *p += delta; VirtualProtect(p, 4, old, &old);
        }
        off += blk->SizeOfBlock;
    }
}
static int resolve(unsigned char *base) {
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + nt->OptionalHeader.DataDirectory[1].VirtualAddress);
    int n = 0;
    for (; d->Name; d++) {
        const char *dll = (const char *)(base + d->Name); HMODULE h;
        IMAGE_THUNK_DATA32 *nm, *iat;
        if (_stricmp(dll, "MSVCR90.dll") && _stricmp(dll, "OgreMain.dll") && _stricmp(dll, "KERNEL32.dll")) continue;
        h = LoadLibraryA(dll); if (!h) { printf("load %s failed %lu\n", dll, GetLastError()); return -1; }
        nm = (IMAGE_THUNK_DATA32 *)(base + d->OriginalFirstThunk); iat = (IMAGE_THUNK_DATA32 *)(base + d->FirstThunk);
        for (; nm->u1.AddressOfData; nm++, iat++) {
            FARPROC f; DWORD old;
            if (nm->u1.Ordinal & 0x80000000) continue;
            f = GetProcAddress(h, (char *)((IMAGE_IMPORT_BY_NAME *)(base + nm->u1.AddressOfData))->Name);
            if (!f) continue;
            VirtualProtect(&iat->u1.Function, 4, PAGE_READWRITE, &old); iat->u1.Function = (DWORD)f; VirtualProtect(&iat->u1.Function, 4, old, &old); n++;
        }
    }
    return n;
}

/* --- runner: jump into the loop with a fake frame; a jmp planted at +0x1d1790 brings us back --- */
DWORD saved_esp, saved_regs[4];
DWORD out_ecx, out_esp; unsigned short out_sw, out_cw; long double out_st0;
WORD g_cw;
void exit_stub(void);
__asm__(
".globl _exit_stub\n_exit_stub:\n"
"  movl %ecx, _out_ecx\n  movl %esp, _out_esp\n"
"  fnstsw _out_sw\n"
"  fnstcw _out_cw\n"
"  fstpt _out_st0\n"
"  movl _saved_esp, %esp\n"
"  movl _saved_regs, %ebx\n  movl _saved_regs+4, %esi\n  movl _saved_regs+8, %edi\n  movl _saved_regs+12, %ebp\n"
"  ret\n");
void run_loop(void *entry, void *frame, DWORD n);
__asm__(
".globl _run_loop\n_run_loop:\n"
"  movl %ebx, _saved_regs\n  movl %esi, _saved_regs+4\n  movl %edi, _saved_regs+8\n  movl %ebp, _saved_regs+12\n"
"  movl 4(%esp), %eax\n  movl 8(%esp), %edx\n  movl 12(%esp), %ecx\n"
"  movl %esp, _saved_esp\n"
"  finit\n  fldcw _g_cw\n"
"  movl %edx, %esp\n"
"  jmp *%eax\n");

static void plant_exit(unsigned char *base) {
    unsigned char *site = base + 0x1d1790; DWORD old; LONG rel = (LONG)((unsigned char *)exit_stub - (site + 5));
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old); site[0] = 0xE9; memcpy(site + 1, &rel, 4); VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
}

static unsigned long long rs = 0x243F6A8885A308D3ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 13); }
static float frand(float a, float b) { return a + (b - a) * (rnd() / 4294967296.0f * 2.0f); }

static unsigned char *stackbuf;
static FARPROC o_sub, o_sq;
typedef void (__thiscall *sub_t)(const float *, float *, const float *);
typedef long double (__thiscall *sq_t)(const float *);
static void ref_weld(const float *pos, const float *w, int *grp, int n) {   /* the report's pseudocode, Ogre's own math */
    int i, j;
    for (i = 0; i < n; i++) {
        if (!(w[i] > 0) || grp[i] != 0) continue;
        for (j = 0; j < n; j++) {
            float r[3], sq; int g;
            if (j == i || !(w[j] > 0)) continue;
            ((sub_t)o_sub)(pos + 3*i, r, pos + 3*j);
            { long double x = ((sq_t)o_sq)(r); sq = 0; if (x <= (long double)9.9999997e-05f) sq = -1; }
            if (sq < 0) { g = grp[j] ? grp[j] : i + 1; grp[i] = g; grp[j] = g; }
        }
    }
}
static int run_one(unsigned char *base, float *pos, float *w, int *grp, int n, DWORD *ecx, int *depth, long double *st0) {
    unsigned char *frame = stackbuf + (1 << 20);
    memset(frame, 0, 0x400);
    *(DWORD *)(frame + 0x34) = n; *(float **)(frame + 0x58) = pos; *(float **)(frame + 0x1e8) = w; *(int **)(frame + 0x204) = grp;
    run_loop(base + 0x1d16da, frame, n);
    *ecx = out_ecx; *depth = 8 - ((out_sw >> 11) & 7) ; *st0 = out_st0;
    if (out_esp != (DWORD)frame) { printf("esp mismatch: %lx vs %lx\n", out_esp, (DWORD)frame); return -1; }
    return 0;
}

int main(int argc, char **argv) {
    unsigned char *A = (unsigned char *)LoadLibraryExA("tl2_orig.exe", NULL, DONT_RESOLVE_DLL_REFERENCES);
    unsigned char *B = (unsigned char *)LoadLibraryExA(getenv("PATCHED") ? getenv("PATCHED") : "tl2_patched.exe", NULL, DONT_RESOLVE_DLL_REFERENCES);
    int iters = argc > 1 ? atoi(argv[1]) : 300, it, mism = 0, cases_with_groups = 0, total_groups = 0;
    if (!A || !B) { printf("map failed\n"); return 2; }
    apply_relocs(A); apply_relocs(B);
    printf("orig at %p (%d imports)  patched at %p (%d imports)\n", A, resolve(A), B, resolve(B));
    plant_exit(A); plant_exit(B);
    { HMODULE o = GetModuleHandleA("OgreMain.dll"); o_sub = GetProcAddress(o, "??GVector3@Ogre@@QBE?AV01@ABV01@@Z"); o_sq = GetProcAddress(o, "?squaredLength@Vector3@Ogre@@QBEMXZ"); }
    stackbuf = VirtualAlloc(NULL, 2 << 20, MEM_COMMIT, PAGE_READWRITE);
    if (argc > 2 && !strcmp(argv[2], "tiny")) {   /* tiny case: two identical vertices + one far away */
        float pos[9] = {1,2,3, 1,2,3, 9,9,9}, w[3] = {1,1,1}; int g1[3] = {0}, g2[3] = {0}; DWORD e; int d; long double st;
        g_cw = 0x027f;
        run_one(A, pos, w, g1, 3, &e, &d, &st); printf("orig:    %d %d %d  ecx=%lu depth=%d st0=%g\n", g1[0], g1[1], g1[2], e, d, (double)st);
        run_one(B, pos, w, g2, 3, &e, &d, &st); printf("patched: %d %d %d  ecx=%lu depth=%d st0=%g\n", g2[0], g2[1], g2[2], e, d, (double)st);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "clobber")) {   /* does the patched code write into the position array? */
        int n = 200, i, changed[2] = {0, 0}, k; float *pos = malloc(12 * n), *snap = malloc(12 * n), w[200]; int g[200]; DWORD e; int d; long double st;
        for (i = 0; i < 3 * n; i++) pos[i] = frand(-1, 1);
        for (i = 0; i < n; i++) w[i] = 1;
        for (k = 0; k < 2; k++) {
            memcpy(snap, pos, 12 * n); memset(g, 0, sizeof g); g_cw = 0x027f;
            run_one(k ? B : A, pos, w, g, n, &e, &d, &st);
            for (i = 0; i < 3 * n; i++) changed[k] += memcmp(&snap[i], &pos[i], 4) != 0;
        }
        printf("position floats changed by run: original=%d patched=%d (of %d)\n", changed[0], changed[1], 3 * n);
        return 0;
    }
    for (it = 0; it < iters; it++) {
        int n, i, k, mode = it % 8;
        float *pos, *w; int *ga, *gb;
        DWORD ea, eb; int da, db; long double sa, sb;
        n = (it % 25 == 0) ? 1500 + rnd() % 1500 : rnd() % 400;
        if (it % 50 == 7) n = 0;
        if (it % 50 == 8) n = 1;
        pos = malloc(12 * (n + 1)); w = malloc(4 * (n + 1)); ga = malloc(4 * (n + 1)); gb = malloc(4 * (n + 1));
        for (i = 0; i < n; i++) {
            float *p = pos + 3 * i;
            if (i > 0 && rnd() % 3 == 0) {           /* near-duplicate of an earlier vertex */
                float *q = pos + 3 * (rnd() % i);
                float s = (rnd() % 4 == 0) ? 0.00995f + frand(0, 0.0001f) : frand(0, 0.012f);   /* straddle d=0.01 */
                float dx = frand(-1, 1), dy = frand(-1, 1), dz = frand(-1, 1), l = sqrtf(dx*dx + dy*dy + dz*dz) + 1e-9f;
                p[0] = q[0] + s * dx / l; p[1] = q[1] + s * dy / l; p[2] = q[2] + s * dz / l;
                if (rnd() % 5 == 0) { p[0] = q[0]; p[1] = q[1]; p[2] = q[2]; }
            } else {
                float sc = mode == 1 ? 0.2f : mode == 2 ? 50.f : mode == 3 ? 5000.f : 2.f;
                p[0] = frand(-sc, sc); p[1] = frand(-sc, sc); p[2] = frand(-sc, sc);
                if (mode == 4) { p[0] = floorf(p[0] / 0.05f) * 0.05f; }                            /* on cell borders */
                if (mode == 5 && rnd() % 10 == 0) p[rnd() % 3] = (rnd() & 1) ? NAN : INFINITY;
                if (mode == 6 && rnd() % 50 == 0) p[rnd() % 3] = frand(0.9e6f, 2e6f);              /* huge -> fallback */
            }
            switch (rnd() % 10) { case 0: w[i] = 0; break; case 1: w[i] = -frand(0, 1); break; case 2: w[i] = (mode == 7) ? NAN : 0.5f; break; default: w[i] = frand(0.01f, 1); }
            ga[i] = gb[i] = (rnd() % 20 == 0) ? (int)(rnd() % 5) : 0;         /* some pre-set group ids */
        }
        for (k = 0; k < 2; k++) {
            int *G[2]; memcpy(gb, ga, 4 * n);
            g_cw = k ? 0x027f : 0x007f;    /* 53-bit (CRT default) and 24-bit (Direct3D) precision */
            { int *a = malloc(4 * (n + 1)); memcpy(a, ga, 4 * n); G[0] = a; }
            G[1] = gb;
            if (run_one(A, pos, w, G[0], n, &ea, &da, &sa) || run_one(B, pos, w, G[1], n, &eb, &db, &sb)) return 3;
            { int *R = malloc(4 * (n + 1)), ra = 0, rb = 0; memcpy(R, ga, 4 * n); ref_weld(pos, w, R, n);
              ra = !memcmp(R, G[0], 4 * n); rb = !memcmp(R, G[1], 4 * n);
              if (it < 6) printf("iter %d n=%d cw=%04x: reference==orig %d, reference==patched %d\n", it, n, g_cw, ra, rb); free(R); }
            if (memcmp(G[0], G[1], 4 * n) || ea != eb || da != db || sa != sb) {
                if (mism < 3) {
                    int c0 = 0, c1 = 0, shown = 0;
                    for (i = 0; i < n; i++) { c0 += G[0][i] != 0; c1 += G[1][i] != 0; }
                    printf("  grouped vertices: orig %d, patched %d\n", c0, c1);
                    for (i = 0; i < n && shown < 4; i++) if (G[0][i] != G[1][i]) {
                        int j, best = -1; double bd = 1e30;
                        for (j = 0; j < n; j++) if (j != i) { double dx = pos[3*i]-pos[3*j], dy = pos[3*i+1]-pos[3*j+1], dz = pos[3*i+2]-pos[3*j+2], d = sqrt(dx*dx+dy*dy+dz*dz); if (d < bd) { bd = d; best = j; } }
                        printf("  v%d: orig grp %d, patched grp %d, preset %d, w=%g pos=(%g,%g,%g) nearest v%d at d=%.6f w=%g grp orig %d patched %d preset %d\n",
                               i, G[0][i], G[1][i], ga[i], w[i], pos[3*i], pos[3*i+1], pos[3*i+2], best, bd, best >= 0 ? w[best] : 0, best >= 0 ? G[0][best] : 0, best >= 0 ? G[1][best] : 0, best >= 0 ? ga[best] : 0);
                        shown++;
                    }
                }
                if (++mism <= 10) {
                    int first = -1; for (i = 0; i < n; i++) if (G[0][i] != G[1][i]) { first = i; break; }
                    printf("MISMATCH iter %d n=%d cw=%04x: ecx %lu/%lu fpu depth %d/%d first differing grp index %d (%d vs %d)\n",
                           it, n, g_cw, ea, eb, da, db, first, first >= 0 ? G[0][first] : 0, first >= 0 ? G[1][first] : 0);
                }
            } else {
                int g = 0; for (i = 0; i < n; i++) if (G[0][i]) g++;
                if (g) cases_with_groups++;
                total_groups += g;
            }
            free(G[0]);
        }
        free(pos); free(w); free(ga); free(gb);
    }
    printf("runs=%d MISMATCHES=%d (runs that formed groups: %d, grouped vertices: %d)\n", iters * 2, mism, cases_with_groups, total_groups);
    return mism != 0;
}
