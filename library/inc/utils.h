#pragma once

#include <stdint.h>
#include <wchar.h>

#ifdef IDX_DEBUG

#include <stdio.h>

#define dbg_msg(fmt, ...) do {\
	fprintf(stderr, "[idx] "fmt, ##__VA_ARGS__);\
} while(0)

#else

#define dbg_msg(fmt, ...) do{} while(0)

#endif

int utf8_to_wchar_t(char* input, wchar_t* output, size_t output_bytes);
int wchar_t_to_utf8(const wchar_t* input, char* output, size_t output_bytes);

int read_file(int fd, char* head, uint32_t size);
int write_file(int fd, char* head, uint32_t size);
