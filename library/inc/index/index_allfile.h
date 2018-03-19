#pragma once

#include <stdint.h>

#include "index.h"

int load_allfile_index(fs_index** pfsi, int fd, uint32_t count);

