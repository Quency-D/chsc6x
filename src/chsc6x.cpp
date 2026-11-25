#include "chsc6x.h"

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
