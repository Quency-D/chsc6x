#pragma once

#include "chsc6x.h"

// The legacy forced value is reserved; the updater only writes confirmed differences.
enum class chsc6x_compare_status {
    not_checked, not_applicable, identical, different, forced, read_error, inconsistent_reads
};

struct chsc6x_part_result {
    chsc6x_compare_status comparison = chsc6x_compare_status::not_checked;
    // Offset within this component, valid only for a complete comparison.
    int32_t first_difference = -1;
    bool write_attempted = false;
    const char *failure_step = nullptr;
    // Valid when failure_step is set. Component-relative block start for I/O
    // failures, exact byte for a mismatch or for the boot signature write.
    uint16_t failure_offset = 0;
};

struct chsc6x_update_package {
    const uint8_t *data;
    size_t length;
    const char *name; // Display label. If provided, it must outlive the returned result.
};

struct chsc6x_update_result {
    chsc6x_init_result init;
    chsc6x_versions target{};
    chsc6x_part_result cfg;
    chsc6x_part_result boot;
    // Retained for existing callers; forced recovery is disabled.
    const char *force_reason = nullptr;
    bool assumed_panel = false;
    bool cfg_reads_inconsistent = false;
    bool boot_reads_inconsistent = false;
    bool running_data_invalid = false;
    int16_t vendor_id = -1;
    int16_t project_id = -1;
    const char *package_name = nullptr;
    uint32_t panel_identity = 0;       // Low 26 bits of the validated running cfg.
    uint32_t package_cfg_identity = 0; // Package values from the image used in select_parts.
    uint32_t package_boot_match = 0;

    bool boot_only() const {
        return cfg.comparison == chsc6x_compare_status::not_applicable &&
               boot.comparison != chsc6x_compare_status::not_applicable;
    }
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
    // Requires valid running firmware and cfg. A compatible package may replace newer firmware.
    static chsc6x_update_result run(chsc6x &touch, const uint8_t *package, size_t length);
    static chsc6x_update_result run(chsc6x &touch, const uint8_t *package, size_t length,
                                    const chsc6x_update_observer &observer);
    // Select a unique package by the vendor/project IDs in the validated running cfg.
    static chsc6x_update_result run_auto(chsc6x &touch, const chsc6x_update_package *packages, size_t count,
                                         const chsc6x_update_observer &observer = {});

private:
    static chsc6x_init_result maintain(chsc6x &touch, const chsc6x_update_package *packages, size_t count,
                                       bool auto_select, chsc6x_update_result &report,
                                       const chsc6x_update_observer &observer);
};
