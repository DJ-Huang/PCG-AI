#pragma once

#include "pcg_api.h"

#include <cstring>

namespace pcg::internal {

inline void write_error(char* err_buf, int err_buf_size, const char* message)
{
    if (!err_buf || err_buf_size <= 0)
        return;

    std::strncpy(err_buf, message, static_cast<size_t>(err_buf_size - 1));
    err_buf[err_buf_size - 1] = '\0';
}

} // namespace pcg::internal
