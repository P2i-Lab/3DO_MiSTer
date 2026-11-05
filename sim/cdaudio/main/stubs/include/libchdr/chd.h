#pragma once
typedef struct chd_file chd_file;
typedef int chd_error;
#define CHDERR_NONE 0
const char *chd_error_string(chd_error err);
void chd_close(chd_file *chd);
