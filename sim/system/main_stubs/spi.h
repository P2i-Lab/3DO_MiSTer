// Main_MiSTer spi.h, reduced to what support/3do uses (implemented in sim_main.cpp)
#pragma once
#include <stdint.h>

void EnableIO();
void DisableIO();
uint16_t spi_w(uint16_t word);
uint16_t spi_uio_cmd_cont(uint16_t cmd);
