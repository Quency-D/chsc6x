#pragma once

#include "../chsc6x_update.h"
#include "firmware_01_01_V224_V4.h"
#include "firmware_05_12_V224_V4.h"

namespace chsc6x_firmware {
static constexpr chsc6x_update_package packages[] = {
    {fw_file_tlsc6x_01_01, sizeof(fw_file_tlsc6x_01_01), "[01_01]"},
    {fw_file_tlsc6x_05_12, sizeof(fw_file_tlsc6x_05_12), "[05_12]"},
};
constexpr size_t count = sizeof(packages) / sizeof(packages[0]);
} // namespace chsc6x_firmware
