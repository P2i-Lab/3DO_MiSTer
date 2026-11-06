// Virtual MiSTer for the 3DO core: drives the clocks of sim_top, runs the 3DO code of
// Main_MiSTer (support/3do, compiled unchanged) with the HPS link emulated, saves the picture
// (PNG) and the sound (WAV), and plays scripted controller inputs and disc swaps.
//
// usage: Vsim_top --bios FILE [--cd FILE.cue|.iso] [options] [+ddr_lat=N]
//   --frames N          stop after N video frames (default 600)
//   --out DIR           output folder (default ./out)
//   --png LIST          frames saved as PNG: "120,300" and/or "every:60"
//   --wav               record the sound (DIR/audio.wav, 44.1 kHz stereo)
//   --press F1-F2:BTNS  hold buttons from frame F1 to F2; BTNS: A B C P X L R T UP DOWN LEFT RIGHT,
//                       joined with '+' (e.g. 300-310:A, 400-405:UP+P)
//   --swap F:FILE       insert another disc image at frame F (OSD "Insert Disk")
//   --reset F           OSD "Reset" at frame F (status bit 0 pulse and p3do_reset(), as Main's menu)
//   --kanji FILE        kanji ROM: Main_MiSTer takes kanji.rom from the folder of the disc
//   --save FILE         start with this .sav (copied to DIR/saves)
//   --max-wall S        stop after S seconds of real time
#include <verilated.h>
#include "Vsim_top.h"
#include "Vsim_top__Dpi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>

#include "support/3do/3do.h"
#include "file_io.h"
#include "user_io.h"
#include "spi.h"
#include "hardware.h"
#include "menu.h"

const char *sim_home_dir = ".";
const char *sim_save_dir = ".";

static VerilatedContext *ctx;
static Vsim_top *top;
static uint64_t cyc = 0;            // clk_sys cycles (20 ns)

// ------------------------------------------------------------------------------------------
// HPS link (hps_io_sim.sv): state set by the Main_MiSTer side, read on every clk_sys edge
static struct
{
	uint16_t ext_din = 0;
	int      strobe = 0, enable = 0;
	uint16_t ext_dout = 0;
	int      dl = 0, dl_wr = 0, dl_first = 0;
	uint16_t dl_index = 0, dl_data = 0;
	uint32_t dl_addr = 0;
	uint64_t status_lo = 0, status_hi = 0;
	uint32_t joy0 = 0;
	int      sd_ack = 0, sd_wr = 0, mounted = 0;
	uint16_t sd_addr = 0, sd_dout = 0;
	int      core_sd_rd = 0, core_sd_wr = 0;
	uint32_t core_sd_lba = 0;
	uint16_t core_sd_din = 0;
} hps;

void hps_cycle(int ext_dout, int sd_rd_i, int sd_wr_i, int sd_lba_i, int sd_buff_din_i,
               int *ext_din, int *ext_ctrl, int *dl_ctrl, int *dl_index, int *dl_addr, int *dl_data,
               long long *status_lo, long long *status_hi, int *joy0, int *sd_ctrl, int *sd_addr, int *sd_dout)
{
	hps.ext_dout = (uint16_t)ext_dout;
	hps.core_sd_rd = sd_rd_i;
	hps.core_sd_wr = sd_wr_i;
	hps.core_sd_lba = (uint32_t)sd_lba_i;
	hps.core_sd_din = (uint16_t)sd_buff_din_i;

	*ext_din = hps.ext_din;
	*ext_ctrl = hps.strobe | (hps.enable << 1);
	*dl_ctrl = hps.dl | (hps.dl_wr << 1);
	hps.dl_wr = 0;
	*dl_index = hps.dl_index;
	*dl_addr = (int)hps.dl_addr;
	*dl_data = hps.dl_data;
	*status_lo = (long long)hps.status_lo;
	*status_hi = (long long)hps.status_hi;
	*joy0 = (int)hps.joy0;
	*sd_ctrl = hps.sd_ack | (hps.sd_wr << 1) | (hps.mounted << 2);
	hps.sd_wr = 0;
	hps.mounted = 0;
	*sd_addr = hps.sd_addr;
	*sd_dout = hps.sd_dout;
}

// ------------------------------------------------------------------------------------------
// Picture and sound
static std::string out_dir = "out";
static std::vector<int> png_frames;
static int png_every = 0;
static bool rec_audio = false;
static std::vector<int16_t> audio;

static uint32_t crc_table[256];
static void crc_init()
{
	for (uint32_t n = 0; n < 256; n++) {
		uint32_t c = n;
		for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		crc_table[n] = c;
	}
}
static uint32_t crc32(const uint8_t *p, size_t n, uint32_t c = 0xFFFFFFFFu)
{
	while (n--) c = crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
	return c;
}
static void be32(std::vector<uint8_t> &v, uint32_t x)
{
	v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}
static void png_chunk(FILE *f, const char *type, const std::vector<uint8_t> &data)
{
	std::vector<uint8_t> c;
	be32(c, (uint32_t)data.size());
	c.insert(c.end(), type, type + 4);
	c.insert(c.end(), data.begin(), data.end());
	uint32_t crc = crc32(c.data() + 4, c.size() - 4) ^ 0xFFFFFFFFu;
	be32(c, crc);
	fwrite(c.data(), 1, c.size(), f);
}
// PNG without compression (stored deflate blocks)
static void png_write(const char *path, int w, int h, const uint32_t *px, int stride)
{
	std::vector<uint8_t> raw;
	raw.reserve((size_t)(w * 3 + 1) * h);
	for (int y = 0; y < h; y++) {
		raw.push_back(0);
		for (int x = 0; x < w; x++) {
			uint32_t p = px[y * stride + x];
			raw.push_back(p >> 16); raw.push_back(p >> 8); raw.push_back(p);
		}
	}
	std::vector<uint8_t> z = { 0x78, 0x01 };
	size_t pos = 0;
	do {
		size_t n = std::min<size_t>(65535, raw.size() - pos);
		z.push_back(pos + n == raw.size());
		z.push_back(n & 0xFF); z.push_back(n >> 8); z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
		z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
		pos += n;
	} while (pos < raw.size());
	uint32_t a = 1, b = 0;
	for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
	be32(z, (b << 16) | a);

	FILE *f = fopen(path, "wb");
	if (!f) { printf("cannot write %s\n", path); return; }
	static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	fwrite(sig, 1, 8, f);
	std::vector<uint8_t> ihdr;
	be32(ihdr, w); be32(ihdr, h);
	ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
	png_chunk(f, "IHDR", ihdr);
	png_chunk(f, "IDAT", z);
	png_chunk(f, "IEND", {});
	fclose(f);
}

static void wav_write(const char *path, const std::vector<int16_t> &s)
{
	FILE *f = fopen(path, "wb");
	if (!f) return;
	uint32_t data = (uint32_t)(s.size() * 2), rate = 44100;
	auto u32 = [&](uint32_t x) { fwrite(&x, 4, 1, f); };
	auto u16 = [&](uint16_t x) { fwrite(&x, 2, 1, f); };
	fwrite("RIFF", 1, 4, f); u32(36 + data); fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
	fwrite("data", 1, 4, f); u32(data);
	fwrite(s.data(), 2, s.size(), f);
	fclose(f);
}

static const int VW = 1024, VH = 640;
static std::vector<uint32_t> vbuf(VW * VH);
static int vx = 0, vy = -1, vmaxw = 0;
static bool last_de = false, last_vs = false;
static int frame_no = 0;

// pauses of the console (3DO.sv pause: waits for the DDR3), per frame
static uint64_t pause_cycles = 0, pause_run = 0, pause_max = 0, frame_cycles = 0;
static uint64_t total_pause = 0, total_cycles = 0, worst_pause = 0;

// CPU activity per frame: opcode fetches and their address range
static uint64_t fetches = 0;
static uint32_t fetch_min = 0xFFFFFFFF, fetch_max = 0, fetch_last = 0;
static int progress_every = 60;

// --pc-hist F1-F2: opcode fetches counted per 16-byte block during frames F1..F2, the 40 busiest
// blocks are printed at the end (where the CPU loops or waits)
static int pch_f1 = -1, pch_f2 = -1;
static std::vector<std::pair<uint32_t, uint64_t>> pc_hist_sorted();
#include <unordered_map>
static std::unordered_map<uint32_t, uint64_t> pc_hist;
static std::vector<std::pair<uint32_t, uint64_t>> pc_hist_sorted()
{
	std::vector<std::pair<uint32_t, uint64_t>> v(pc_hist.begin(), pc_hist.end());
	std::sort(v.begin(), v.end(), [](const std::pair<uint32_t, uint64_t> &x, const std::pair<uint32_t, uint64_t> &y) { return x.second > y.second; });
	return v;
}

// DSP activity between two progress lines
static uint64_t dsp_run = 0, dma2dsp = 0, audio_nz = 0;
static uint32_t dsp_pc_min = 0x3FF, dsp_pc_max = 0, fifo_ne = 0;
static bool dma2dsp_old = false;

// --watch LO-HI: CPU reads and writes in this address range are printed (at most watch_max),
// from frame watch_from (--watch-from F), only the writes with --watch-writes
struct Watch { uint32_t lo, hi; };
static std::vector<Watch> watches;
static int watch_count = 0, watch_max = 200, watch_from = 0;
static bool watch_writes = false;

static std::chrono::steady_clock::time_point wall0;

static bool want_png(int f)
{
	if (png_every && f % png_every == 0) return true;
	return std::find(png_frames.begin(), png_frames.end(), f) != png_frames.end();
}

static void end_frame()
{
	int h = std::min(vy + 1, VH), w = vmaxw;
	if (h > 0 && w > 0 && want_png(frame_no)) {
		char p[1024];
		snprintf(p, sizeof(p), "%s/frame_%05d.png", out_dir.c_str(), frame_no);
		png_write(p, w, h, vbuf.data(), VW);
	}
	total_pause += pause_cycles;
	total_cycles += frame_cycles;
	worst_pause = std::max(worst_pause, pause_max);
	if (progress_every && frame_no % progress_every == 0) {
		double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
		double sim = cyc / 50e6;
		int lit = 0;   // pixels brighter than the console's black (01 01 01)
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
			uint32_t p = vbuf[y * VW + x];
			lit += ((p >> 16) & 0xFF) > 0x10 || ((p >> 8) & 0xFF) > 0x10 || (p & 0xFF) > 0x10;
		}
		printf("[sim] frame %5d  %dx%d lit %4.1f%%  t=%.2f s  wall %.0f s (%.0fx)  pause %.1f%% max %.1f us"
		       "  cpu %llu fetches %08X-%08X last %08X\n",
		       frame_no, w, h, w * h ? 100.0 * lit / (w * h) : 0.0, sim, wall, sim > 0 ? wall / sim : 0.0,
		       frame_cycles ? 100.0 * pause_cycles / frame_cycles : 0.0, pause_max / 50.0,
		       (unsigned long long)fetches, fetches ? fetch_min : 0, fetch_max, fetch_last);
		printf("[dsp] run %4.1f%%  pc %03X-%03X  dma->dsp %llu  fifos with data %04X  audio non-zero %llu\n",
		       frame_cycles ? 100.0 * dsp_run / frame_cycles : 0.0, dsp_run ? dsp_pc_min : 0, dsp_pc_max,
		       (unsigned long long)dma2dsp, fifo_ne, (unsigned long long)audio_nz);
		fflush(stdout);
	}
	dsp_run = dma2dsp = audio_nz = 0;
	dsp_pc_min = 0x3FF;
	dsp_pc_max = 0;
	fifo_ne = 0;
	pause_cycles = pause_run = pause_max = frame_cycles = 0;
	fetches = 0;
	fetch_min = 0xFFFFFFFF;
	fetch_max = 0;
	frame_no++;
	vy = -1;
	vmaxw = 0;
}

static void video_sample()
{
	if (!top->CE_PIXEL) return;
	bool de = top->VGA_DE, vs = top->VGA_VS;
	if (vs && !last_vs) end_frame();
	last_vs = vs;
	if (de) {
		if (!last_de) { vy++; vx = 0; }
		if (vy >= 0 && vy < VH && vx < VW) vbuf[vy * VW + vx] = (top->VGA_R << 16) | (top->VGA_G << 8) | top->VGA_B;
		vx++;
		vmaxw = std::max(vmaxw, vx);
	}
	last_de = de;
}

static uint64_t audio_acc = 0;
static void audio_sample()
{
	if (!rec_audio) return;
	audio_acc += 44100;
	if (audio_acc >= 50000000) {
		audio_acc -= 50000000;
		audio.push_back((int16_t)top->AUDIO_L);
		audio.push_back((int16_t)top->AUDIO_R);
	}
}

static void tick()
{
	top->clk_sys = 1; top->clk_mem = 1; top->eval();
	top->clk_mem = 0; top->eval();
	top->clk_sys = 0; top->clk_mem = 1; top->eval();
	top->clk_mem = 0; top->eval();
	cyc++;

	frame_cycles++;
	if (top->CPU_PAUSE) {
		pause_cycles++;
		pause_run++;
		pause_max = std::max(pause_max, pause_run);
	}
	else pause_run = 0;

	if (top->DBG_FETCH) {
		uint32_t a = top->DBG_CPU_A;
		fetches++;
		fetch_min = std::min(fetch_min, a);
		fetch_max = std::max(fetch_max, a);
		fetch_last = a;
		if (pch_f1 >= 0 && frame_no >= pch_f1 && frame_no <= pch_f2) pc_hist[a & ~15u]++;
	}
	if (top->DBG_DSP_GW) {
		dsp_run++;
		dsp_pc_min = std::min<uint32_t>(dsp_pc_min, top->DBG_DSP_PC);
		dsp_pc_max = std::max<uint32_t>(dsp_pc_max, top->DBG_DSP_PC);
	}
	if (top->DBG_DMA_TO_DSP && !dma2dsp_old) dma2dsp++;
	dma2dsp_old = top->DBG_DMA_TO_DSP;
	fifo_ne |= top->DBG_EIFIFO_NE;
	if (top->AUDIO_L || top->AUDIO_R) audio_nz++;
	if (!watches.empty() && (top->DBG_WRITE || (top->DBG_READ && !watch_writes)) && watch_count < watch_max &&
	    frame_no >= watch_from) {
		uint32_t a = top->DBG_CPU_A;
		for (auto &w : watches) {
			if (a >= w.lo && a <= w.hi) {
				watch_count++;
				printf("[watch %llu] t=%.6f s frame %d pc~%08X %s %08X = %08X\n", (unsigned long long)(cyc * 2), cyc / 50e6, frame_no, fetch_last,
				       top->DBG_WRITE ? "write" : "read ", a, top->DBG_WRITE ? top->DBG_CPU_DO : top->DBG_CPU_DI);
				break;
			}
		}
	}

	video_sample();
	audio_sample();
}

static void run(int n) { while (n-- > 0) tick(); }

// ------------------------------------------------------------------------------------------
// Main_MiSTer services used by support/3do
unsigned long GetTimer(unsigned long offset) { return (unsigned long)(cyc / 50000) + offset; }
unsigned long CheckTimer(unsigned long t) { return (cyc / 50000) >= t; }
void Info(const char *message, int, int, int, int) { printf("[OSD] %s\n", message); }

void EnableIO() { hps.enable = 1; tick(); }
void DisableIO() { hps.enable = 0; run(2); }
uint16_t spi_w(uint16_t word)
{
	hps.ext_din = word;
	hps.strobe = 1;
	tick();            // io_strobe set
	hps.strobe = 0;
	tick();            // hps_ext takes the word
	tick();            // its answer is read
	return hps.ext_dout;
}
uint16_t spi_uio_cmd_cont(uint16_t cmd) { EnableIO(); return spi_w(cmd); }

static const int WORD_CYCLES = 6;   // one 16-bit word of a download every 120 ns
void user_io_set_index(unsigned char index) { hps.dl_index = index; run(2); }
void user_io_set_download(unsigned char enable, int)
{
	if (enable) { hps.dl_addr = 0; hps.dl_first = 1; hps.dl = 1; }
	else {
		if (hps.dl && !hps.dl_first) hps.dl_addr += 2;
		hps.dl = 0;
	}
	run(4);
}
static int cd_log = 0, cd_logged = 0;
void user_io_file_tx_data(const uint8_t *buf, uint32_t len)
{
	if (cd_log && (hps.dl_index == 8 || hps.dl_index == 12) && cd_logged < cd_log) {
		cd_logged++;
		printf("[Main %.3f s] %s %u bytes:", cyc / 50e6, hps.dl_index == 8 ? "sector" : "disc info", len);
		for (uint32_t i = 0; i < 16 && i < len; i++) printf(" %02X", buf[i]);
		printf("\n");
	}
	for (uint32_t i = 0; i < len; i += 2) {
		if (!hps.dl_first) hps.dl_addr += 2;
		hps.dl_first = 0;
		hps.dl_data = buf[i] | ((i + 1 < len ? buf[i + 1] : 0) << 8);
		hps.dl_wr = 1;
		run(WORD_CYCLES);
	}
}
int user_io_file_tx(const char *name, unsigned char index, char, char, char, uint32_t)
{
	FILE *f = fopen(name, "rb");
	if (!f) return 0;
	std::vector<uint8_t> d;
	uint8_t b[65536];
	size_t n;
	while ((n = fread(b, 1, sizeof(b), f)) > 0) d.insert(d.end(), b, b + n);
	fclose(f);
	printf("[Main] %s: %zu bytes, index %d\n", name, d.size(), index);
	fflush(stdout);
	user_io_set_index(index);
	user_io_set_download(1);
	user_io_file_tx_data(d.data(), (uint32_t)d.size());
	user_io_set_download(0);
	return 1;
}
void user_io_status_set(const char *opt, uint32_t value, int)
{
	int hi, lo;
	if (sscanf(opt, "[%d:%d]", &hi, &lo) != 2) {
		if (sscanf(opt, "[%d]", &lo) != 1) return;
		hi = lo;
	}
	for (int bit = lo; bit <= hi; bit++) {
		uint64_t &w = bit < 64 ? hps.status_lo : hps.status_hi;
		uint64_t m = 1ULL << (bit & 63);
		w = ((value >> (bit - lo)) & 1) ? (w | m) : (w & ~m);
	}
	run(2);
}

// the save file, mounted as SD image 0
static struct { std::string path; std::vector<uint8_t> data; int type = 0; bool dirty = false; } sd_img;
static void save_write()
{
	if (!sd_img.dirty || sd_img.path.empty()) return;
	FILE *f = fopen(sd_img.path.c_str(), "wb");
	if (f) { fwrite(sd_img.data.data(), 1, sd_img.data.size(), f); fclose(f); }
	printf("[Main] save written: %s\n", sd_img.path.c_str());
	sd_img.dirty = false;
}
int user_io_file_mount(const char *name, unsigned char, char pre, int)
{
	save_write();
	sd_img.path.clear();
	sd_img.data.clear();
	sd_img.type = 0;
	if (name && *name) {
		sd_img.path = name;
		sd_img.type = pre ? 2 : 1;
		FILE *f = fopen(name, "rb");
		if (f) {
			uint8_t b[4096];
			size_t n;
			while ((n = fread(b, 1, sizeof(b), f)) > 0) sd_img.data.insert(sd_img.data.end(), b, b + n);
			fclose(f);
		}
		printf("[Main] save %s: %s\n", name, sd_img.data.empty() ? "new (blank NVRAM served)" : "loaded");
	}
	hps.mounted = 1;
	run(2);
	return 1;
}
static void sd_service()
{
	if (!hps.core_sd_rd && !hps.core_sd_wr) return;
	bool rd = hps.core_sd_rd;
	uint32_t lba = hps.core_sd_lba;
	uint8_t blk[512];
	hps.sd_ack = 1;
	run(3);
	if (rd) {
		size_t off = (size_t)lba * 512;
		if (off < sd_img.data.size()) {
			size_t n = std::min<size_t>(512, sd_img.data.size() - off);
			memcpy(blk, &sd_img.data[off], n);
			memset(blk + n, 0, 512 - n);
		}
		else if (sd_img.type == 2) p3do_fill_blanksave(blk, lba);
		else memset(blk, 0, 512);
		for (int i = 0; i < 256; i++) {
			hps.sd_addr = i;
			hps.sd_dout = blk[2 * i] | (blk[2 * i + 1] << 8);
			hps.sd_wr = 1;
			run(2);
		}
	}
	else {
		for (int i = 0; i < 256; i++) {
			hps.sd_addr = i;
			run(3);
			blk[2 * i] = hps.core_sd_din & 0xFF;
			blk[2 * i + 1] = hps.core_sd_din >> 8;
		}
		if (!sd_img.path.empty()) {
			size_t off = (size_t)lba * 512;
			if (sd_img.data.size() < off + 512) sd_img.data.resize(off + 512);
			memcpy(&sd_img.data[off], blk, 512);
			sd_img.dirty = true;
		}
	}
	hps.sd_ack = 0;
	run(3);
}

// ------------------------------------------------------------------------------------------
// Scripted inputs
struct Press { int f0, f1; uint32_t mask; };
struct Swap { int f; std::string path; bool done; };
static std::vector<Press> presses;
static std::vector<Swap> swaps;
static std::vector<int> osd_resets;

static uint32_t button_mask(const std::string &s)
{
	static const struct { const char *name; int bit; } names[] = {
		{"RIGHT", 0}, {"LEFT", 1}, {"DOWN", 2}, {"UP", 3}, {"A", 4}, {"B", 5}, {"C", 6}, {"P", 7},
		{"X", 8}, {"R", 9}, {"L", 10}, {"T", 11}
	};
	uint32_t m = 0;
	size_t p = 0;
	while (p <= s.size()) {
		size_t q = s.find('+', p);
		std::string n = s.substr(p, q == std::string::npos ? std::string::npos : q - p);
		bool ok = false;
		for (auto &b : names) if (n == b.name) { m |= 1u << b.bit; ok = true; }
		if (!ok) { fprintf(stderr, "unknown button %s\n", n.c_str()); exit(2); }
		if (q == std::string::npos) break;
		p = q + 1;
	}
	return m;
}

static int last_event_frame = -1;
static void events()
{
	if (frame_no == last_event_frame) return;
	last_event_frame = frame_no;
	uint32_t m = 0;
	for (auto &p : presses) if (frame_no >= p.f0 && frame_no <= p.f1) m |= p.mask;
	hps.joy0 = m;
	for (int f : osd_resets) {
		if (frame_no == f) {
			printf("[sim] frame %d: OSD reset\n", frame_no);
			p3do_reset();
			user_io_status_set("[0]", 1, 0);
			user_io_status_set("[0]", 0, 0);	// at once, as Main's menu: the core stretches the pulse
		}
	}
	for (auto &s : swaps) {
		if (!s.done && frame_no >= s.f) {
			s.done = true;
			printf("[sim] frame %d: insert %s\n", frame_no, s.path.c_str());
			p3do_set_image(0, s.path.c_str());
		}
	}
}

static std::string shell_quote(const std::string &s)
{
	std::string r = "'";
	for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; }
	return r + "'";
}

// Main_MiSTer takes kanji.rom from the folder of the disc: the disc files are linked into a
// folder of the simulation that also holds the kanji ROM.
static std::string disc_with_kanji(const std::string &cd, const std::string &kanji)
{
	std::string dir = out_dir + "/disc";
	std::string src = cd.substr(0, cd.rfind('/'));
	std::string cmd = "rm -rf " + shell_quote(dir) + " && mkdir -p " + shell_quote(dir) +
	                  " && ln -s " + shell_quote(src) + "/* " + shell_quote(dir) + "/" +
	                  " && cp " + shell_quote(kanji) + " " + shell_quote(dir + "/kanji.rom");
	if (system(cmd.c_str())) { fprintf(stderr, "cannot prepare %s\n", dir.c_str()); exit(2); }
	return dir + cd.substr(cd.rfind('/'));
}

int main(int argc, char **argv)
{
	std::string bios, cd, kanji, save;
	int frames = 600;
	double max_wall = 0;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		auto next = [&]() -> std::string { if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", a.c_str()); exit(2); } return argv[++i]; };
		if (a == "--bios") bios = next();
		else if (a == "--cd") cd = next();
		else if (a == "--frames") frames = atoi(next().c_str());
		else if (a == "--out") out_dir = next();
		else if (a == "--wav") rec_audio = true;
		else if (a == "--kanji") kanji = next();
		else if (a == "--save") save = next();
		else if (a == "--max-wall") max_wall = atof(next().c_str());
		else if (a == "--progress") progress_every = atoi(next().c_str());
		else if (a == "--watch") {
			std::string v = next();
			Watch w;
			if (sscanf(v.c_str(), "%x-%x", &w.lo, &w.hi) != 2) { fprintf(stderr, "--watch LO-HI (hex)\n"); return 2; }
			watches.push_back(w);
		}
		else if (a == "--watch-max") watch_max = atoi(next().c_str());
		else if (a == "--watch-from") watch_from = atoi(next().c_str());
		else if (a == "--watch-writes") watch_writes = true;
		else if (a == "--pc-hist") {
			if (sscanf(next().c_str(), "%d-%d", &pch_f1, &pch_f2) != 2) { fprintf(stderr, "--pc-hist F1-F2\n"); return 2; }
		}
		else if (a == "--cd-log") cd_log = atoi(next().c_str());
		else if (a == "--png") {
			std::string v = next();
			size_t p = 0;
			while (p < v.size()) {
				size_t q = v.find(',', p);
				std::string t = v.substr(p, q == std::string::npos ? std::string::npos : q - p);
				if (t.rfind("every:", 0) == 0) png_every = atoi(t.c_str() + 6);
				else png_frames.push_back(atoi(t.c_str()));
				if (q == std::string::npos) break;
				p = q + 1;
			}
		}
		else if (a == "--press") {
			std::string v = next();
			int f0, f1;
			char btn[256];
			if (sscanf(v.c_str(), "%d-%d:%255s", &f0, &f1, btn) != 3) { fprintf(stderr, "--press F1-F2:BUTTONS\n"); return 2; }
			presses.push_back({ f0, f1, button_mask(btn) });
		}
		else if (a == "--reset") osd_resets.push_back(atoi(next().c_str()));
		else if (a == "--swap") {
			std::string v = next();
			size_t c = v.find(':');
			if (c == std::string::npos) { fprintf(stderr, "--swap FRAME:FILE\n"); return 2; }
			swaps.push_back({ atoi(v.substr(0, c).c_str()), v.substr(c + 1), false });
		}
		else if (a[0] == '+') ;
		else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
	}
	if (bios.empty()) { fprintf(stderr, "usage: %s --bios FILE [--cd FILE] [options]\n", argv[0]); return 2; }

	crc_init();
	std::string home = out_dir + "/home", saves = out_dir + "/saves";
	std::string cmd = "mkdir -p " + shell_quote(home) + " " + shell_quote(saves) + " && cp " + shell_quote(bios) + " " + shell_quote(home + "/boot.rom");
	if (system(cmd.c_str())) { fprintf(stderr, "cannot prepare %s\n", out_dir.c_str()); return 2; }
	static std::string home_s = home, saves_s = saves;
	sim_home_dir = home_s.c_str();
	sim_save_dir = saves_s.c_str();
	if (!cd.empty() && !kanji.empty()) cd = disc_with_kanji(cd, kanji);
	if (!save.empty() && !cd.empty()) {
		char sp[2048];
		FileGenerateSavePath(cd.c_str(), sp);
		std::string c2 = "cp " + shell_quote(save) + " " + shell_quote(sp);
		if (system(c2.c_str())) { fprintf(stderr, "cannot copy %s\n", save.c_str()); return 2; }
	}

	ctx = new VerilatedContext;
	ctx->commandArgs(argc, argv);
	top = new Vsim_top{ ctx };
	wall0 = std::chrono::steady_clock::now();

	top->reset = 1;
	top->clk_sys = 0;
	top->clk_mem = 0;
	top->eval();
	run(32);
	top->reset = 0;
	run(32);

	// Main_MiSTer: disc chosen in the OSD (BIOS from boot.rom when there is no cd_bios.rom)
	p3do_set_image(0, cd.c_str());
	printf("[sim] image loaded at t=%.3f s\n", cyc / 50e6);
	fflush(stdout);

	while (frame_no < frames && !ctx->gotFinish()) {
		tick();
		if ((cyc & 255) == 0) {
			p3do_poll();
			sd_service();
			events();
			if (max_wall > 0 && (cyc & 0xFFFFF) == 0 &&
			    std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count() > max_wall) {
				printf("[sim] wall time limit reached\n");
				break;
			}
		}
	}

	double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
	printf("[sim] end: %d frames, %.2f s of console time in %.0f s (%.0fx slower), pause %.2f%%, longest %.0f us\n",
	       frame_no, cyc / 50e6, wall, cyc ? wall / (cyc / 50e6) : 0.0,
	       total_cycles ? 100.0 * total_pause / total_cycles : 0.0, worst_pause / 50.0);
	if (pch_f1 >= 0) {
		auto v = pc_hist_sorted();
		uint64_t total = 0;
		for (auto &e : v) total += e.second;
		printf("[pc-hist] frames %d-%d: %llu fetches\n", pch_f1, pch_f2, (unsigned long long)total);
		for (size_t i = 0; i < v.size() && i < 40; i++)
			printf("[pc-hist] %08X %10llu %5.1f%%\n", v[i].first, (unsigned long long)v[i].second, total ? 100.0 * v[i].second / total : 0.0);
	}
	if (rec_audio) {
		std::string w = out_dir + "/audio.wav";
		wav_write(w.c_str(), audio);
		printf("[sim] sound: %s (%.2f s)\n", w.c_str(), audio.size() / 2 / 44100.0);
	}
	save_write();
	top->final();
	delete top;
	delete ctx;
	return 0;
}
