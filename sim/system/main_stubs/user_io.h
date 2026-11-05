// Main_MiSTer user_io.h, reduced to what support/3do uses (implemented in sim_main.cpp)
#pragma once
#include <stdint.h>

#define UIO_CD_GET 0x34
#define UIO_CD_SET 0x35

void user_io_set_index(unsigned char index);
void user_io_set_download(unsigned char enable, int size = 0);
void user_io_file_tx_data(const uint8_t *addr, uint32_t len);
int  user_io_file_tx(const char* name, unsigned char index = 0, char opensave = 0, char mute = 0, char composite = 0, uint32_t load_addr = 0);
int  user_io_file_mount(const char *name, unsigned char index = 0, char pre = 0, int pre_size = 0);
void user_io_status_set(const char *opt, uint32_t value, int ex = 0);
