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
#define ENABLE_SOUND 1
#include <stdint.h>
struct gb_s;
static uint8_t g_apu[0x30]; static FILE *g_snd; static long g_sframe; static struct gb_s *g_gbp;
uint8_t audio_read(uint16_t a);
void audio_write(uint16_t a, uint8_t v);
#include "Peanut-GB/peanut_gb.h"
uint8_t audio_read(uint16_t a) { return g_apu[(a - 0xff10) % 0x30]; }
void audio_write(uint16_t a, uint8_t v) {
    g_apu[(a - 0xff10) % 0x30] = v;
    if (g_snd) fprintf(g_snd, "%ld %02x %02x %02x:%04x\n", g_sframe, a & 0xff, v,
                       g_gbp->cpu_reg.pc.reg < 0x4000 ? 0 : g_gbp->selected_rom_bank, g_gbp->cpu_reg.pc.reg);
}

static uint8_t *rom; static size_t romsz;
static uint8_t cart_ram[0x8000];
static uint8_t fb[144][160];

static uint8_t g_lastbc, g_lastba; static uint32_t g_prevpc;
static void pc_hook(struct gb_s *gb, uint16_t pc) {
    if (g_snd) {
        uint8_t bc = gb->hram_io[0xbc], ba = gb->hram_io[0xba];
        if (bc != g_lastbc && bc) fprintf(g_snd, "X %ld bc %02x %05x de00=%02x\n", g_sframe, bc, g_prevpc, __gb_read(gb, 0xde00));
        if (ba != g_lastba) fprintf(g_snd, "X %ld ba %02x %05x\n", g_sframe, ba, g_prevpc);
        g_lastbc = bc; g_lastba = ba;
        g_prevpc = pc < 0x4000 ? pc : (pc < 0x8000 ? gb->selected_rom_bank * 0x10000u + pc : pc);
        if (pc == 0x4394 && gb->selected_rom_bank == 2) fprintf(g_snd, "A %ld %02x\n", g_sframe, __gb_read(gb, 0xde00));
    }
    if (g_snd && gb->selected_rom_bank == 0x0f && (pc == 0x4006 || pc == 0x4003)) {
        if (pc == 0x4006 || 1) {
            fprintf(g_snd, "%c %ld", pc == 0x4006 ? 'C' : 'I', g_sframe);
            for (int a = 0xcb00; a < 0xcb68; a++) fprintf(g_snd, " %02x", __gb_read(gb, a));
            for (int a = 0xffba; a < 0xffc6; a++) fprintf(g_snd, " %02x", __gb_read(gb, a));
            fprintf(g_snd, "\n");
        }
    }
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
    static struct gb_s gb; g_gbp = &gb; g_snd = fopen(getenv("SNDLOG") ? getenv("SNDLOG") : "/tmp/snd.log", "w");
    if (gb_init(&gb, rr, cr, cw, er, NULL) != GB_INIT_NO_ERROR) return 1;
    gb_init_lcd(&gb, line);
    FILE *sc = fopen(argv[3], "r"); char tok[256]; long frames = 0;
    while (fscanf(sc, "%255s", tok) == 1) {
        if (tok[0] == '#') { int c; while ((c = fgetc(sc)) != '\n' && c != EOF); continue; }
        if (tok[0] == 'w' && tok[1] >= '0' && tok[1] <= '9') { int n = atoi(tok + 1); for (int i = 0; i < n; i++) (gb_run_frame(&gb), g_sframe++); frames += n; }
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
            gb.direct.joypad &= ~btn(b); for (int i = 0; i < n; i++) (gb_run_frame(&gb), g_sframe++);
            gb.direct.joypad = 0xff; frames += n;
        } else {                                  // button or button*count
            char b[32]; int n = 1; char *st = strchr(tok, '*');
            if (st) { *st = 0; n = atoi(st + 1); } strcpy(b, tok);
            for (int k = 0; k < n; k++) {
                gb.direct.joypad &= ~btn(b); for (int i = 0; i < 3; i++) (gb_run_frame(&gb), g_sframe++);
                gb.direct.joypad = 0xff; for (int i = 0; i < 17; i++) (gb_run_frame(&gb), g_sframe++);
                frames += 20;
            }
        }
    }
    { char p[512]; snprintf(p, sizeof p, "%s/readers.bin", outdir); FILE *o = fopen(p, "wb"); fwrite(g_reader, 4, romsz, o); fclose(o);
      snprintf(p, sizeof p, "%s/reader_banks.bin", outdir); o = fopen(p, "wb"); fwrite(g_rb, 4, romsz * 16, o); fclose(o); }
    fprintf(stderr, "ran %ld frames\n", frames); if (g_snd) fclose(g_snd);
    return 0;
}
