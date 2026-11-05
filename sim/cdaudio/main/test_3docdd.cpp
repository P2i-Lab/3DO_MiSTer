// Host test of Main_MiSTer support/3do/3docdd.cpp (CD-DA, raw blocks, pacing): builds CUE/BIN and ISO
// images, drives p3docdd_t like support/3do/3do.cpp does (SetCommand, CommandExec, Process, Update)
// and checks the TOC and every byte sent to the core.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "support/3do/3do.h"

uint32_t p3do_frame_cnt = 0;

// ---- hardware.h stubs: a clock the test sets ----
static unsigned long fake_ms = 0;
unsigned long GetTimer(unsigned long offset) { return fake_ms + offset; }
unsigned long CheckTimer(unsigned long t) { return fake_ms >= t; }

// ---- file_io / chd stubs ----
int FileOpen(fileTYPE *file, const char *name, char)
{
	file->fp = fopen(name, "rb");
	if (!file->fp) return 0;
	fseeko(file->fp, 0, SEEK_END);
	file->size = ftello(file->fp);
	fseeko(file->fp, 0, SEEK_SET);
	return 1;
}
int FileClose(fileTYPE *file) { if (file->fp) fclose(file->fp); file->fp = 0; return 1; }
int FileSeek(fileTYPE *file, __off64_t offset, int origin) { return fseeko(file->fp, offset, origin) == 0; }
int FileReadAdv(fileTYPE *file, void *buf, int len, int) { return (int)fread(buf, 1, len, file->fp); }
int FileLoad(const char *name, void *buf, int size)
{
	FILE *f = fopen(name, "rb");
	if (!f) return 0;
	int n = (int)fread(buf, 1, size, f);
	fclose(f);
	return n;
}
const char *getFullPath(const char *name) { return name; }
const char *chd_error_string(chd_error) { return "stub"; }
void chd_close(chd_file *) {}
chd_error mister_load_chd(const char *, toc_t *) { return 1; }
chd_error mister_chd_read_sector(chd_file *, int, uint32_t, uint32_t, int, uint8_t *, uint8_t *, int *) { return 1; }

// ---- capture of what Main sends to the core ----
static std::vector<std::vector<uint8_t>> sent;
static int capture(uint8_t *buf, int len, uint8_t index)
{
	if (index == CD_DATA_IO_INDEX) sent.push_back(std::vector<uint8_t>(buf, buf + len));
	return 1;
}

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint8_t data_byte(int lba, int i) { return (uint8_t)(lba * 13 + i * 5 + 1); }
static uint8_t audio_byte(int lba, int i) { return (uint8_t)(lba * 7 + i * 3 + 0x40); }

// Raw Mode 1 sector of LBA lba (absolute) with a recognizable user-data pattern
static void make_raw_data(uint8_t *s, int lba)
{
	memset(s, 0, 2352);
	memset(s + 1, 0xFF, 10);
	int a = lba + 150;
	s[12] = BCD(a / 75 / 60); s[13] = BCD((a / 75) % 60); s[14] = BCD(a % 75); s[15] = 1;
	for (int i = 0; i < 2048; i++) s[16 + i] = data_byte(lba, i);
	for (int i = 2064; i < 2352; i++) s[i] = 0xEE;	// fake EDC/ECC
}

static void write_file(const char *name, const std::vector<uint8_t> &d)
{
	FILE *f = fopen(name, "wb");
	fwrite(d.data(), 1, d.size(), f);
	fclose(f);
}

// One READ of n blocks at absolute MSF, in block format fmt, the way the core asks for it
static void do_read(int abs_lba, int n, int fmt)
{
	uint8_t cmd[8] = { 0x10, (uint8_t)(abs_lba / 75 / 60), (uint8_t)((abs_lba / 75) % 60), (uint8_t)(abs_lba % 75),
	                   (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n, (uint8_t)fmt };
	uint8_t next[8] = { 0x20, 0, 0, 0, 0, 0, 0, 0 };
	uint8_t tm;
	sent.clear();
	for (int b = 0; b < n; b++)
	{
		p3docdd.SetCommand(b ? next : cmd);
		p3docdd.CommandExec();
		p3docdd.Process(&tm);
		p3docdd.Update();
	}
}

int main()
{
	// ---------- 1. BIN/CUE: data track + 2 audio tracks, one file per track (Redump style) ----------
	// track 1: 300 raw Mode 1 sectors (LBA 0..299)
	// track 2: audio, 150-frame pregap (INDEX 00) + 200 frames, file = 350 frames
	// track 3: audio, 100 frames
	std::vector<uint8_t> t1(300 * 2352), t2(350 * 2352), t3(100 * 2352);
	for (int l = 0; l < 300; l++) make_raw_data(&t1[l * 2352], l);
	for (int l = 0; l < 350; l++) for (int i = 0; i < 2352; i++) t2[l * 2352 + i] = audio_byte(1000 + l, i);
	for (int l = 0; l < 100; l++) for (int i = 0; i < 2352; i++) t3[l * 2352 + i] = audio_byte(2000 + l, i);
	write_file("t1.bin", t1); write_file("t2.bin", t2); write_file("t3.bin", t3);
	FILE *c = fopen("disc.cue", "w");
	fprintf(c, "FILE \"t1.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
	           "FILE \"t2.bin\" BINARY\n  TRACK 02 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 00:02:00\n"
	           "FILE \"t3.bin\" BINARY\n  TRACK 03 AUDIO\n    INDEX 01 00:00:00\n");
	fclose(c);

	CHECK(p3docdd.Load("disc.cue") > 0, "CUE load");
	p3docdd.SendData = capture;
	static uint8_t info[2048];
	CHECK(p3docdd.GetDiscInfo(info) > 0, "disc info");
	printf("TOC: first %d last %d leadout %02d:%02d:%02d\n", info[2], info[3], info[4], info[5], info[6]);
	for (int t = 1; t <= info[3] + 1; t++)
		printf("  entry %d: ctl %02x track %d start %02d:%02d:%02d\n", t, info[16*t+2], info[16*t+3], info[16*t+5], info[16*t+6], info[16*t+7]);
	CHECK(info[3] == 3, "3 tracks");
	CHECK(info[16*1+2] == 0x14 && info[16*2+2] == 0x10 && info[16*3+2] == 0x10, "track control bytes %02x %02x %02x", info[16*1+2], info[16*2+2], info[16*3+2]);

	// track 2 TOC start (as Main computes it) -> read its first 3 blocks in raw format
	int t2_abs = info[16*2+5] * 4500 + info[16*2+6] * 75 + info[16*2+7];
	const int t2_first_abs = t2_abs;
	do_read(t2_abs, 3, P3DO_FMT_RAW);
	CHECK(sent.size() == 3, "3 audio blocks sent (%zu)", sent.size());
	for (size_t b = 0; b < sent.size(); b++)
	{
		CHECK(sent[b].size() == 2354, "audio block size %zu", sent[b].size());
		// the track starts at the file start (Main keeps INDEX 00 in the track): file frame b
		int bad = -1;
		for (int i = 0; i < 2352 && bad < 0; i++)
		{
			uint8_t e = audio_byte(1000 + (int)b, i);	// samples in disc order, as the drive returns them
			if (sent[b][i] != e) bad = i;
		}
		CHECK(bad < 0, "audio block %zu byte %d = %02x, expected %02x", b, bad, bad >= 0 ? sent[b][bad] : 0, bad >= 0 ? audio_byte(1000 + (int)b, bad) : 0);
	}


	// subcode (2448-byte blocks, as the BIOS CD player reads): Q channel in bit 6, BCD positions, CRC
	do_read(t2_abs + 80, 2, P3DO_FMT_RAW | 4);
	CHECK(sent.size() == 2 && sent[0].size() == 2352 + 96 + 2, "subcode block size");
	for (size_t b = 0; b < sent.size(); b++)
	{
		uint8_t q[12] = {};
		bool pw_clear = true;
		for (int i = 0; i < 96; i++)
		{
			uint8_t s = sent[b][2352 + i];
			if (s & 0xBF) pw_clear = false;
			q[i >> 3] |= ((s >> 6) & 1) << (7 - (i & 7));
		}
		CHECK(pw_clear, "subcode: only the Q bit may be set");
		int lba = t2_abs - 150 + 80 + (int)b, rel = 80 + (int)b, abs = lba + 150;
		CHECK(q[0] == 0x01 && q[1] == 0x02 && q[2] == 0x01, "subcode Q control/track/index %02x %02x %02x", q[0], q[1], q[2]);
		CHECK(q[3] == BCD(rel / 75 / 60) && q[4] == BCD((rel / 75) % 60) && q[5] == BCD(rel % 75), "subcode Q relative time");
		CHECK(q[6] == 0 && q[7] == BCD(abs / 75 / 60) && q[8] == BCD((abs / 75) % 60) && q[9] == BCD(abs % 75), "subcode Q absolute time");
		uint16_t crc = 0;
		for (int i = 0; i < 10; i++) { crc ^= (uint16_t)q[i] << 8; for (int k = 0; k < 8; k++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1); }
		crc = (uint16_t)~crc;
		CHECK(q[10] == (crc >> 8) && q[11] == (crc & 0xFF), "subcode Q CRC");
		CHECK(sent[b][0] == audio_byte(1000 + 80 + (int)b, 0), "audio data with subcode");
	}
	// a known Q frame: track 1 audio, index 1, 00:00:00 relative, 00:02:00 absolute
	{
		uint8_t k[10] = { 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00 };
		uint16_t crc = 0;
		for (int i = 0; i < 10; i++) { crc ^= (uint16_t)k[i] << 8; for (int j = 0; j < 8; j++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1); }
		printf("Q CRC of 01 01 01 00 00 00 00 00 02 00 = %04X\n", (uint16_t)~crc);
	}
	// across the track 2 -> 3 boundary
	int t3_abs = info[16*3+5] * 4500 + info[16*3+6] * 75 + info[16*3+7];
	do_read(t3_abs - 1, 2, P3DO_FMT_RAW);
	CHECK(sent.size() == 2, "2 blocks across tracks");
	CHECK(sent.size() == 2 && sent[0][0] == audio_byte(1000 + 349, 0) && sent[1][0] == audio_byte(2000, 0), "track boundary data");

	// data track: 2048 user bytes (unchanged path) and raw 2352
	do_read(150 + 16, 1, P3DO_FMT_DATA);
	CHECK(sent.size() == 1 && sent[0].size() == 2050, "data block size");
	if (sent.size() == 1) for (int i = 0; i < 2048; i++) if (sent[0][i] != data_byte(16, i)) { CHECK(0, "data byte %d", i); break; }
	do_read(150 + 17, 1, P3DO_FMT_RAW);
	CHECK(sent.size() == 1 && sent[0].size() == 2354, "raw data block size");
	if (sent.size() == 1) CHECK(memcmp(sent[0].data(), &t1[17 * 2352], 2352) == 0, "raw data block content");
	do_read(150 + 18, 1, P3DO_FMT_RAW_2340);
	CHECK(sent.size() == 1 && sent[0].size() == 2342 && memcmp(sent[0].data(), &t1[18 * 2352 + 12], 2340) == 0, "2340-byte block");

	// a large block count (readahead) keeps the MSF address
	do_read(150 + 20, 1, P3DO_FMT_DATA);
	{
		uint8_t cmd[8] = { 0x10, 0, 2, 20, 0x05, 0x00, 0x00, 0 };	// 327680 blocks
		uint8_t tm;
		sent.clear();
		p3docdd.SetCommand(cmd); p3docdd.CommandExec(); p3docdd.Process(&tm); p3docdd.Update();
		CHECK(sent.size() == 1 && sent[0][0] == data_byte(20, 0), "big count: wrong address");
	}

	// ---------- 2. ISO: raw format builds sync + header ----------
	std::vector<uint8_t> iso(50 * 2048);
	for (int l = 0; l < 50; l++) for (int i = 0; i < 2048; i++) iso[l * 2048 + i] = data_byte(l, i);
	write_file("game.iso", iso);
	CHECK(p3docdd.Load("game.iso") > 0, "ISO load");
	p3docdd.SendData = capture;
	do_read(150 + 7, 1, P3DO_FMT_RAW);
	CHECK(sent.size() == 1, "ISO raw block");
	if (sent.size() == 1)
	{
		uint8_t *s = sent[0].data();
		CHECK(s[0] == 0 && s[1] == 0xFF && s[10] == 0xFF && s[11] == 0, "sync");
		CHECK(s[12] == 0x00 && s[13] == 0x02 && s[14] == 0x07 && s[15] == 1, "header %02x %02x %02x %02x", s[12], s[13], s[14], s[15]);
		CHECK(memcmp(s + 16, &iso[7 * 2048], 2048) == 0, "ISO user data");
	}
	do_read(150 + 8, 1, P3DO_FMT_DATA);
	CHECK(sent.size() == 1 && memcmp(sent[0].data(), &iso[8 * 2048], 2048) == 0, "ISO data block");

	// ---------- 3. one BIN with every track (data + audio); MOTOROLA audio (big-endian) swapped ----------
	std::vector<uint8_t> all(400 * 2352);
	for (int l = 0; l < 200; l++) make_raw_data(&all[l * 2352], l);
	for (int l = 200; l < 400; l++) for (int i = 0; i < 2352; i++) all[l * 2352 + i] = audio_byte(3000 + l, i);
	write_file("all.bin", all);
	c = fopen("all.cue", "w");
	fprintf(c, "FILE \"all.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
	           "  TRACK 02 AUDIO\n    INDEX 00 00:02:50\n    INDEX 01 00:02:60\n");
	fclose(c);
	CHECK(p3docdd.Load("all.cue") > 0, "single-BIN load");
	p3docdd.SendData = capture;
	CHECK(p3docdd.GetDiscInfo(info) > 0, "single-BIN disc info");
	t2_abs = info[16*2+5] * 4500 + info[16*2+6] * 75 + info[16*2+7];
	printf("single BIN: track 2 at %02d:%02d:%02d (expected 00:04:60)\n", info[16*2+5], info[16*2+6], info[16*2+7]);
	CHECK(t2_abs == 150 + 210, "single-BIN track 2 start");
	do_read(t2_abs, 1, P3DO_FMT_RAW);
	CHECK(sent.size() == 1 && sent[0][0] == audio_byte(3000 + 210, 0) && sent[0][1] == audio_byte(3000 + 210, 1), "single-BIN audio");

	c = fopen("motorola.cue", "w");
	fprintf(c, "FILE \"t3.bin\" MOTOROLA\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n");
	fclose(c);
	CHECK(p3docdd.Load("motorola.cue") > 0, "MOTOROLA load");
	p3docdd.SendData = capture;
	do_read(150 + 5, 1, P3DO_FMT_RAW);
	CHECK(sent.size() == 1 && sent[0][0] == audio_byte(2005, 1) && sent[0][1] == audio_byte(2005, 0), "MOTOROLA audio not swapped to disc order");


	// CUE FLAGS PRE: control bit 0x01 in the TOC and in the subcode
	c = fopen("pre.cue", "w");
	fprintf(c, "FILE \"t3.bin\" BINARY\n  TRACK 01 AUDIO\n    FLAGS PRE DCP\n    INDEX 01 00:00:00\n");
	fclose(c);
	CHECK(p3docdd.Load("pre.cue") > 0, "FLAGS load");
	p3docdd.SendData = capture;
	CHECK(p3docdd.GetDiscInfo(info) > 0, "FLAGS disc info");
	CHECK(info[16*1+2] == 0x13, "FLAGS: TOC control %02x", info[16*1+2]);
	do_read(150 + 3, 1, P3DO_FMT_RAW | 4);
	CHECK(sent.size() == 1 && (((sent[0][2352+1] >> 6) & 1) == 0) && (((sent[0][2352+2] >> 6) & 1) == 1) && (((sent[0][2352+3] >> 6) & 1) == 1) && (((sent[0][2352+7] >> 6) & 1) == 1), "FLAGS: subcode control bits");

	// ---------- 4. CD-DA at the speed of the disc ----------
	// One READ of 100 blocks as the core asks for them (NEXT after each block): 48 blocks at once
	// (read-ahead), then 75 per second of the clock. Another READ that does not continue it restarts it.
	CHECK(p3docdd.Load("disc.cue") > 0, "pacing: CUE load");
	p3docdd.SendData = capture;
	{
		int a = t2_first_abs;
		uint8_t cmd[8] = { 0x10, (uint8_t)(a / 75 / 60), (uint8_t)((a / 75) % 60), (uint8_t)(a % 75), 0, 0, 100, P3DO_FMT_RAW };
		uint8_t next[8] = { 0x20, 0, 0, 0, 0, 0, 0, 0 };
		uint8_t tm;
		sent.clear();
		fake_ms = 5000;
		p3docdd.SetCommand(cmd);
		p3docdd.CommandExec();
		size_t at_once = 0, at_600 = 0;
		for (int poll = 0; poll < 4000 && sent.size() < 100; poll++)
		{
			size_t before = sent.size();
			p3docdd.Process(&tm);
			p3docdd.Update();
			if (sent.size() > before) {
				if (sent.size() < 100) { p3docdd.SetCommand(next); p3docdd.CommandExec(); }
			}
			else {
				if (fake_ms == 5000) at_once = sent.size();
				if (fake_ms == 5600) at_600 = sent.size();
				fake_ms += 100;	// a poll that sent nothing: time goes on
			}
		}
		printf("pacing: %zu blocks at once, %zu after 600 ms, 100 after %lu ms\n", at_once, at_600, fake_ms - 5000);
		CHECK(at_once == 48, "pacing: %zu blocks sent before the clock moved (48 expected)", at_once);
		CHECK(at_600 == 48 + 45, "pacing: %zu blocks after 600 ms (93 expected)", at_600);
		CHECK(sent.size() == 100 && fake_ms - 5000 == 700, "pacing: 100 blocks after %lu ms (700 expected)", fake_ms - 5000);
		int bad = -1;
		for (size_t b = 0; b < sent.size() && bad < 0; b++)
			if (sent[b][0] != audio_byte(1000 + (int)b, 0) || sent[b][2351] != audio_byte(1000 + (int)b, 2351)) bad = (int)b;
		CHECK(bad < 0, "pacing: block %d out of order", bad);
	}
	// data blocks are not paced
	fake_ms = 0;
	do_read(150 + 20, 30, P3DO_FMT_DATA);
	CHECK(sent.size() == 30, "pacing: data blocks held back (%zu sent)", sent.size());

	printf(fails ? "MAIN TEST FAIL (%d)\n" : "MAIN TEST PASS\n", fails);
	return fails != 0;
}
