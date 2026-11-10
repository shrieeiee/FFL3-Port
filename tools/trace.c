// Coverage logger: Peanut-GB + per-instruction hook. Records every executed
// instruction start (ROM bank:addr and RAM addr), split into named phases.
// usage: cov rom.gb outdir script.txt
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct gb_s;
static uint32_t *g_rom_hits;   // indexed by rom offset
static uint32_t g_ram_hits[0x10000];
static uint32_t g_cur=0xffffffff; static uint32_t *g_reader; static uint32_t *g_rb; // reader per data byte, reader x bank counts
static void pc_hook(struct gb_s *gb, uint16_t pc);
#define GB_PC_HOOK(gb, pc) pc_hook(gb, pc)
#include "Peanut-GB/peanut_gb.h"

static uint8_t *rom; static size_t romsz;
static uint8_t peek(struct gb_s *gb, uint16_t a) {
    if (a < 0x4000) return rom[a];
    if (a < 0x8000) return rom[(size_t)gb->selected_rom_bank * 0x4000 + a - 0x4000];
    if (a >= 0xc000 && a < 0xe000) return gb->wram[a - 0xc000];
    if (a >= 0xff80) return gb->hram_io[a - 0xff00];
    return 0;
}
static uint8_t cart_ram[0x8000];
static uint8_t fb[144][160];

static FILE *g_tr; static long g_frame; static uint16_t g_hp[16]; static int g_trace_on; static uint8_t g_w[64];
static uint16_t g_watch = 0xd822; static int g_watchn = 20;
static uint8_t peek(struct gb_s *gb, uint16_t a);
static FILE *g_rp; static uint8_t g_snap[0x2080]; static int g_innative;
static void snap(struct gb_s *gb, uint8_t *buf) { memcpy(buf, gb->wram, 0x2000); memcpy(buf + 0x2000, gb->hram_io + 0x80, 0x80); }
static void trace(struct gb_s *gb, uint16_t pc) {
    if (!g_tr) return;
    unsigned bank = pc < 0x4000 ? 0 : gb->selected_rom_bank;
    if (g_rp && bank == 0x0c) {
        if (pc == 0x4008) fprintf(g_rp, "S %04x\n", gb->cpu_reg.de.reg);
        else if (pc == 0x4135) { uint16_t de = gb->cpu_reg.de.reg; fprintf(g_rp, "N %04x %ld\n", peek(gb, de + 1) | peek(gb, de + 2) << 8, g_frame); snap(gb, g_snap); g_innative = 1; }
        else if (pc == 0x4143 && g_innative) {
            uint8_t now[0x2080]; snap(gb, now); fprintf(g_rp, "D");
            for (int i = 0; i < 0x2080; i++) if (now[i] != g_snap[i]) fprintf(g_rp, " %04x:%02x", i < 0x2000 ? 0xc000 + i : 0xff80 + i - 0x2000, now[i]);
            fprintf(g_rp, "\n"); g_innative = 0;
        }
        else if ((pc == 0x4416 || pc == 0x42fa) && !g_innative) fprintf(g_rp, "R %02x\n", gb->cpu_reg.a);
    }
    if (bank == 0x0c && pc == 0x4000 && getenv("REPLAY") && !g_rp) {
        g_rp = fopen(getenv("REPLAY"), "w");
        FILE *o = fopen("/tmp/trout/enter.mem", "wb");
        for (unsigned a = 0; a < 0x10000; a++) fputc(__gb_read(gb, a), o); fclose(o);
    }
    if (bank == 0x0c && pc == 0x4000) {
        uint16_t sp = gb->cpu_reg.sp.reg;
        fprintf(g_tr, "f%ld ENTER de=%04x ret=%02x%02x %02x%02x %02x%02x\n", g_frame, gb->cpu_reg.de.reg, peek(gb, sp+1), peek(gb, sp), peek(gb, sp+3), peek(gb, sp+2), peek(gb, sp+5), peek(gb, sp+4));
    }
    if (bank == 0x0c && pc == 0x4008) {
        uint16_t de = gb->cpu_reg.de.reg;
        static int dumpde = -2, nd = 0;
        if (dumpde == -2) { const char *e = getenv("DUMP_DE"); dumpde = e ? (int)strtol(e, 0, 16) : -1; }
        if (de == dumpde) {
            char p[256]; snprintf(p, sizeof p, "/tmp/trout/de%04x_%02d.mem", de, nd++); FILE *o = fopen(p, "wb");
            for (unsigned a = 0; a < 0x10000; a++) fputc(__gb_read(gb, a), o); fclose(o);
            fprintf(g_tr, "DUMP %s\n", p);
        }
        fprintf(g_tr, "f%ld VM de=%04x op=%02x [", g_frame, de, peek(gb, de));
        for (int i = 0; i < 6; i++) fprintf(g_tr, "%02x ", peek(gb, de + i));
        fprintf(g_tr, "] cc00:");
        for (int i = 0; i < 0x14; i++) fprintf(g_tr, "%02x", peek(gb, 0xcc00 + i));
        fprintf(g_tr, "\n");
    }
    if (g_trace_on && bank == 0x0c) fprintf(g_tr, "  pc %04x a=%02x bc=%04x de=%04x hl=%04x\n", pc, gb->cpu_reg.a, gb->cpu_reg.bc.reg, gb->cpu_reg.de.reg, gb->cpu_reg.hl.reg);
    for (int i = 0; i < g_watchn; i++) {
        uint8_t v = peek(gb, g_watch + i);
        if (v != g_w[i]) { fprintf(g_tr, "f%ld W[%04x] %02x -> %02x at %02x:%04x de=%04x\n", g_frame, g_watch + i, g_w[i], v, bank, pc, gb->cpu_reg.de.reg); g_w[i] = v; }
    }
    for (int i = 0; i < 10; i++) {
        uint16_t a = 0xd010 + i * 0x80; uint16_t v = peek(gb, a) | peek(gb, a + 1) << 8;
        if (v != g_hp[i]) { fprintf(g_tr, "f%ld HP[%d] %d -> %d at %02x:%04x\n", g_frame, i, g_hp[i], v, bank, pc); g_hp[i] = v; }
    }
}
static void pc_hook(struct gb_s *gb, uint16_t pc) {
    trace(gb, pc);
    if (pc < 0x4000) { g_rom_hits[pc]++; g_cur = pc; }
    else if (pc < 0x8000) { g_cur = (uint32_t)gb->selected_rom_bank * 0x4000 + (pc - 0x4000); g_rom_hits[g_cur]++; }
    else { g_ram_hits[pc]++; g_cur = 0x80000000u | pc; }
}
static uint8_t rr(struct gb_s *g, const uint_fast32_t a) { (void)g;
    if (a < romsz && g_cur != 0xffffffff && !(a >= g_cur && a < g_cur + 3)) {
        if (!g_reader[a]) g_reader[a] = (g_cur & 0x80000000u) ? (0x100000u | (g_cur & 0xffff)) + 1 : g_cur + 1;
        if (!(g_cur & 0x80000000u)) g_rb[(g_cur << 4) | (a >> 14)]++;
    }
    return a < romsz ? rom[a] : 0xff; }
static uint8_t cr(struct gb_s *g, const uint_fast32_t a) { (void)g; return cart_ram[a & 0x7fff]; }
static void cw(struct gb_s *g, const uint_fast32_t a, const uint8_t v) { (void)g; cart_ram[a & 0x7fff] = v; }
static void er(struct gb_s *g, const enum gb_error_e e, const uint16_t a) { (void)g; fprintf(stderr, "gb error %d at %04x\n", e, a); }
static void line(struct gb_s *g, const uint8_t *px, const uint_fast8_t y) { (void)g; for (int x = 0; x < 160; x++) fb[y][x] = px[x] & 3; }

static const char *outdir;
static void dump_phase(const char *name) {
    char p[512]; snprintf(p, sizeof p, "%s/cov_%s.bin", outdir, name);
    FILE *f = fopen(p, "wb");
    fwrite(g_rom_hits, 4, romsz, f); fwrite(g_ram_hits, 4, 0x10000, f); fclose(f);
    memset(g_rom_hits, 0, romsz * 4); memset(g_ram_hits, 0, sizeof g_ram_hits);
}
static void shot(const char *name) {
    char p[512]; snprintf(p, sizeof p, "%s/%s.pgm", outdir, name);
    FILE *f = fopen(p, "wb"); fprintf(f, "P5 160 144 255\n");
    for (int y = 0; y < 144; y++) for (int x = 0; x < 160; x++) fputc(255 - fb[y][x] * 85, f);
    fclose(f);
}
static uint8_t btn(const char *s) {
    if (!strcmp(s, "a")) return JOYPAD_A; if (!strcmp(s, "b")) return JOYPAD_B;
    if (!strcmp(s, "select")) return JOYPAD_SELECT; if (!strcmp(s, "start")) return JOYPAD_START;
    if (!strcmp(s, "right")) return JOYPAD_RIGHT; if (!strcmp(s, "left")) return JOYPAD_LEFT;
    if (!strcmp(s, "up")) return JOYPAD_UP; if (!strcmp(s, "down")) return JOYPAD_DOWN;
    fprintf(stderr, "bad button %s\n", s); exit(1);
}
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); romsz = ftell(f); rewind(f);
    rom = malloc(romsz); fread(rom, 1, romsz, f); fclose(f);
    g_rom_hits = calloc(romsz, 4); outdir = argv[2]; g_reader = calloc(romsz, 4); g_rb = calloc(romsz * 16, 4);
    static struct gb_s gb;
    if (gb_init(&gb, rr, cr, cw, er, NULL) != GB_INIT_NO_ERROR) return 1;
    gb_init_lcd(&gb, line);
    FILE *sc = fopen(argv[3], "r"); char tok[256]; long frames = 0;
    while (fscanf(sc, "%255s", tok) == 1) {
        if (tok[0] == '#') { int c; while ((c = fgetc(sc)) != '\n' && c != EOF); continue; }
        if (tok[0] == 'w' && tok[1] >= '0' && tok[1] <= '9') { int n = atoi(tok + 1); for (int i = 0; i < n; i++) { gb_run_frame(&gb); g_frame++; } frames += n; }
        else if (!strncmp(tok, "trace:", 6)) { g_tr = fopen(tok + 6, "w"); }
        else if (!strcmp(tok, "steps:on")) g_trace_on = 1;
        else if (!strcmp(tok, "steps:off")) g_trace_on = 0;
        else if (!strncmp(tok, "phase:", 6)) dump_phase(tok + 6);
        else if (!strncmp(tok, "shot:", 5)) shot(tok + 5);
        else if (!strncmp(tok, "poke:", 5)) {      // poke:ADDR:VAL (hex)
            unsigned a, v; sscanf(tok + 5, "%x:%x", &a, &v); __gb_write(&gb, (uint16_t)a, (uint8_t)v);
        }
        else if (!strncmp(tok, "dump:", 5)) {
            char p[512]; snprintf(p, sizeof p, "%s/%s.mem", outdir, tok + 5); FILE *o = fopen(p, "wb");
            for (unsigned a = 0; a < 0x10000; a++) fputc(__gb_read(&gb, a), o);
            fwrite(gb.vram, 1, 0x2000, o); fputc(gb.selected_rom_bank, o); fclose(o);
        }
        else if (!strncmp(tok, "hold:", 5)) {      // hold:button:frames
            char b[32]; int n; sscanf(tok + 5, "%31[^:]:%d", b, &n);
            gb.direct.joypad &= ~btn(b); for (int i = 0; i < n; i++) { gb_run_frame(&gb); g_frame++; }
            gb.direct.joypad = 0xff; frames += n;
        } else {                                  // button or button*count
            char b[32]; int n = 1; char *st = strchr(tok, '*');
            if (st) { *st = 0; n = atoi(st + 1); } strcpy(b, tok);
            for (int k = 0; k < n; k++) {
                gb.direct.joypad &= ~btn(b); for (int i = 0; i < 3; i++) { gb_run_frame(&gb); g_frame++; }
                gb.direct.joypad = 0xff; for (int i = 0; i < 17; i++) { gb_run_frame(&gb); g_frame++; }
                frames += 20;
            }
        }
    }
    { char p[512]; snprintf(p, sizeof p, "%s/readers.bin", outdir); FILE *o = fopen(p, "wb"); fwrite(g_reader, 4, romsz, o); fclose(o);
      snprintf(p, sizeof p, "%s/reader_banks.bin", outdir); o = fopen(p, "wb"); fwrite(g_rb, 4, romsz * 16, o); fclose(o); }
    fprintf(stderr, "ran %ld frames\n", frames); if (g_tr) fclose(g_tr); if (g_rp) fclose(g_rp);
    return 0;
}
