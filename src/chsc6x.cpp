#include "chsc6x.h"
#include "detail/chsc6x_bytes.h"
#include <string.h>

namespace {
constexpr uint32_t chip3536 = 0x35368008;
constexpr uint32_t runningTag = 0x544C4E4B;
constexpr uint32_t burnHelper = 0x6D6F8008;
constexpr uint32_t cfgIdentityMask = 0x03FFFFFF;
} // namespace

bool chsc6x::valid_cfg(const chsc6x_cfg &cfg, uint8_t addr)
{
    return cfg.bytes[106] == (addr << 1) && chsc6x_bytes::sum16(cfg.bytes, sizeof(cfg.bytes)) == 0;
}

int chsc6x::read_running_cfg(chsc6x_cfg &cfg)
{
    for (size_t offset = 0; offset < sizeof(cfg.bytes); offset += MAX_IIC_RD_LEN) {
        const size_t remaining = sizeof(cfg.bytes) - offset;
        const size_t count = remaining < MAX_IIC_RD_LEN ? remaining : MAX_IIC_RD_LEN;
        if (chsc6x_read_bytes_u16addr(_i2c_addr, 0x9E00 + offset, cfg.bytes + offset, count) != OS_OK) {
            return CHSC6X_CFG_READ_FAILED;
        }
    }
    return valid_cfg(cfg, _i2c_addr) ? OS_OK : CHSC6X_CFG_INVALID;
}

chsc6x::probe_state chsc6x::read_running_versions(chsc6x_versions &versions, uint32_t &identity,
                                                     maintenance_probe *maintenance)
{
    bool haveBootHeader = false;
    unsigned badCfgReads = 0;
    probe_state state = probe_state::unreadable;
    bool transportFailed = false;
    for (int attempt = 0; attempt < TP_RETRY_CNT2; ++attempt) {
        if (attempt > 0) {
            chsc6x_msleep(20);
        }
        uint8_t header[12];
        if (chsc6x_read_bytes_u16addr(_i2c_addr, 0x8000, header, sizeof(header)) != OS_OK) {
            transportFailed = true;
            continue;
        }
        const uint32_t family = chsc6x_bytes::le32(header);
        if (maintenance) {
            if (!haveBootHeader) {
                memcpy(maintenance->first_boot_header, header, sizeof(header));
                haveBootHeader = true;
            } else if (memcmp(maintenance->first_boot_header, header, sizeof(header)) != 0) {
                maintenance->boot_reads_inconsistent = true;
                // A live RAM helper still requires a power cycle.
                return family == burnHelper ? probe_state::burn_helper : probe_state::unreadable;
            }
        }
        const uint32_t tag = chsc6x_bytes::le32(header + 8);
        if (family == burnHelper) {
            return probe_state::burn_helper;
        }
        // The vendor recognizes a partial TLNK signature with an invalid low byte.
        if ((tag & 0xFFFFFF00) != (runningTag & 0xFFFFFF00)) {
            state = probe_state::unknown_header;
            continue;
        }
        if (family != chip3536) {
            return probe_state::unsupported;
        }
        const uint16_t boot = chsc6x_bytes::le16(header + 4);
        if (tag != runningTag || boot == 0 || boot == 0xFFFF) {
            state = probe_state::boot_invalid;
            continue;
        }
        versions.boot = boot;
        chsc6x_cfg cfg;
        const int status = read_running_cfg(cfg);
        if (status != OS_OK && status != CHSC6X_CFG_INVALID) {
            transportFailed = true;
            state = probe_state::cfg_unreadable;
            continue;
        }
        if (badCfgReads && memcmp(maintenance->first_bad_cfg.bytes, cfg.bytes, sizeof(cfg.bytes)) != 0) {
            return probe_state::cfg_inconsistent;
        }
        if (status == OS_OK) {
            versions.cfg = cfg.version();
            identity = chsc6x_bytes::le32(cfg.bytes) & cfgIdentityMask;
            return probe_state::running;
        }
        if (maintenance) {
            if (badCfgReads == 0) {
                maintenance->first_bad_cfg = cfg;
            }
            ++badCfgReads;
        }
        state = probe_state::cfg_invalid;
    }

    // Recovery requires a complete, matching snapshot on every probe attempt.
    if (transportFailed || (badCfgReads != 0 && badCfgReads != TP_RETRY_CNT2)) {
        return state == probe_state::cfg_invalid || state == probe_state::cfg_unreadable
            ? probe_state::cfg_unreadable : probe_state::unreadable;
    }
    return state;
}

void chsc6x::require_power_cycle(const char *stage)
{
    init_result.status = chsc6x_init_status::power_cycle_required;
    init_result.stage = stage;
    init_result.needs_power_cycle = true;
    init_result.touch_ready = false;
    chsc6x_info("CHSC6X: power-cycle the touch controller\r\n");
}

void chsc6x::restore_touch_mode(bool usable)
{
    if (set_cfg_direct_mode(false) != OS_OK) {
        init_result.status = chsc6x_init_status::error;
        init_result.stage = "restore_mapping";
        init_result.touch_ready = false;
        return;
    }
    init_result.touch_ready = usable;
}

bool chsc6x::prepare_controller(bool hasReset, bool wasWaiting, bool &readFault)
{
    semi_touch_get_int();
    if (semi_touch_i2c_init() != OS_OK) {
        if (!hasReset && wasWaiting) {
            require_power_cycle("await_power_cycle");
        }
        return false;
    }
    init_result.stage = "probe";
    chsc6x_msleep(60);
    if (hasReset) {
        chsc6x_tp_reset(HW_CMD_RESET);
        return true;
    }

    // A stopped MCU can retain its old boot and cfg in RAM across a host restart.
    bool direct = false;
    const bool modeKnown = get_cfg_direct_mode(direct) == OS_OK;
    if (wasWaiting && (!modeKnown || direct)) {
        require_power_cycle("await_power_cycle");
        return false;
    }
    if (!modeKnown) {
        readFault = true;
        init_result.stage = "read_mode";
        if (get_cfg_direct_mode(direct) != OS_OK) {
            chsc6x_err("CHSC6X: I2C mode unreadable\r\n");
            return false;
        }
    }
    if (direct) {
        uint8_t control;
        if (chsc6x_read_bytes_u16addr(_i2c_addr, 0x0602, &control, 1) != OS_OK) {
            if (!readFault) {
                init_result.stage = "mcu_state";
            }
            readFault = true;
            if (chsc6x_read_bytes_u16addr(_i2c_addr, 0x0602, &control, 1) != OS_OK) {
                init_result.stage = "mcu_state";
                chsc6x_err("CHSC6X: MCU state unreadable\r\n");
                return false;
            }
        }
        if (control == 0x05) {
            require_power_cycle("mcu_state");
            return false;
        }
    }
    return true;
}

chsc6x::probe_state chsc6x::begin_probe(bool &hasReset, uint32_t &identity, bool &readFault,
                                            maintenance_probe *maintenance)
{
    const bool wasWaiting = init_result.needs_power_cycle;
    init_result = chsc6x_init_result{};
    init_result.status = chsc6x_init_status::error;
    init_result.stage = "i2c_init";
    hasReset = semi_touch_get_rst() >= 0;
    identity = 0;
    readFault = false;
    if (!prepare_controller(hasReset, wasWaiting, readFault)) {
        return probe_state::blocked;
    }

    const probe_state state = set_cfg_direct_mode(true) == OS_OK
        ? read_running_versions(init_result.before, identity, maintenance) : probe_state::unreadable;
    if (state == probe_state::burn_helper) {
        require_power_cycle("burn_helper_active");
        return probe_state::blocked;
    }

    init_result.before_valid = state == probe_state::running;
    if (state == probe_state::unsupported) {
        init_result.status = chsc6x_init_status::incompatible;
    }
    if (maintenance && maintenance->boot_reads_inconsistent) {
        init_result.stage = "boot_read_unreliable";
        chsc6x_err("CHSC6X: running boot header reads disagree\r\n");
    } else if (state == probe_state::cfg_inconsistent) {
        init_result.stage = "cfg_read_unreliable";
        chsc6x_err("CHSC6X: running cfg reads disagree\r\n");
    } else if (readFault) {
        chsc6x_err("CHSC6X: I2C read failed at %s\r\n", init_result.stage);
    } else if (state == probe_state::cfg_invalid || state == probe_state::cfg_unreadable) {
        init_result.stage = "read_cfg";
        chsc6x_err("CHSC6X: failed to read a valid running cfg\r\n");
    } else if (state != probe_state::running) {
        init_result.stage = "boot_header";
        chsc6x_err("CHSC6X: failed to identify running firmware\r\n");
    }
    return state;
}

void chsc6x::chsc6x_init()
{
    bool hasReset, readFault;
    uint32_t identity;
    const probe_state state = begin_probe(hasReset, identity, readFault);
    if (state == probe_state::blocked) {
        return;
    }
    if (state == probe_state::running && !readFault) {
        init_result.status = chsc6x_init_status::ready;
        init_result.stage = "versions_read";
    }
    restore_touch_mode(state == probe_state::running || state == probe_state::cfg_unreadable ||
                       state == probe_state::cfg_invalid);
}

int chsc6x::get_cfg_direct_mode(bool &direct)
{
    uint8_t mode[3];
    if (chsc6x_read_bytes_u16addr(_i2c_addr, 0x0001, mode, sizeof(mode)) != OS_OK) {
        return -OS_ERROR;
    }
    direct = mode[0] == (_i2c_addr << 1) && mode[2] == 0x01;
    return OS_OK;
}

int chsc6x::set_cfg_direct_mode(bool direct)
{
    uint8_t enter[] = {0x28, 0x35, 0xC1, 0x00, 0x35, 0xAE};
    uint8_t leave = 0x05;
    for (int attempt = 0; attempt <= TP_RETRY_CNT2; ++attempt) {
        bool isDirect = false;
        const bool readable = get_cfg_direct_mode(isDirect) == OS_OK;
        if (readable && isDirect == direct) {
            return OS_OK;
        }
        if (attempt == TP_RETRY_CNT2) {
            break;
        }
        // Confirm the transition by reading the signature again after the delay.
        if (direct && readable) {
            chsc6x_write_bytes_u16addr(_i2c_addr, 0x42BD, enter, sizeof(enter));
        } else if (!direct) {
            chsc6x_write_bytes_u16addr(_i2c_addr, 0x0003, &leave, 1);
        }
        chsc6x_msleep(30);
    }
    return -OS_ERROR;
}

int chsc6x::chsc6x_read_cfg(chsc6x_cfg &cfg)
{
    if (init_result.needs_power_cycle) {
        return CHSC6X_CFG_ENTER_FAILED;
    }
    chsc6x_cfg snapshot;
    int status = CHSC6X_CFG_ENTER_FAILED;
    if (set_cfg_direct_mode(true) == OS_OK) {
        for (int attempt = 0; attempt < TP_RETRY_CNT2; ++attempt) {
            if (attempt > 0) {
                chsc6x_msleep(20);
            }
            status = read_running_cfg(snapshot);
            if (status == OS_OK) {
                break;
            }
        }
    }
    if (set_cfg_direct_mode(false) != OS_OK) {
        return CHSC6X_CFG_RESTORE_FAILED;
    }
    if (status == OS_OK) {
        cfg = snapshot;
    }
    return status;
}




int chsc6x::chsc6x_read_touch_info(uint16_t *touchX, uint16_t *touchY)
{
    if (init_result.needs_power_cycle) {
        return -OS_ERROR;
    }
    int ret;
    uint8_t i, point_num;
    uint8_t rd_len = 16;
    uint8_t rd_buf[24] = { 0 };
    struct ts_event events[CHSC6X_MAX_POINTS_NUM];
    memset(&events, 0, sizeof(events));

    ret = chsc6x_read_bytes_u8addr(_i2c_addr, 0x0, rd_buf, rd_len);
    if( 0 != ret)
    {
        chsc6x_err("chsc6x: chsc6x_read_touch_info iic err! rd_len=%d, ret=%d \r\n", rd_len, ret);
        return -OS_ERROR;
    }

    point_num = rd_buf[2] & 0x07;

    if((0 == rd_buf[2] && 0 == rd_buf[3] && 0 == rd_buf[4] && 0 == rd_buf[6]) || \
        (0xff== rd_buf[2] && 0xff == rd_buf[3] && 0xff == rd_buf[4] && 0xff == rd_buf[6]))
    {
        return -OS_ERROR;   
    }

    for (i = 0; i < CHSC6X_MAX_POINTS_NUM; i++)
    {
        if ((rd_buf[6 * i + 3] & 0xc0) == 0xc0)
        {
            continue;
        }
        events[i].x = (uint16_t) (rd_buf[6 * i + 3] & 0x0F) << 8 | (uint16_t) rd_buf[6 * i + 4];
        events[i].y= (uint16_t) (rd_buf[6 * i + 5] & 0x0F) << 8 | (uint16_t) rd_buf[6 * i + 6];
        events[i].flag = rd_buf[6 * i + 3] >> 4;
        events[i].id = rd_buf[6 * i + 5] >> 4;
    }
    if(point_num==0)
    {
        return -1;
    }
  *touchX = events[0].x;
  *touchY = events[0].y;
  static uint32_t num_n=0;
//   if(((num_n++)%100)==0)
  {
    chsc6x_info("chsc6x: point_num:%d, X1:%d, Y1:%d, flag1:%d, id1:%d; \r\n", \
                point_num, events[0].x, events[0].y, events[0].flag, events[0].id);
    chsc6x_info("chsc6x: point_num:%d, X2:%d, Y2:%d, flag2:%d, id2:%d; \r\n", \
                point_num, events[1].x, events[1].y, events[1].flag, events[1].id);

    if(0 == point_num)
    {
        chsc6x_info("chsc6x: all point up\r\n");
    }
  }

    return OS_OK;
}

#if CHSC6X_GESTURE
void chsc6x::chsc6x_gesture_check(void)
{
    uint8_t buft[1] = {0};
    chsc6x_tp_reset(HW_CMD_RESET);
    if(OS_OK == chsc6x_write_bytes_u16addr(CHSC6X_I2C_ID, 0xD001, buft, 0))//0xd000-disable
    {
        g_gesture_status = 1;
        real_suspend_flag = 1;
        chsc6x_info("chsc6x: enable gesture check succeed \r\n");
    } 
    else
    {
        chsc6x_err("chsc6x: enable gesture check failed! \r\n");
    }

}
uint8_t chsc6x::chsc6x_get_gesture(void)
{    
    uint8_t rd_buf[24] = { 0 };
    if(g_gesture_enable && g_gesture_status)
	{
        chsc6x_read_bytes_u8addr(_i2c_addr, 0xd3, rd_buf, 4);
        chsc6x_info("chsc6x : gesture_id:%x; \r\n", rd_buf[0]);
        return rd_buf[0];
	}
    return 0;
}
#endif





void chsc6x::chsc6x_resume(void)    //Call when the display lights up
{
    if (!can_reset_for_sleep) {
        return;
    }

    chsc6x_info("chsc6x_resume enter.\r\n");

#if CHSC6X_GESTURE
    if(1 == g_gesture_enable)
    {
        g_gesture_status = 0;
    }
#endif
    chsc6x_tp_reset(HW_CMD_RESET);
    real_suspend_flag = 0;
}

void chsc6x::chsc6x_suspend(void)    //Call when the display is off
{
    if (!can_reset_for_sleep) {
        return;
    }

    uint8_t buff[1] = {0};

#if CHSC6X_GESTURE
    if(1 == g_gesture_enable)
    {
        chsc6x_gesture_check();
        return;
    }
#endif
    chsc6x_tp_reset(HW_CMD_RESET);
    if(OS_OK == chsc6x_write_bytes_u16addr(CHSC6X_I2C_ID, 0xA503, buff, 0))
    {
        real_suspend_flag = 1;
        chsc6x_info("chsc6x: enter suspend succeed! \r\n");
    }else{
        chsc6x_err("chsc6x: enter suspend failed! \r\n");
    }
}
