// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unistd.h>
#include <string.h>
#include <iconv.h>
#include <wchar.h>

#include "utils.h"

#define IO_BLK_SIZE		(1<<14)

__attribute__((visibility("default"))) int utf8_to_wchar_t(char* input, wchar_t* output, size_t output_bytes)
{
	char *pinput = input;
	char *poutput = (char *)output;
	size_t inbytes = strlen(input), outbytes = output_bytes;

	iconv_t icd = iconv_open("WCHAR_T", "UTF-8");
	size_t chars = iconv(icd, &pinput, &inbytes, &poutput, &outbytes);
	iconv_close(icd);

	if (chars == -1)
		return 1;

	wchar_t *p = (wchar_t*)poutput;
	*p = 0;
	return 0;
}

__attribute__((visibility("default"))) int wchar_t_to_utf8(const wchar_t* input, char* output, size_t output_bytes)
{
	char *pinput = (char *)input;
	char *poutput = output;
	size_t inbytes = wcslen(input)*sizeof(wchar_t), outbytes = output_bytes;

	iconv_t icd = iconv_open("UTF-8", "WCHAR_T");
	size_t chars = iconv(icd, &pinput, &inbytes, &poutput, &outbytes);
	iconv_close(icd);

	if (chars == -1)
		return 1;

	*poutput = 0;
	return 0;
}

int read_file(int fd, char* head, uint32_t size)
{
	uint32_t left = size;
	char* p = head;
	while (left > 0) {
		uint32_t to_read = left > IO_BLK_SIZE ? IO_BLK_SIZE : left;
		if (read(fd, p, to_read) != to_read)
			return 1;
		p += to_read;
		left -= to_read;
	}
	return 0;
}

int write_file(int fd, char* head, uint32_t size)
{
	uint32_t left = size;
	char* p = head;
	while (left > 0) {
		uint32_t to_write = left > IO_BLK_SIZE ? IO_BLK_SIZE : left;
		if (write(fd, p, to_write) != to_write)
			return 1;
		p += to_write;
		left -= to_write;
	}
	return 0;
}
