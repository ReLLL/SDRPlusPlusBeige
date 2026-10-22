/* Exercise production driver register writes against a simulated USB device. */
#include <assert.h>
#include <stdio.h>
#include RTLSDR_SOURCE_FILE

static unsigned char registers[64];
static int transfers, pointer, fail_io;

static unsigned char reverse(unsigned char v) {
    unsigned char result = 0;
    for (int i = 0; i < 8; ++i) { result = (result << 1) | (v & 1); v >>= 1; }
    return result;
}

int LIBUSB_CALL libusb_control_transfer(libusb_device_handle *handle,
        uint8_t type, uint8_t request, uint16_t value, uint16_t index,
        unsigned char *data, uint16_t length, unsigned int timeout) {
    (void)handle; (void)request; (void)value; (void)timeout;
    ++transfers;
    if (fail_io) return LIBUSB_ERROR_NO_DEVICE;
    if ((index >> 8) == IICB) {
        if (type & LIBUSB_ENDPOINT_IN) {
            for (int i = 0; i < length; ++i) {
                int reg = pointer + i;
                // AGC keeps estimating gain even when manual values are active.
                unsigned char v = reg == 3 ? 0xed : 0;
                data[i] = reverse(v);
            }
        } else if (length) {
            pointer = data[0];
            for (int i = 1; i < length; ++i) registers[pointer+i-1] = data[i];
        }
    } else if (type & LIBUSB_ENDPOINT_IN) memset(data, 0, length);
    return length;
}

int main(void) {
    rtlsdr_dev_t dev = {0};
    dev.devh = (libusb_device_handle *)&dev;
    dev.tuner_type = RTLSDR_TUNER_R828D;
    dev.tuner = &tuners[RTLSDR_TUNER_R828D];
    dev.r82xx_c.i2c_addr = R828D_I2C_ADDR;
    dev.r82xx_c.max_i2c_msg_len = 8;
    dev.r82xx_p.cfg = &dev.r82xx_c;
    dev.r82xx_p.rtl_dev = &dev;
    for (int lna = 0; lna < 16; ++lna) for (int mix = 0; mix < 16; ++mix) for (int vga = 0; vga < 16; ++vga) {
        for (int i = 0; i < 64; ++i) registers[i] = (unsigned char)(i * 7);
        memcpy(dev.r82xx_p.regs, registers + REG_SHADOW_START, NUM_REGS);
        unsigned char high5 = registers[5] & 0xe0, high7 = registers[7] & 0xe0, high12 = registers[12] & 0xe0;
        assert(rtlsdr_beige_set_gain_stages(&dev, lna, mix, vga) == 0);
        assert(registers[5] == (high5 | 0x10 | lna));
        assert(registers[7] == (high7 | mix));
        assert(registers[12] == (high12 | vga));
        int a = -1, b = -1, c = -1;
        assert(rtlsdr_beige_get_gain_stages(&dev, &a, &b, &c) == 0);
        assert(a == lna && b == mix && c == vga);
        // V4 RF input selection and all filter/PLL registers remain untouched.
        for (int i = 0; i < 64; ++i) if (i != 5 && i != 7 && i != 12)
            assert(registers[i] == (unsigned char)(i * 7));
    }
    assert(rtlsdr_set_tuner_gain_mode(&dev, 0) == 0);
    int agc_lna, agc_mixer, agc_vga;
    assert(rtlsdr_beige_get_gain_stages(&dev, &agc_lna, &agc_mixer, &agc_vga) == 0);
    assert(agc_lna == 13 && agc_mixer == 14);
    int before = transfers, a, b, c;
    assert(rtlsdr_beige_set_gain_stages(&dev, -1, 0, 0) == LIBUSB_ERROR_INVALID_PARAM);
    assert(rtlsdr_beige_set_gain_stages(&dev, 0, 16, 0) == LIBUSB_ERROR_INVALID_PARAM);
    assert(rtlsdr_beige_get_gain_stages(&dev, NULL, &b, &c) == LIBUSB_ERROR_INVALID_PARAM);
    dev.tuner_type = RTLSDR_TUNER_E4000;
    assert(rtlsdr_beige_set_gain_stages(&dev, 0, 0, 0) == LIBUSB_ERROR_NOT_SUPPORTED);
    dev.tuner_type = RTLSDR_TUNER_R828D;
    dev.direct_sampling = 1;
    assert(rtlsdr_beige_get_gain_stages(&dev, &a, &b, &c) == LIBUSB_ERROR_NOT_SUPPORTED);
    dev.direct_sampling = 0;
    dev.dev_lost = 1;
    assert(rtlsdr_beige_set_gain_stages(&dev, 0, 0, 0) == LIBUSB_ERROR_NO_DEVICE);
    assert(transfers == before);
    dev.dev_lost = 0;
    fail_io = 1;
    assert(rtlsdr_beige_set_gain_stages(&dev, 1, 2, 3) < 0);
    puts("4096 production gain combinations, readback, preserved V4 routing, rejection and USB errors passed.");
}
