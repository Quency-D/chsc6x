#pragma once

#include "chsc6x.h"

enum class chsc6x_compare_status {
    not_checked, not_applicable, identical, different, forced, read_error, inconsistent_reads
};

struct chsc6x_part_result {
    chsc6x_compare_status comparison = chsc6x_compare_status::not_checked;
    // Offset within this component, valid only for a complete comparison.
    int32_t first_difference = -1;
    bool write_attempted = false;
};

struct chsc6x_update_result {
    chsc6x_init_result init;
    chsc6x_versions target{};
    chsc6x_part_result cfg;
    chsc6x_part_result boot;
    const char *force_reason = nullptr;
    bool assumed_panel = false;
    bool cfg_reads_inconsistent = false;
    bool boot_reads_inconsistent = false;
};

enum class chsc6x_update_part { cfg, boot };
enum class chsc6x_update_phase { compare, write, verify };

struct chsc6x_update_observer {
    // Called after the initial controller probe/reset and before starting the flash helper.
    // Return false to stop maintenance. Both hooks are optional and run synchronously.
    bool (*begin)();
    // Completed and total bytes within this component and phase. Boot totals include
    // the repeated signature write and the final full-image readback, respectively.
    void (*progress)(chsc6x_update_part part, chsc6x_update_phase phase, uint32_t done, uint32_t total);
};

class chsc6x_updater {
public:
    // Explicit maintenance operation. A compatible package may replace newer firmware.
    static chsc6x_update_result run(chsc6x &touch, const uint8_t *package, size_t length);
    static chsc6x_update_result run(chsc6x &touch, const uint8_t *package, size_t length,
                                    const chsc6x_update_observer &observer);

private:
    static chsc6x_init_result maintain(chsc6x &touch, const uint8_t *package, size_t length,
                                      chsc6x_update_result &report, const chsc6x_update_observer &observer);
};
