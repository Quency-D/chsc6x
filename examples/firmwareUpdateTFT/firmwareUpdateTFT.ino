// Select Heltec WiFi LoRa 32(V4) or Heltec WiFi LoRa 32(V4 R8) to match the board with a TFT.
// Requires LovyanGFX.
// A differing component is replaced even when the controller has a newer version.
#include <chsc6x_update.h>
#include "heltec_tft_display.h"

#if defined(WIFI_LORA_32_V4) && !defined(WIFI_LORA_32_V4_R8)
#include <firmware/firmware_01_01_V224_V4.h>
static constexpr auto &panelFirmware = fw_file_tlsc6x_01_01;
#else
#include <firmware/firmware_05_12_V224_V4.h>
static constexpr auto &panelFirmware = fw_file_tlsc6x_05_12;
#endif

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
    const auto result = chsc6x_updater::run(controller, panelFirmware, sizeof(panelFirmware), observer);
    // On R8 the final touch reset also resets the TFT. Repaint the result on either board.
    if (!openDisplay()) {
        Serial.print("TFT result screen unavailable.\r\n");
    }
    const auto &init = result.init;
    if (result.running_data_invalid) {
        resultLine(148, "Invalid running data. Update skipped.", 1);
    } else {
        resultLine(148, resultName(init.status), 2);
    }
    showPart(176, "cfg", result.cfg, init.cfg_written);
    showPart(190, "boot", result.boot, init.boot_written);
    resultLine(204, init.stage, 1);
    showVersions(222, "Before", init.before, init.before_valid);
    showVersions(244, "Package", result.target, result.target.boot != 0);
    showVersions(266, "After", init.after, init.after_valid);
    if (result.cfg_reads_inconsistent || result.boot_reads_inconsistent) {
        resultLine(292, "Unreliable controller reads", 1);
    }
    if (init.needs_power_cycle) {
        resultLine(306, "Power-cycle the touch controller", 1);
    }
}

void loop()
{
    delay(1000);
}
