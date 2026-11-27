// Select Heltec WiFi LoRa 32(V4) or Heltec WiFi LoRa 32(V4 R8) to match the board with a TFT.
// Requires LovyanGFX.
// A differing component is replaced even when the controller has a newer version.
#include <chsc6x_update.h>
#include <firmware/panel_packages.h>
#include "heltec_tft_display.h"

chsc6x controller(&Wire1, heltec_tft::touchSda, heltec_tft::touchScl, -1,
                  heltec_tft::touchReset, heltec_tft::sharedReset);
heltec_tft::Display tft;
static bool displayReady = false;
// Rows are cfg and boot. Columns are compare, write and verify.
static int percentages[2][3] = {{-1, -1, -1}, {-1, -1, -1}};

static void drawPercentage(int part, int phase)
{
    const int x = 108 + 66 * part;
    const int y = 64 + 24 * phase;
    char text[16] = "--";
    if (percentages[part][phase] >= 0) {
        snprintf(text, sizeof(text), "%d%%", percentages[part][phase]);
    }
    tft.fillRect(x, y, 48, 16, TFT_BLACK);
    tft.drawString(text, x, y);
}

static bool openDisplay()
{
    // On V4, pulse the dedicated TFT reset on first use. On R8 the updater owns GPIO21.
    if (!displayReady && !heltec_tft::sharedReset) {
        displayReady = tft.init();
    } else {
        displayReady = tft.init_without_reset(true);
    }
    if (!displayReady) {
        return false;
    }
    tft.setRotation(0);
    tft.setBrightness(130);
    tft.setTextFont(1);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("CHSC6X update", 8, 8);
    tft.drawString("cfg", 108, 40);
    tft.drawString("boot", 174, 40);
    const char *phases[] = {"Compare", "Write", "Verify"};
    for (int phase = 0; phase < 3; ++phase) {
        tft.drawString(phases[phase], 8, 64 + 24 * phase);
        drawPercentage(0, phase);
        drawPercentage(1, phase);
    }
    return true;
}

static bool beginUpdate()
{
    if (!openDisplay()) {
        Serial.print("TFT initialization failed. Stopping before flash writes.\r\n");
        return false;
    }
    tft.drawString("Keep power on", 8, 152);
    return true;
}

static void showProgress(chsc6x_update_part part, chsc6x_update_phase phase,
                         uint32_t done, uint32_t total)
{
    if (!total) {
        return;
    }
    const int row = static_cast<int>(part);
    const int column = static_cast<int>(phase);
    const int percent = static_cast<int>(done * 100u / total);
    if (percent != percentages[row][column]) {
        percentages[row][column] = percent;
        drawPercentage(row, column);
    }
}

static const char *resultName(chsc6x_init_status status)
{
    switch (status) {
    case chsc6x_init_status::current:              return "Already current";
    case chsc6x_init_status::updated:              return "Update verified";
    case chsc6x_init_status::power_cycle_required: return "Power cycle needed";
    case chsc6x_init_status::incompatible:         return "Incompatible panel";
    default:                                       return "Update failed";
    }
}

static const char *comparisonName(chsc6x_compare_status status)
{
    switch (status) {
    case chsc6x_compare_status::identical:          return "identical";
    case chsc6x_compare_status::different:          return "different";
    case chsc6x_compare_status::not_applicable:     return "not applicable";
    case chsc6x_compare_status::read_error:         return "read failed";
    case chsc6x_compare_status::inconsistent_reads: return "unreliable reads";
    default:                                        return "not checked";
    }
}

static void resultLine(int y, const char *text, int size)
{
    Serial.printf("%s\r\n", text);
    if (displayReady) {
        tft.setTextSize(size);
        tft.drawString(text, 8, y);
    }
}

static void showIdentity(int y, const char *label, uint32_t identity)
{
    char line[40];
    snprintf(line, sizeof(line), "%s 0x%08lX", label, static_cast<unsigned long>(identity));
    resultLine(y, line, 1);
}

static void showPart(int y, const char *name, const chsc6x_part_result &part, bool written)
{
    const char *action = "skipped";
    if (part.write_attempted) {
        action = written ? "verified" : "unverified";
    }
    char line[56];
    snprintf(line, sizeof(line), "%s: %s, %s", name, comparisonName(part.comparison), action);
    resultLine(y, line, 1);
}

static void showVersions(int y, const char *label, const chsc6x_versions &v, bool valid)
{
    char line[28];
    if (valid) {
        snprintf(line, sizeof(line), "%s V%u 0x%04X", label, unsigned(v.cfg), unsigned(v.boot));
    } else {
        snprintf(line, sizeof(line), "%s --", label);
    }
    resultLine(y, line, 2);
}

void setup()
{
    Serial.begin(115200);
    if (heltec_tft::sdChipSelect >= 0) {
        pinMode(heltec_tft::sdChipSelect, OUTPUT);
        digitalWrite(heltec_tft::sdChipSelect, HIGH);
    }
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW); // Active low on both variants.
    delay(1000); // Allow the panel supply to settle before probing the controller.

    const chsc6x_update_observer observer = {beginUpdate, showProgress};
    const auto result = chsc6x_updater::run_auto(controller, chsc6x_firmware::packages,
                                                 chsc6x_firmware::count, observer);
    // On R8 the final touch reset also resets the TFT. Repaint the result on either board.
    if (!openDisplay()) {
        Serial.print("TFT result screen unavailable.\r\n");
    }
    const auto &init = result.init;
    char line[48] = "VID/PID --  Pkg --";
    if (result.vendor_id >= 0) {
        snprintf(line, sizeof(line), "VID/PID %d/%d  Pkg %s", result.vendor_id, result.project_id,
                 result.package_name ? result.package_name : "--");
    }
    resultLine(132, line, 1);
    if (result.cfg.comparison == chsc6x_compare_status::not_applicable) {
        showIdentity(142, "Panel ID", result.panel_identity);
        showIdentity(152, "Package cfg ID", result.package_cfg_identity);
        showIdentity(162, "Package boot match", result.package_boot_match);
        resultLine(172, result.boot_only() ? "Cfg ID mismatch: boot only"
                                           : "Neither cfg nor boot matches. Skipped.", 1);
    } else if (result.vendor_id >= 0 && result.target.boot == 0) {
        showIdentity(142, "Panel ID", result.panel_identity);
        resultLine(152, "Package cfg ID --", 1);
        resultLine(162, "Package boot match --", 1);
    }
    if (result.running_data_invalid) {
        resultLine(183, "Invalid running data. Update skipped.", 1);
    } else {
        resultLine(183, resultName(init.status), 2);
    }
    showPart(201, "cfg", result.cfg, init.cfg_written);
    showPart(211, "boot", result.boot, init.boot_written);
    resultLine(221, init.stage, 1);
    const auto &failed = result.cfg.failure_step ? result.cfg : result.boot;
    if (failed.failure_step) {
        snprintf(line, sizeof(line), "FAIL %s: %s +0x%04X", result.cfg.failure_step ? "cfg" : "boot",
                 failed.failure_step, unsigned(failed.failure_offset));
        resultLine(231, line, 1);
    }
    showVersions(241, "Before", init.before, init.before_valid);
    showVersions(259, "Package", result.target, result.target.boot != 0);
    showVersions(277, "After", init.after, init.after_valid);
    if (result.cfg_reads_inconsistent || result.boot_reads_inconsistent) {
        resultLine(295, "Unreliable controller reads", 1);
    }
    if (init.needs_power_cycle) {
        resultLine(306, "Power-cycle the touch controller", 1);
    }
}

void loop()
{
    delay(1000);
}
