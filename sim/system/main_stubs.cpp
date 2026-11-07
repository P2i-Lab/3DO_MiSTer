// File access of Main_MiSTer for support/3do, on the files of the PC (sim_main.cpp sets the
// folders). CHD images are not read: convert them with "chdman extractcd".
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "file_io.h"
#include "cd.h"
#include "support/chd/mister_chd.h"

extern const char *sim_home_dir;   // folder of boot.rom (Main_MiSTer: games/3DO)
extern const char *sim_save_dir;   // folder of the .sav files (Main_MiSTer: saves/3DO)

int FileOpen(fileTYPE *file, const char *name, char)
{
	file->fp = fopen(name, "rb");
	file->size = 0;
	file->offset = 0;
	if (!file->fp) return 0;
	fseeko(file->fp, 0, SEEK_END);
	file->size = ftello(file->fp);
	fseeko(file->fp, 0, SEEK_SET);
	return 1;
}

int FileClose(fileTYPE *file)
{
	if (file->fp) fclose(file->fp);
	file->fp = 0;
	return 1;
}

int FileSeek(fileTYPE *file, __off64_t offset, int origin)
{
	if (!file->fp || fseeko(file->fp, offset, origin)) return 0;
	file->offset = ftello(file->fp);
	return 1;
}

int FileReadAdv(fileTYPE *file, void *buf, int len, int)
{
	if (!file->fp) return 0;
	int n = (int)fread(buf, 1, len, file->fp);
	file->offset += n;
	return n;
}

int FileLoad(const char *name, void *buf, int size)
{
	FILE *f = fopen(name, "rb");
	if (!f) return 0;
	if (!size) { fseek(f, 0, SEEK_END); size = (int)ftell(f); fseek(f, 0, SEEK_SET); }
	int n = (int)fread(buf, 1, size, f);
	fclose(f);
	return n;
}

int FileSave(const char *name, void *buf, int size)
{
	FILE *f = fopen(name, "wb");
	if (!f) return 0;
	int n = (int)fwrite(buf, 1, size, f);
	fclose(f);
	return n == size;
}

const char *getFullPath(const char *name) { return name; }

// saves/3DO/<image name>.sav
void FileGenerateSavePath(const char *name, char *out_name, int)
{
	const char *base = strrchr(name, '/');
	base = base ? base + 1 : name;
	sprintf(out_name, "%s/%s", sim_save_dir, base);
	char *dot = strrchr(out_name, '.');
	char *slash = strrchr(out_name, '/');
	if (dot && dot > slash) *dot = 0;
	strcat(out_name, ".sav");
}

const char *HomeDir(const char *) { return sim_home_dir; }

const char *chd_error_string(chd_error) { return "CHD images are not supported by the simulation"; }
void chd_close(chd_file *) {}
chd_error mister_load_chd(const char *, toc_t *) { return 1; }
chd_error mister_chd_read_sector(chd_file *, int, uint32_t, uint32_t, int, uint8_t *, uint8_t *, int *) { return 1; }
