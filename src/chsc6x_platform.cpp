#include "chsc6x_platform.h"

int chsc6x_platform::semi_touch_i2c_init(void)
{
    if (_sda_pin != -1 && _scl_pin != -1) {
        return _wire->begin(_sda_pin, _scl_pin, 400000) ? OS_OK : -OS_ERROR;
    }
    return _wire->begin() ? OS_OK : -OS_ERROR;
}

int chsc6x_platform::i2cRead(uint8_t i2c_adr, uint16_t reg_adr, uint8_t *rxbuf, uint16_t lenth)
{
    return readRegister(i2c_adr, reg_adr, reg_adr > 0xFF, rxbuf, lenth);
}

int chsc6x_platform::readRegister(uint8_t id, uint16_t reg, bool wideAddress, uint8_t *data, uint16_t length)
{
    if (data == nullptr || length == 0) {
        return -OS_ERROR;
    }
    const uint8_t address[] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg)};
    const size_t addressLength = wideAddress ? 2 : 1;
    _wire->beginTransmission(id);
    if (_wire->write(address + (2 - addressLength), addressLength) != addressLength) {
        _wire->endTransmission();
        return -OS_ERROR;
    }
    if (_wire->endTransmission(false) != 0) {
        return -OS_ERROR;
    }
    const size_t received = _wire->requestFrom(id, static_cast<size_t>(length), true);
    uint16_t count = 0;
    while (count < received && count < length && _wire->available()) {
        data[count++] = static_cast<uint8_t>(_wire->read());
    }
    return count;
}

int chsc6x_platform::i2cSend(uint8_t i2c_adr, uint16_t reg_adr, const uint8_t *txbuf, uint16_t lenth)
{
    uint8_t buffer[MAX_IIC_WR_LEN+2] = {0};
    if (NULL==txbuf|| lenth>MAX_IIC_WR_LEN)
    {
        return -OS_ERROR;
    }
    buffer[0] =  reg_adr >> 8;
    buffer[1] =  reg_adr;
    memcpy(&buffer[2], txbuf, lenth);

    _wire->beginTransmission(i2c_adr);
    const size_t written = _wire->write(buffer, lenth + 2);
    if (_wire->endTransmission() != 0 || written != static_cast<size_t>(lenth + 2)) {
        return -OS_ERROR;
    }
    return lenth+2;
}

int chsc6x_platform::chsc6x_read_bytes_u8addr(uint8_t id, uint8_t adr, uint8_t *rxbuf, uint16_t lenth)
{
    int ret = i2cRead(id,adr,rxbuf,lenth);
    if(ret == lenth) {
        return OS_OK;
    }else{
        return -OS_ERROR;
    }
}

int chsc6x_platform::chsc6x_write_bytes_u16addr(uint8_t id, uint16_t adr, const uint8_t *txbuf, uint16_t lenth)
{
    int ret = i2cSend(id,adr,txbuf,lenth);
    if(ret == lenth+2) {
        return OS_OK;
    }else{
        return -OS_ERROR;
    }
}

int chsc6x_platform::chsc6x_read_bytes_u16addr(uint8_t id, uint16_t adr, uint8_t *rxbuf, uint16_t lenth)
{
    int ret = readRegister(id, adr, true, rxbuf, lenth);
    if(ret == lenth) {
        return OS_OK;
    }else{
        return -OS_ERROR;
    }
}

void chsc6x_platform::chsc6x_msleep(int ms)
{
    delay(ms);
}

void chsc6x_platform::chsc6x_tp_reset_delay(chsc6x_reset_e type)
{
    switch (type)
    {
        case HW_CMD_RESET:
            chsc6x_msleep(30);
            break;

        case HW_ACTIVE_RESET:
            chsc6x_msleep(2);
            break;

        default:
            break;
    }

}

int chsc6x_platform::chsc6x_tp_reset(chsc6x_reset_e type)
{
    if (RESET_NONE >= type || RESET_MAX <= type)
    {
        return -OS_ERROR;
    }
    if(_rst_pin != -1) {
        semi_rst_pin_low(_rst_pin);
        chsc6x_msleep(20);
        semi_rst_pin_high(_rst_pin);
        chsc6x_tp_reset_delay(type);
    }
    return OS_OK;
}

bool state = false;
void blink() {
  state = !state;
}
/* GPIO-INT*/
int chsc6x_platform::semi_touch_get_int(void)
{
    if(_int_pin != -1) {
        pinMode(_int_pin, INPUT_PULLUP);
        int irq = digitalPinToInterrupt(_int_pin);
        if (irq >= 0) {
            attachInterrupt(irq, blink, RISING);
        }
    }
    return _int_pin;
}

/* GPIO-RST*/
int chsc6x_platform::semi_touch_get_rst(void)
{
    if(_rst_pin != -1) {
        pinMode(_rst_pin,OUTPUT);
        digitalWrite(_rst_pin, HIGH);
    }
    return _rst_pin;
}

void chsc6x_platform::semi_rst_pin_low(int pin)
{
    if(pin == -1) {
        return;
    }
    pinMode(pin,OUTPUT);
    digitalWrite(pin, LOW);
}

void chsc6x_platform::semi_rst_pin_high(int pin)
{
    if(pin == -1) {
        return;
    }
    pinMode(pin,OUTPUT);
    digitalWrite(pin, HIGH);
}
