// Main_MiSTer file_io.h, reduced to what support/3do uses (implemented in main_stubs.cpp)
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>

struct fileTYPE
{
	FILE *fp;
	__off64_t size;
	__off64_t offset;
	int opened() { return fp != 0; }
};

int FileOpen(fileTYPE *file, const char *name, char mute = 0);
int FileClose(fileTYPE *file);
int FileSeek(fileTYPE *file, __off64_t offset, int origin);
int FileReadAdv(fileTYPE *file, void *pBuffer, int length, int failres = 0);
int FileLoad(const char *name, void *pBuffer, int size);
const char *getFullPath(const char *name);
void FileGenerateSavePath(const char *name, char* out_name, int ext_replace = 1);
const char *HomeDir(const char *name = 0);
