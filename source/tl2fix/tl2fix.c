/*
 * tl2fix - applies the Torchlight 2 (GOG) stutter fixes to your own game files, or removes them.
 *
 *   Torchlight2.exe : paperdoll vertex-weld loop replaced with a grid search (identical results)
 *   OgreMain.dll    : timer no longer pins the thread to one CPU core on every clock read
 *
 * It contains only the byte differences, never game code. Every file is identified by its SHA-256
 * before anything is changed, the result is checked against the expected SHA-256 before it is
 * written, and unknown files are left alone.
 *
 * Usage: run it in the game folder (the one with Torchlight2.exe), or pass the folder as an argument.
 *   tl2fix.exe            interactive
 *   tl2fix.exe --apply    apply without asking
 *   tl2fix.exe --undo     restore the original files
 *
 * MIT license.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "patchdata.h"

#define VERSION "1.1"

/* ---------------------------------------------------------------- SHA-256 */
typedef struct { unsigned int h[8]; unsigned long long len; unsigned char buf[64]; unsigned n; } sha256_ctx;
static const unsigned int K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,
    0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
    0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,
    0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(sha256_ctx *c, const unsigned char *p) {
    unsigned int w[64], a, b, d, e, f, g, h, cc, i, t1, t2;
    for (i = 0; i < 16; i++) w[i] = (unsigned)p[4*i] << 24 | (unsigned)p[4*i+1] << 16 | (unsigned)p[4*i+2] << 8 | p[4*i+3];
    for (; i < 64; i++) w[i] = w[i-16] + (ROR(w[i-15],7) ^ ROR(w[i-15],18) ^ (w[i-15] >> 3)) + w[i-7] + (ROR(w[i-2],17) ^ ROR(w[i-2],19) ^ (w[i-2] >> 10));
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (i = 0; i < 64; i++) {
        t1 = h + (ROR(e,6) ^ ROR(e,11) ^ ROR(e,25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        t2 = (ROR(a,2) ^ ROR(a,13) ^ ROR(a,22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}
static void sha256_hex(const unsigned char *p, size_t n, char out[65]) {
    sha256_ctx c = {{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}, 0, {0}, 0};
    unsigned char pad[128]; size_t i, full = n / 64 * 64, rem = n - full, padlen;
    unsigned long long bits = (unsigned long long)n * 8;
    for (i = 0; i < full; i += 64) sha_block(&c, p + i);
    memset(pad, 0, sizeof pad); memcpy(pad, p + full, rem); pad[rem] = 0x80;
    padlen = rem < 56 ? 64 : 128;
    for (i = 0; i < 8; i++) pad[padlen - 1 - i] = (unsigned char)(bits >> (8 * i));
    sha_block(&c, pad); if (padlen == 128) sha_block(&c, pad + 64);
    for (i = 0; i < 8; i++) sprintf(out + 8 * i, "%08x", c.h[i]);
}

/* ---------------------------------------------------------------- files */
static WCHAR g_dir[MAX_PATH];

static unsigned char *read_file(const WCHAR *name, size_t *len) {
    WCHAR path[MAX_PATH * 2]; FILE *f; unsigned char *b; long n;
    swprintf(path, MAX_PATH * 2, L"%ls\\%ls", g_dir, name);
    f = _wfopen(path, L"rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(n + 8192);
    if (!b || fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f); *len = (size_t)n; return b;
}

/* Write next to the target, then swap it in, so a failure never leaves a half-written game file. */
static int write_file(const WCHAR *name, const unsigned char *b, size_t len) {
    WCHAR path[MAX_PATH * 2], tmp[MAX_PATH * 2]; FILE *f;
    swprintf(path, MAX_PATH * 2, L"%ls\\%ls", g_dir, name);
    swprintf(tmp, MAX_PATH * 2, L"%ls\\%ls.tl2fix-tmp", g_dir, name);
    f = _wfopen(tmp, L"wb"); if (!f) return 0;
    if (fwrite(b, 1, len, f) != len || fflush(f) || fclose(f)) { DeleteFileW(tmp); return 0; }
    {   /* a read-only game file would block the replace */
        DWORD a = GetFileAttributesW(path);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(path, a & ~FILE_ATTRIBUTE_READONLY);
    }
    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { DeleteFileW(tmp); return 0; }
    return 1;
}

static int backup_once(const WCHAR *name, const unsigned char *b, size_t len) {
    WCHAR bak[MAX_PATH], path[MAX_PATH * 2]; FILE *f;
    swprintf(bak, MAX_PATH, L"%ls.original", name);
    swprintf(path, MAX_PATH * 2, L"%ls\\%ls", g_dir, bak);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return 1;          /* keep the first backup */
    f = _wfopen(path, L"wb"); if (!f) return 0;
    if (fwrite(b, 1, len, f) != len) { fclose(f); return 0; }
    return fclose(f) == 0;
}

/* Apply runs forward (from -> to) or backward (to -> from), checking every byte first. */
static int apply_runs(unsigned char *b, size_t len, const struct run *r, int n, int forward) {
    int i;
    for (i = 0; i < n; i++) {
        const unsigned char *expect = forward ? r[i].from : r[i].to;
        if (r[i].off + r[i].len > len || memcmp(b + r[i].off, expect, r[i].len)) return 0;
    }
    for (i = 0; i < n; i++) memcpy(b + r[i].off, forward ? r[i].to : r[i].from, r[i].len);
    return 1;
}

enum state { ST_MISSING, ST_ORIGINAL, ST_FIXED, ST_OLDFIX, ST_UNKNOWN };
static const char *state_text[] = {"not found", "original (unpatched)", "patched (tl2fix " VERSION ")",
    "patched with the earlier exe (has a weld bug - apply to update it)", "UNKNOWN version - will not be touched"};

static enum state exe_state(unsigned char *b, size_t len) {
    char h[65];
    if (!b) return ST_MISSING;
    sha256_hex(b, len, h);
    if (!strcmp(h, SHA_EXE_ORIG)) return ST_ORIGINAL;
    if (!strcmp(h, SHA_EXE_FIXED)) return ST_FIXED;
    if (!strcmp(h, SHA_EXE_CHATGPT)) return ST_OLDFIX;
    return ST_UNKNOWN;
}
static enum state ogre_state(unsigned char *b, size_t len) {
    char h[65];
    if (!b) return ST_MISSING;
    sha256_hex(b, len, h);
    if (!strcmp(h, SHA_OGRE_ORIG)) return ST_ORIGINAL;
    if (!strcmp(h, SHA_OGRE_FIXED)) return ST_FIXED;
    return ST_UNKNOWN;
}

/* Turn the exe in b (in state st) into the target state; result checked by hash. */
static int exe_transform(unsigned char *b, size_t *len, enum state st, int to_fixed) {
    char h[65];
    if (to_fixed) {
        if (st == ST_OLDFIX) { if (!apply_runs(b, *len, upg_runs, UPG_NRUNS, 1)) return 0; }
        else if (st == ST_ORIGINAL) {
            if (*len != EXE_ORIG_LEN || !apply_runs(b, *len, exe_runs, EXE_NRUNS, 1)) return 0;
            memcpy(b + EXE_ORIG_LEN, exe_tail, sizeof exe_tail); *len = EXE_FIXED_LEN;
        } else return 0;
        sha256_hex(b, *len, h); return !strcmp(h, SHA_EXE_FIXED);
    } else {
        if (st == ST_OLDFIX) { if (!apply_runs(b, *len, upg_runs, UPG_NRUNS, 1)) return 0; st = ST_FIXED; }
        if (st != ST_FIXED || *len != EXE_FIXED_LEN || memcmp(b + EXE_ORIG_LEN, exe_tail, sizeof exe_tail)) return 0;
        *len = EXE_ORIG_LEN;
        if (!apply_runs(b, *len, exe_runs, EXE_NRUNS, 0)) return 0;
        sha256_hex(b, *len, h); return !strcmp(h, SHA_EXE_ORIG);
    }
}
static int ogre_transform(unsigned char *b, size_t len, int to_fixed) {
    char h[65];
    if (!apply_runs(b, len, ogre_runs, OGRE_NRUNS, to_fixed)) return 0;
    sha256_hex(b, len, h); return !strcmp(h, to_fixed ? SHA_OGRE_FIXED : SHA_OGRE_ORIG);
}

static int game_running(void) {
    WCHAR path[MAX_PATH * 2]; HANDLE h;
    swprintf(path, MAX_PATH * 2, L"%ls\\Torchlight2.exe", g_dir);
    h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_SHARING_VIOLATION;
    CloseHandle(h); return 0;
}

int wmain(int argc, WCHAR **argv) {
    int mode = 0, i, interactive = 1, ok = 1, changed = 0;
    unsigned char *exe, *ogre; size_t exe_len = 0, ogre_len = 0; enum state es, os;
    WCHAR *slash;

    printf("tl2fix %s - Torchlight 2 (GOG) stutter fixes\n\n", VERSION);
    GetModuleFileNameW(NULL, g_dir, MAX_PATH);
    slash = wcsrchr(g_dir, L'\\'); if (slash) *slash = 0;
    for (i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--apply")) { mode = 'a'; interactive = 0; }
        else if (!wcscmp(argv[i], L"--undo")) { mode = 'u'; interactive = 0; }
        else { wcsncpy(g_dir, argv[i], MAX_PATH - 1); g_dir[MAX_PATH - 1] = 0; }
    }
    exe = read_file(L"Torchlight2.exe", &exe_len);
    if (!exe) {   /* not next to the game: try the current directory */
        GetCurrentDirectoryW(MAX_PATH, g_dir);
        exe = read_file(L"Torchlight2.exe", &exe_len);
    }
    if (!exe) { wprintf(L"Torchlight2.exe not found. Put tl2fix.exe in the game folder and run it there.\n"); goto done_fail; }
    ogre = read_file(L"OgreMain.dll", &ogre_len);
    es = exe_state(exe, exe_len); os = ogre_state(ogre, ogre_len);
    wprintf(L"Game folder: %ls\n", g_dir);
    printf("  Torchlight2.exe : %s\n  OgreMain.dll    : %s\n\n", state_text[es], state_text[os]);

    if (interactive) {
        char line[16];
        printf("[A] apply the fixes   [U] undo (restore originals)   [Q] quit\n> ");
        if (!fgets(line, sizeof line, stdin)) goto done;
        mode = (line[0] | 0x20) == 'a' ? 'a' : (line[0] | 0x20) == 'u' ? 'u' : 0;
        if (!mode) goto done;
    }
    if (game_running()) { printf("Torchlight 2 appears to be running. Close it and try again.\n"); goto done_fail; }

    /* Torchlight2.exe */
    if (mode == 'a' && (es == ST_ORIGINAL || es == ST_OLDFIX)) {
        unsigned char *orig_copy = NULL;
        if (es == ST_ORIGINAL) { orig_copy = malloc(exe_len); memcpy(orig_copy, exe, exe_len); }
        if (!exe_transform(exe, &exe_len, es, 1)) { printf("Torchlight2.exe: patch check failed - nothing written.\n"); ok = 0; }
        else if (orig_copy && !backup_once(L"Torchlight2.exe", orig_copy, EXE_ORIG_LEN)) { printf("Torchlight2.exe: could not write the backup - nothing changed.\n"); ok = 0; }
        else if (!write_file(L"Torchlight2.exe", exe, exe_len)) { printf("Torchlight2.exe: could not write the file (is the folder read-only?).\n"); ok = 0; }
        else { printf("Torchlight2.exe: %s\n", es == ST_OLDFIX ? "updated to the corrected weld fix" : "patched (original saved as Torchlight2.exe.original)"); changed++; }
        free(orig_copy);
    } else if (mode == 'u' && (es == ST_FIXED || es == ST_OLDFIX)) {
        if (!exe_transform(exe, &exe_len, es, 0)) { printf("Torchlight2.exe: restore check failed - nothing written.\n"); ok = 0; }
        else if (!write_file(L"Torchlight2.exe", exe, exe_len)) { printf("Torchlight2.exe: could not write the file.\n"); ok = 0; }
        else { printf("Torchlight2.exe: restored to the original\n"); changed++; }
    } else if (es == ST_UNKNOWN) { printf("Torchlight2.exe: not a version tl2fix knows - left unchanged.\n"); ok = 0; }
    else printf("Torchlight2.exe: nothing to do\n");

    /* OgreMain.dll */
    if (mode == 'a' && os == ST_ORIGINAL) {
        unsigned char *orig_copy = malloc(ogre_len); memcpy(orig_copy, ogre, ogre_len);
        if (!ogre_transform(ogre, ogre_len, 1)) { printf("OgreMain.dll: patch check failed - nothing written.\n"); ok = 0; }
        else if (!backup_once(L"OgreMain.dll", orig_copy, ogre_len)) { printf("OgreMain.dll: could not write the backup - nothing changed.\n"); ok = 0; }
        else if (!write_file(L"OgreMain.dll", ogre, ogre_len)) { printf("OgreMain.dll: could not write the file.\n"); ok = 0; }
        else { printf("OgreMain.dll: patched (original saved as OgreMain.dll.original)\n"); changed++; }
        free(orig_copy);
    } else if (mode == 'u' && os == ST_FIXED) {
        if (!ogre_transform(ogre, ogre_len, 0) || !write_file(L"OgreMain.dll", ogre, ogre_len)) { printf("OgreMain.dll: restore failed - nothing written.\n"); ok = 0; }
        else { printf("OgreMain.dll: restored to the original\n"); changed++; }
    } else if (os == ST_UNKNOWN) { printf("OgreMain.dll: not a version tl2fix knows - left unchanged.\n"); ok = 0; }
    else printf("OgreMain.dll: nothing to do\n");

    printf("\n%s\n", ok ? (changed ? "Done." : "No changes needed.") : "Finished with problems - see above.");
    goto done;
done_fail:
    ok = 0;
done:
    if (interactive) { printf("\nPress Enter to close."); getchar(); }
    return ok ? 0 : 1;
}
