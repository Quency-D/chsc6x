#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// R8 TFT wiring from firmware/variants/esp32s3/heltec_v4_r8.
class V4R8Panel : public lgfx::Panel_ST7789
{
protected:
    const uint8_t *getInitCommands(uint8_t listno) const override
    {
        static constexpr uint8_t gamma[] = {CMD_GAMMASET, 1, 0x01, 0xFF, 0xFF};
        return listno == 1 ? gamma : Panel_ST7789::getInitCommands(listno);
    }
};

class V4R8Display : public lgfx::LGFX_Device
{
public:
    static constexpr int SHARED_RESET_PIN = 21;

    V4R8Display()
    {
        auto bus = bus_.config();
        bus.spi_host = SPI3_HOST;
        bus.spi_mode = 0;
        bus.freq_write = 75000000;
        bus.freq_read = 75000000;
        bus.spi_3wire = false;
        bus.use_lock = true;
        bus.dma_channel = SPI_DMA_CH_AUTO;
        bus.pin_sclk = 16;
        bus.pin_mosi = 15;
        bus.pin_miso = 45;
        bus.pin_dc = 48;
        bus_.config(bus);
        panel_.setBus(&bus_);

        auto panel = panel_.config();
        panel.pin_cs = 47;
        panel.pin_rst = -1; // The updater alone drives the shared GPIO21 reset.
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
        light.pin_bl = 44;
        light.invert = false;
        light.freq = 44100;
        light.pwm_channel = 7;
        light_.config(light);
        panel_.setLight(&light_);
        setPanel(&panel_);
    }

private:
    V4R8Panel panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;
};
