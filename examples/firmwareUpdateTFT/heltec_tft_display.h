#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace heltec_tft {
// These are the per-board macros from Heltec's Arduino boards.txt.
#if defined(WIFI_LORA_32_V4_R8)
constexpr int touchSda = 17, touchScl = 18;
constexpr int displayReset = 21, touchReset = displayReset;
constexpr int sdChipSelect = 3;
constexpr int sck = 16, mosi = 15, miso = 45;
constexpr int dc = 48, cs = 47, backlight = 44;
constexpr uint32_t writeHz = 75000000, readHz = 75000000;
constexpr bool spi3wire = false;
#elif defined(WIFI_LORA_32_V4)
constexpr int touchSda = 47, touchScl = 48;
constexpr int displayReset = 18, touchReset = 44;
constexpr int sdChipSelect = -1;
constexpr int sck = 17, mosi = 33, miso = -1;
constexpr int dc = 16, cs = 15, backlight = 21;
constexpr uint32_t writeHz = 40000000, readHz = 4000000;
constexpr bool spi3wire = true;
#else
#error "Select Heltec WiFi LoRa 32(V4) or Heltec WiFi LoRa 32(V4 R8) in Tools > Board."
#endif
constexpr bool sharedReset = touchReset == displayReset;

class Panel : public lgfx::Panel_ST7789
{
protected:
    const uint8_t *getInitCommands(uint8_t listno) const override
    {
        static constexpr uint8_t gamma[] = {CMD_GAMMASET, 1, 0x01, 0xFF, 0xFF};
        return listno == 1 ? gamma : Panel_ST7789::getInitCommands(listno);
    }
};

class Display : public lgfx::LGFX_Device
{
public:
    Display()
    {
        auto bus = bus_.config();
        bus.spi_host = SPI3_HOST;
        bus.spi_mode = 0;
        bus.freq_write = writeHz;
        bus.freq_read = readHz;
        bus.spi_3wire = spi3wire;
        bus.use_lock = true;
        bus.dma_channel = SPI_DMA_CH_AUTO;
        bus.pin_sclk = sck;
        bus.pin_mosi = mosi;
        bus.pin_miso = miso;
        bus.pin_dc = dc;
        bus_.config(bus);
        panel_.setBus(&bus_);

        auto panel = panel_.config();
        panel.pin_cs = cs;
        panel.pin_rst = sharedReset ? -1 : displayReset;
        panel.pin_busy = -1;
        panel.memory_width = panel.panel_width = 240;
        panel.memory_height = panel.panel_height = 320;
        panel.offset_x = panel.offset_y = 0;
        panel.offset_rotation = 0;
        panel.dummy_read_pixel = 9;
        panel.dummy_read_bits = 1;
        panel.readable = false;
        panel.invert = true;
        panel.rgb_order = false;
        panel.dlen_16bit = false;
        panel.bus_shared = false;
        panel_.config(panel);

        auto light = light_.config();
        light.pin_bl = backlight;
        light.invert = false;
        light.freq = 44100;
        light.pwm_channel = 7;
        light_.config(light);
        panel_.setLight(&light_);
        setPanel(&panel_);
    }

private:
    Panel panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;
};

} // namespace heltec_tft
