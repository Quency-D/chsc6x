#ifndef __CHSC6X_MAIN_H__
#define __CHSC6X_MAIN_H__
#include <stdint.h>
#include "chsc6x_platform.h"

typedef struct sm_touch_dev
{    
    int int_pin;
    int rst_pin;
}sm_touch_dev, *psm_touch_dev;

struct ts_event {
     uint16_t x; /*x coordinate */
     uint16_t y; /*y coordinate */
    int flag; /* touch event flag: 0 -- down; 4-- up; 8 -- contact */
    int id;   /*touch ID */
};



struct chsc6x_cfg {
    uint8_t bytes[204];
    uint8_t version() const { return bytes[3] >> 2; }
};

enum chsc6x_cfg_error {
    CHSC6X_CFG_ENTER_FAILED = -1,
    CHSC6X_CFG_READ_FAILED = -2,
    CHSC6X_CFG_INVALID = -3,
    CHSC6X_CFG_RESTORE_FAILED = -4
};

struct chsc6x_versions {
    uint8_t cfg;
    uint16_t boot;
};

enum class chsc6x_init_status {
    not_initialized, current, updated, incompatible, power_cycle_required, error
};

struct chsc6x_init_result {
    chsc6x_init_status status = chsc6x_init_status::not_initialized;
    const char *stage = "idle";
    chsc6x_versions before{};
    chsc6x_versions after{};
    bool before_valid = false;
    bool after_valid = false;
    // Set only once the corresponding nonvolatile data has been read back.
    bool cfg_written = false;
    bool boot_written = false;
    bool needs_power_cycle = false;
    bool touch_ready = false;
};

class chsc6x :public chsc6x_platform{
private:
    chsc6x_init_result init_result;
    enum class probe_state { running, burn_helper, unsupported, unreadable };
    probe_state read_running_versions(chsc6x_versions &versions, uint32_t &identity);
    bool prepare_controller(bool hasReset, const chsc6x_init_result &previous);
    void update_if_needed(uint32_t identity, bool hasReset);
    void restore_touch_mode(bool usable);
    int get_cfg_direct_mode(bool &direct);
    int set_cfg_direct_mode(bool direct);
    int read_running_cfg(chsc6x_cfg &cfg);
    uint8_t real_suspend_flag = 1;
#if CHSC6X_GESTURE
    uint8_t g_gesture_enable = 1; 
    uint8_t g_gesture_status = 0;
#endif

public:
    chsc6x(TwoWire *wire = &Wire1, int sda_pin = -1, int scl_pin = -1, int int_pin = -1, int rst_pin = -1)
        : chsc6x_platform(wire, sda_pin, scl_pin, int_pin, rst_pin)
    {
    };
    ~chsc6x(){};
    void chsc6x_init(void);
    const chsc6x_init_result &last_init_result() const { return init_result; }
    // On OS_OK, cfg contains a validated snapshot and touch reporting is restored.
    int chsc6x_read_cfg(chsc6x_cfg &cfg);
    void chsc6x_suspend(void);
    void chsc6x_resume(void);
    int chsc6x_read_touch_info(uint16_t *touchX, uint16_t *touchY);

#if CHSC6X_GESTURE
    void chsc6x_gesture_check(void);
    uint8_t chsc6x_get_gesture(void);
#endif

};




#endif


