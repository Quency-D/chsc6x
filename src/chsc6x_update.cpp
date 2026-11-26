#include "chsc6x.h"
#include "chsc6x_update.h"
#include <string.h>

namespace {

// Little-endian fields and checksums shared by the image and programming protocol.
uint16_t le16(const uint8_t *p)
{
    return p[0] | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t le32(const uint8_t *p)
{
    return le16(p) | (static_cast<uint32_t>(le16(p + 2)) << 16);
}

uint16_t sum16(const uint8_t *data, size_t length)
{
    uint16_t sum = 0;
    for (size_t i = 0; i + 1 < length; i += 2) {
        sum += le16(data + i);
    }
    return sum;
}

uint32_t checksum_ex(const uint8_t *data, size_t length)
{
    uint16_t sum = 0;
    uint16_t weighted = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
        weighted += static_cast<uint16_t>(i * data[i]);
    }
    return sum | (static_cast<uint32_t>(weighted) << 16);
}

// Identify the panel and validate the packaged CFG and BOOT before programming.
constexpr uint32_t chip3536 = 0x35368008;
constexpr uint32_t runningTag = 0x544C4E4B;
constexpr uint32_t burnHelper = 0x6D6F8008;
constexpr char mcuReadWarning[] = "CHSC6X: MCU state unreadable, skipping update\r\n";
constexpr uint32_t cfgIdentityMask = 0x03FFFFFF;
constexpr uint32_t bootIdentityMask = 0x00FFFFFF;

// This vendor package contains one match entry, one cfg and one boot image.
struct UpdateImage {
    const uint8_t *cfg;
    const uint8_t *boot;
    uint16_t boot_size;
    uint32_t cfg_identity;
    uint32_t boot_match;
    chsc6x_versions versions;
};

bool open_image(UpdateImage &image)
{
    const uint8_t *p = fw_file_tlsc6x_05_12;
    constexpr size_t cfgOffset = 24 + 4;
    constexpr size_t bootOffset = cfgOffset + sizeof(chsc6x_cfg::bytes);
    constexpr size_t size = sizeof(fw_file_tlsc6x_05_12);
    static_assert(size >= bootOffset + 16 && size - bootOffset <= 0x8000, "Invalid CHSC image size");
    if (le32(p) != 0x43534843 || le32(p + 8) != 1 || le32(p + 12) != 1 ||
        le32(p + 16) != sizeof(chsc6x_cfg::bytes) || le32(p + 20) != size - bootOffset) {
        return false;
    }
    image.cfg = p + cfgOffset;
    image.boot = p + bootOffset;
    image.boot_size = size - bootOffset;
    image.cfg_identity = le32(image.cfg) & cfgIdentityMask;
    image.boot_match = le32(p + 24);
    image.versions.cfg = image.cfg[3] >> 2;
    image.versions.boot = le16(image.boot + 4);
    const size_t body = image.boot_size - 4;
    return image.boot_match == (image.cfg_identity & bootIdentityMask) &&
           image.cfg[106] == (CHSC6X_I2C_ID << 1) && sum16(image.cfg, sizeof(chsc6x_cfg::bytes)) == 0 &&
           le32(image.boot) == chip3536 && le32(image.boot + 8) == runningTag &&
           image.versions.boot != 0 && le16(image.boot + 6) == body &&
           checksum_ex(image.boot, body) == le32(image.boot + body);
}

// Chipsemi RAM programmer and its flash command mailbox.
constexpr uint16_t data_address = 0x9000;
constexpr uint16_t command_address = 0x9F00;
constexpr uint16_t response_address = 0x9F40;
constexpr uint16_t cfg_address = 0x8000;
constexpr uint16_t block_size = 1024;
constexpr uint8_t write_command = 0x30;
constexpr uint8_t read_command = 0x31;
constexpr uint8_t attempts = 3;
static_assert(sizeof(fw_fcode_burn) == 1776, "Unexpected CHSC6X burn helper size");

void put_le16(uint8_t *data, uint16_t value)
{
    data[0] = uint8_t(value);
    data[1] = uint8_t(value >> 8);
}

// Chipsemi 3536 burn helper. The caller owns the controller in direct mode
// until it resets or power-cycles the touch controller after programming.
class chsc6x_burn {
public:
    explicit chsc6x_burn(chsc6x_platform &bus) : bus_(bus) {}

    bool start();
    bool write_cfg(const uint8_t *data);
    bool write_boot(const uint8_t *data, uint16_t len);

private:
    enum class command_result { ok, retry, stop };

    bool write_ram(uint16_t addr, const uint8_t *data, uint16_t len);
    bool matches_ram(uint16_t addr, const uint8_t *data, uint16_t len);
    bool load_checked(uint16_t addr, const uint8_t *data, uint16_t len);
    command_result send_command(uint8_t id, uint16_t addr, uint16_t len,
                                uint32_t checksum, uint8_t (&response)[8]);
    bool transfer_block(uint8_t id, uint16_t addr, const uint8_t *data, uint16_t len);
    bool program(uint16_t addr, const uint8_t *data, uint16_t len);

    chsc6x_platform &bus_;
};

bool chsc6x_burn::write_ram(uint16_t addr, const uint8_t *data, uint16_t len)
{
    // A 32-byte Wire transmission includes the two register-address bytes.
    while (len > 0) {
        const uint16_t count = len > 30 ? 30 : len;
        if (bus_.chsc6x_write_bytes_u16addr(bus_._i2c_addr, addr, data, count) != OS_OK) {
            return false;
        }
        addr += count;
        data += count;
        len -= count;
    }
    return true;
}

bool chsc6x_burn::matches_ram(uint16_t addr, const uint8_t *data, uint16_t len)
{
    uint8_t actual[32];
    while (len > 0) {
        const uint16_t count = len > sizeof(actual) ? sizeof(actual) : len;
        if (bus_.chsc6x_read_bytes_u16addr(bus_._i2c_addr, addr, actual, count) != OS_OK ||
            memcmp(actual, data, count) != 0) {
            return false;
        }
        addr += count;
        data += count;
        len -= count;
    }
    return true;
}

bool chsc6x_burn::load_checked(uint16_t addr, const uint8_t *data, uint16_t len)
{
    while (len > 0) {
        const uint16_t count = len > 128 ? 128 : len;
        uint8_t attempt = 0;
        while (!write_ram(addr, data, count) || !matches_ram(addr, data, count)) {
            if (++attempt == attempts) {
                return false;
            }
        }
        addr += count;
        data += count;
        len -= count;
    }
    return true;
}

bool chsc6x_burn::start()
{
    const uint8_t stop = 0x05;
    const uint8_t disable_irq = 0;
    const uint8_t run = 0x88;
    if (!load_checked(0x0602, &stop, 1) ||
        !load_checked(0x0643, &disable_irq, 1) ||
        !load_checked(0x8000, fw_fcode_burn, sizeof(fw_fcode_burn))) {
        return false;
    }
    for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
        if (write_ram(0x0602, &run, 1)) {
            bus_.chsc6x_msleep(30);
            return true;
        }
    }
    return false;
}

chsc6x_burn::command_result chsc6x_burn::send_command(
    uint8_t id, uint16_t addr, uint16_t len, uint32_t checksum, uint8_t (&response)[8])
{
    const uint8_t clear = 0;
    const uint8_t trigger = 0x35;
    uint8_t packet[16] = {};
    packet[0] = id;
    packet[1] = uint8_t(~id);
    put_le16(packet + 2, addr);
    put_le16(packet + 4, len);
    put_le16(packet + 6, uint16_t(checksum));
    packet[8] = id == write_command ? 0x11 : 0x03;
    packet[9] = trigger;
    put_le16(packet + 10, uint16_t(0 - sum16(packet, 10)));
    put_le16(packet + 12, uint16_t(checksum >> 16));
    packet[9] = 0x30;
    if (!write_ram(response_address, &clear, 1) ||
        !write_ram(command_address, packet, sizeof(packet))) {
        return command_result::retry;
    }

    // A failed transfer of the trigger byte can still have reached the chip.
    // Wait for a valid reply before restaging its shared command/data buffer.
    write_ram(command_address + 9, &trigger, 1);
    for (uint8_t poll = 0; poll < 100; ++poll) {
        bus_.chsc6x_msleep(20);
        if (bus_.chsc6x_read_bytes_u16addr(bus_._i2c_addr, response_address,
                                          response, sizeof(response)) != OS_OK ||
            response[0] != id || sum16(response, sizeof(response)) != 0) {
            continue;
        }
        if (response[1] == 0) {
            return command_result::ok;
        }
        if (response[1] == 0x09 && le16(response + 2) == 0x05) {
            return command_result::stop;
        }
        return command_result::retry;
    }
    return command_result::stop;
}

bool chsc6x_burn::transfer_block(uint8_t id, uint16_t addr, const uint8_t *data, uint16_t len)
{
    const uint32_t expected = checksum_ex(data, len);
    for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
        if (id == write_command && !write_ram(data_address, data, len)) {
            continue;
        }
        uint8_t response[8];
        const auto result = send_command(id, addr, len, id == write_command ? expected : 0, response);
        if (result == command_result::stop) {
            return false;
        }
        if (result == command_result::ok) {
            if (id == write_command) {
                return true;
            }
            if (le32(response + 2) == expected &&
                matches_ram(data_address, data, len)) {
                return true;
            }
        }
    }
    return false;
}

bool chsc6x_burn::program(uint16_t addr, const uint8_t *data, uint16_t len)
{
    while (len > 0) {
        const uint16_t count = len > block_size ? block_size : len;
        if (!transfer_block(write_command, addr, data, count) ||
            !transfer_block(read_command, addr, data, count)) {
            return false;
        }
        addr += count;
        data += count;
        len -= count;
    }
    return true;
}

bool chsc6x_burn::write_cfg(const uint8_t *data)
{
    return data && program(cfg_address, data, 204);
}

bool chsc6x_burn::write_boot(const uint8_t *data, uint16_t len)
{
    if (!data || len <= 8 || len > cfg_address || data[8] != 0x4B) {
        return false;
    }
    // Vendor sequence writes the entire supplied image, then rewrites byte 8.
    // Read back the first block again after this last write.
    return program(0, data, len) && transfer_block(write_command, 8, data + 8, 1) &&
           transfer_block(read_command, 0, data, len > block_size ? block_size : len);
}

} // namespace

// Read the running image and coordinate reset, programming and verification.
int chsc6x::read_running_cfg(chsc6x_cfg &cfg)
{
    for (size_t offset = 0; offset < sizeof(cfg.bytes); offset += MAX_IIC_RD_LEN) {
        const size_t remaining = sizeof(cfg.bytes) - offset;
        const size_t count = remaining < MAX_IIC_RD_LEN ? remaining : MAX_IIC_RD_LEN;
        if (chsc6x_read_bytes_u16addr(_i2c_addr, 0x9E00 + offset, cfg.bytes + offset, count) != OS_OK) {
            return CHSC6X_CFG_READ_FAILED;
        }
    }

    const uint16_t checksum = sum16(cfg.bytes, sizeof(cfg.bytes));
    return cfg.bytes[106] == (_i2c_addr << 1) && checksum == 0 ? OS_OK : CHSC6X_CFG_INVALID;
}

// Called in direct mode. With no RST the helper can survive an ESP32 restart.
chsc6x::probe_state chsc6x::read_running_versions(chsc6x_versions &versions, uint32_t &identity)
{
    uint8_t header[12];
    chsc6x_cfg cfg;
    probe_state state = probe_state::unreadable;
    for (int attempt = 0; attempt < TP_RETRY_CNT2; ++attempt) {
        if (attempt > 0) {
            chsc6x_msleep(20);
        }
        if (chsc6x_read_bytes_u16addr(_i2c_addr, 0x8000, header, sizeof(header)) != OS_OK) {
            continue;
        }
        if (le32(header) == burnHelper) {
            return probe_state::burn_helper;
        }
        if (le32(header) != chip3536) {
            return probe_state::unsupported;
        }
        const uint16_t boot = le16(header + 4);
        if (le32(header + 8) != runningTag || boot == 0 || boot == 0xFFFF) {
            state = probe_state::unreadable;
            continue;
        }
        if (read_running_cfg(cfg) != OS_OK) {
            state = probe_state::cfg_unreadable;
            continue;
        }
        versions = {cfg.version(), boot};
        identity = le32(cfg.bytes) & cfgIdentityMask;
        return probe_state::running;
    }
    return state;
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

bool chsc6x::prepare_controller(bool hasReset, const chsc6x_init_result &previous, bool &skipUpdate)
{
    semi_touch_get_int();
    if (semi_touch_i2c_init() != OS_OK) {
        if (!hasReset && previous.needs_power_cycle) {
            init_result = previous;
        }
        return false;
    }
    init_result.stage = "probe";
    chsc6x_msleep(60);
    if (hasReset) {
        chsc6x_tp_reset(HW_CMD_RESET);
        return true;
    }

    // A cold-started touch controller returns to mapping mode. A stopped
    // MCU can still expose its OLD boot/cfg in RAM before the helper loads.
    bool direct = false;
    const bool modeKnown = get_cfg_direct_mode(direct) == OS_OK;
    if (previous.needs_power_cycle && (!modeKnown || direct)) {
        init_result = previous;
        chsc6x_info("CHSC6X: power-cycle the touch controller\r\n");
        return false;
    }
    if (!modeKnown) {
        // Retry only to recover touch. This init must skip programming.
        skipUpdate = true;
        init_result.stage = "read_mode";
        if (get_cfg_direct_mode(direct) != OS_OK) {
            chsc6x_err("CHSC6X: I2C mode unreadable, skipping update\r\n");
            return false;
        }
    }
    if (direct) {
        uint8_t control = 0;
        if (chsc6x_read_bytes_u16addr(_i2c_addr, 0x0602, &control, 1) != OS_OK) {
            if (!skipUpdate) {
                init_result.stage = "mcu_state";
            }
            skipUpdate = true;
            if (chsc6x_read_bytes_u16addr(_i2c_addr, 0x0602, &control, 1) != OS_OK) {
                init_result.stage = "mcu_state";
                chsc6x_err(mcuReadWarning);
                return false;
            }
        }
        if (control == 0x05) {
            init_result.status = chsc6x_init_status::power_cycle_required;
            init_result.stage = "mcu_state";
            init_result.needs_power_cycle = true;
            chsc6x_info("CHSC6X: power-cycle the touch controller\r\n");
            return false;
        }
    }
    return true;
}

void chsc6x::chsc6x_init()
{
    const chsc6x_init_result previous = init_result;
    init_result = chsc6x_init_result{};
    init_result.status = chsc6x_init_status::error;
    init_result.stage = "i2c_init";
    const bool hasReset = semi_touch_get_rst() >= 0;
    bool skipUpdate = false;
    if (!prepare_controller(hasReset, previous, skipUpdate)) {
        return;
    }

    uint32_t identity = 0;
    const probe_state state = set_cfg_direct_mode(true) == OS_OK
        ? read_running_versions(init_result.before, identity) : probe_state::unreadable;
    if (state == probe_state::burn_helper) {
        if (previous.needs_power_cycle) {
            init_result = previous;
        } else {
            init_result.status = chsc6x_init_status::power_cycle_required;
            init_result.stage = "burn_helper_active";
        }
        init_result.needs_power_cycle = true;
        chsc6x_info("CHSC6X: power-cycle the touch controller\r\n");
        return;
    }
    init_result.before_valid = state == probe_state::running;
    if (init_result.before_valid && !skipUpdate) {
        update_if_needed(identity, hasReset);
        return;
    }
    if (state == probe_state::unsupported) {
        init_result.status = chsc6x_init_status::incompatible;
    }
    if (skipUpdate) {
        chsc6x_err("CHSC6X: I2C read failed at %s, skipping update\r\n", init_result.stage);
    } else if (state == probe_state::cfg_unreadable) {
        init_result.stage = "read_cfg";
        chsc6x_err("CHSC6X: cfg unreadable, skipping update\r\n");
    } else if (state == probe_state::unreadable) {
        chsc6x_err("CHSC6X: running firmware unreadable, skipping update\r\n");
    }
    restore_touch_mode(init_result.before_valid || state == probe_state::cfg_unreadable);
}

void chsc6x::update_if_needed(uint32_t identity, bool hasReset)
{
    UpdateImage image;
    if (!open_image(image)) {
        init_result.stage = "invalid_package";
        restore_touch_mode(true);
        return;
    }
    const bool cfgMatches = identity == image.cfg_identity;
    const bool bootMatches = (identity & bootIdentityMask) == image.boot_match;
    if (!cfgMatches && !bootMatches) {
        init_result.status = chsc6x_init_status::incompatible;
        init_result.stage = "panel_identity";
        restore_touch_mode(true);
        return;
    }
    const bool writeCfg = cfgMatches && image.versions.cfg > init_result.before.cfg;
    const bool writeBoot = bootMatches && image.versions.boot > init_result.before.boot;
    if (!writeCfg && !writeBoot) {
        init_result.status = chsc6x_init_status::current;
        init_result.stage = "current";
        init_result.after = init_result.before;
        init_result.after_valid = true;
        restore_touch_mode(true);
        return;
    }

    chsc6x_burn burner(*this);
    init_result.stage = "load_helper";
    bool ok = burner.start();
    if (ok && writeCfg) {
        init_result.stage = "cfg_write_verify";
        init_result.cfg_written = burner.write_cfg(image.cfg);
        ok = init_result.cfg_written;
    }
    if (ok && writeBoot) {
        init_result.stage = "boot_write_verify";
        init_result.boot_written = burner.write_boot(image.boot, image.boot_size);
        ok = init_result.boot_written;
    }

    // The helper replaces the normal firmware in RAM. Changing the I2C mode
    // alone cannot reload the running firmware from nonvolatile storage.
    if (!hasReset) {
        init_result.needs_power_cycle = true;
        if (ok) {
            init_result.status = chsc6x_init_status::power_cycle_required;
            init_result.stage = "written_verified";
        }
        chsc6x_info("CHSC6X: power-cycle the touch controller\r\n");
        return;
    }
    chsc6x_tp_reset(HW_CMD_RESET);
    uint32_t identityAfterReset = 0;
    const bool normal = set_cfg_direct_mode(true) == OS_OK &&
        read_running_versions(init_result.after, identityAfterReset) == probe_state::running;
    init_result.after_valid = normal;
    if (ok) {
        const uint8_t expectedCfg = writeCfg ? image.versions.cfg : init_result.before.cfg;
        const uint16_t expectedBoot = writeBoot ? image.versions.boot : init_result.before.boot;
        if (normal && identityAfterReset == identity && init_result.after.cfg == expectedCfg &&
            init_result.after.boot == expectedBoot) {
            init_result.status = chsc6x_init_status::updated;
            init_result.stage = "verified";
        } else {
            init_result.stage = "verify_running";
        }
    }
    restore_touch_mode(normal);
}
