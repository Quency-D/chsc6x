#include <chsc6x_update.h>
#include "firmware_05_12_V224_V4.h"

// Heltec V4R8 with TouchTFT V0.2. Change these settings for another board.
constexpr int touchSda = 17;
constexpr int touchScl = 18;
constexpr int touchInt = -1;
constexpr int touchReset = 21;
constexpr int panelPower = 40;  // active low; use -1 for an always-powered panel
constexpr bool resetSharedWithTft = true;
chsc6x touch(&Wire1, touchSda, touchScl, touchInt, touchReset,
             resetSharedWithTft);
static const char *touchReminder = nullptr;

static const char *comparisonName(chsc6x_compare_status value)
{
    switch (value) {
    case chsc6x_compare_status::not_checked:    return "not checked";
    case chsc6x_compare_status::not_applicable: return "not applicable";
    case chsc6x_compare_status::identical:      return "identical";
    case chsc6x_compare_status::different:      return "different";
    case chsc6x_compare_status::forced:         return "forced";
    case chsc6x_compare_status::read_error:     return "flash read failed. Update skipped.";
    case chsc6x_compare_status::inconsistent_reads:
        return "unreliable flash reads: two reads of the same block differ. Update skipped.";
    }
    return "unknown";
}

static const char *statusName(chsc6x_init_status status)
{
    switch (status) {
    case chsc6x_init_status::current:              return "current";
    case chsc6x_init_status::updated:              return "updated";
    case chsc6x_init_status::incompatible:         return "incompatible";
    case chsc6x_init_status::power_cycle_required: return "power cycle required";
    default:                                       return "error";
    }
}

static void printPart(const char *name, const chsc6x_part_result &part, bool written)
{
    Serial.printf("%s: %s", name, comparisonName(part.comparison));
    if (part.first_difference >= 0) {
        Serial.printf(", first difference at +0x%04lX", (unsigned long)part.first_difference);
    }
    if (part.write_attempted) {
        Serial.printf(", write/readback %s", written ? "verified" : "failed");
    }
    Serial.print("\r\n");
}

static void printVersions(const char *label, const chsc6x_versions &versions, bool valid)
{
    if (valid) {
        Serial.printf("%s: cfg V%u, boot 0x%04X\r\n", label, unsigned(versions.cfg), unsigned(versions.boot));
    } else {
        Serial.printf("%s: unavailable\r\n", label);
    }
}

void setup()
{
    Serial.begin(115200);
    if (panelPower >= 0) {
        pinMode(panelPower, OUTPUT);
        digitalWrite(panelPower, LOW);
        delay(100);
    }

    Serial.print("CHSC6X update may install an older firmware version.\r\n");
    const auto result = chsc6x_updater::run(touch, fw_file_tlsc6x_05_12, sizeof(fw_file_tlsc6x_05_12));
    const auto &init = result.init;
    printVersions("Old (controller before maintenance)", init.before, init.before_valid);
    printVersions("New (package target)", result.target, result.target.boot != 0);
    if (result.force_reason) {
        Serial.printf("Recovery reason: %s\r\n", result.force_reason);
    }
    if (result.assumed_panel) {
        Serial.print("Factory cfg invalid; using the target panel selected by this sketch.\r\n");
    }
    printPart("cfg", result.cfg, init.cfg_written);
    printPart("boot", result.boot, init.boot_written);
    Serial.printf("Result: %s, stage=%s, touch_ready=%u\r\n", statusName(init.status), init.stage, unsigned(init.touch_ready));
    if (result.cfg_reads_inconsistent) {
        Serial.print("Unreliable cfg reads: repeated snapshots differ. Update skipped.\r\n");
    }
    if (result.boot_reads_inconsistent) {
        Serial.print("Unreliable boot header reads: repeated 12-byte snapshots differ. Update skipped.\r\n");
    }
    if (init.needs_power_cycle) {
        Serial.print("After (controller readback): unavailable until the touch controller is power-cycled.\r\n");
    } else {
        printVersions("After (controller readback)", init.after, init.after_valid);
    }
    if (init.needs_power_cycle) {
        touchReminder = "Power-cycle the touch controller to restore touch. If the whole board shares "
                        "its supply, flash the normal application before cycling board power.\r\n";
    } else if (!init.touch_ready) {
        touchReminder = "Touch reporting is unconfirmed. Skip coordinate reads until normal "
                        "initialization reports touch_ready=1.\r\n";
    }
    if (touchReminder) {
        Serial.print(touchReminder);
    }
}

void loop()
{
    delay(1000);
    if (touchReminder) {
        Serial.print(touchReminder);
    }
}
