/* QC5 T23DL RAM-only sensor identification. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>

#ifndef QC5_SENSOR_TEST
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#endif

#define BIT(n) (UINT32_C(1) << (n))

enum { CPM, GPIO_A, I2C0 };
enum {
    CPCCR = 0x00, APLL = 0x10, MPLL = 0x14, CLKGR = 0x20, CIMCDR = 0x7c,
    CIM_LATCH = BIT(29), CIM_BUSY = BIT(28), CIM_STOP = BIT(27),
    I2C_GATE = BIT(7),
    GPIO_INT_CLR = 0x18, GPIO_MASK_SET = 0x24, GPIO_MASK_CLR = 0x28,
    GPIO_PAT1_CLR = 0x38, GPIO_PAT0_SET = 0x44, GPIO_PAT0_CLR = 0x48,
    SENSOR_BUS_PINS = BIT(12) | BIT(13), SENSOR_MCLK_PIN = BIT(15), SENSOR_RESET = BIT(18),
    I2C_CON = 0x00, I2C_TAR = 0x04, I2C_DATA_CMD = 0x10,
    I2C_SS_HCNT = 0x14, I2C_SS_LCNT = 0x18, I2C_INTR_MASK = 0x30,
    I2C_RAW_INTR = 0x34, I2C_CLR_INTR = 0x40, I2C_ENABLE = 0x6c,
    I2C_STATUS = 0x70, I2C_SDA_HOLD = 0x7c, I2C_ABORT_SOURCE = 0x80,
    I2C_DMA_CR = 0x88, I2C_SDA_SETUP = 0x94, I2C_ENABLE_STATUS = 0x9c,
    I2C_RESTART = BIT(10), I2C_STOP = BIT(9), I2C_READ = BIT(8),
    I2C_TX_ABRT = BIT(6), I2C_STOP_DET = BIT(9), I2C_TFNF = BIT(1),
    I2C_RFNE = BIT(3), I2C_MSTACT = BIT(5),
    GC1084_ADDRESS = 0x37,
};
enum result {
    SENSOR_ID_OK, SENSOR_NACK, SENSOR_TIMEOUT, SENSOR_WRONG_ID, SENSOR_CLOCK, SENSOR_MAP,
};

#ifdef QC5_SENSOR_TEST
uint32_t reg_read(unsigned region, unsigned offset);
void reg_write(unsigned region, unsigned offset, uint32_t value);
void delay_us(unsigned duration);
#else
static volatile uint32_t *regs[3];

static uint32_t reg_read(unsigned region, unsigned offset)
{
    return regs[region][offset / 4];
}

static void reg_write(unsigned region, unsigned offset, uint32_t value)
{
    regs[region][offset / 4] = value;
    __asm__ volatile("sync" ::: "memory");
}

static void delay_us(unsigned duration)
{
    struct timespec interval = {duration / 1000000, (duration % 1000000) * 1000L};
    while (nanosleep(&interval, &interval) < 0 && errno == EINTR)
        ;
}
#endif

/* T23 PLL counters are literal divisors. Only use an enabled, locked parent. */
static uint32_t pll_rate(uint32_t value)
{
    uint32_t m = value >> 20, n = (value >> 14) & 63;
    uint32_t od1 = (value >> 11) & 7, od0 = (value >> 8) & 7;
    if ((value & (BIT(3) | BIT(0))) != (BIT(3) | BIT(0)) || !n || !od1 || !od0)
        return 0;
    uint64_t rate = ((uint64_t)24000000 * m) / (n * od1 * od0);
    return rate <= 2400000000u ? (uint32_t)rate : 0;
}

static int wait_bits(unsigned region, unsigned offset, uint32_t mask, uint32_t wanted)
{
    for (unsigned tries = 0; tries < 30; ++tries) {
        if ((reg_read(region, offset) & mask) == wanted)
            return 1;
        delay_us(1000);
    }
    return 0;
}

static int configure_clock(uint32_t *apb_rate)
{
    uint32_t cpccr = reg_read(CPM, CPCCR);
    unsigned sclka_src = cpccr >> 30, ahb2_src = (cpccr >> 24) & 3;
    uint32_t sclka = sclka_src == 2 ? pll_rate(reg_read(CPM, APLL)) :
                     sclka_src == 1 ? 24000000 : 0;
    uint32_t mpll = pll_rate(reg_read(CPM, MPLL));
    uint32_t apb_parent = ahb2_src == 1 ? sclka : ahb2_src == 2 ? mpll : 0;
    *apb_rate = apb_parent / (((cpccr >> 16) & 15) + 1);
    if (*apb_rate < 1000000 || *apb_rate > 200000000)
        return 0;

    const uint32_t rates[] = {27000000, 24000000};
    const uint32_t parents[] = {sclka, mpll};
    for (unsigned i = 0; i < 2; ++i) {
        for (unsigned parent = 0; parent < 2; ++parent) {
            uint32_t divider = parents[parent] / rates[i];
            if (!divider || divider > 256 || parents[parent] % rates[i])
                continue;
            if (!wait_bits(CPM, CIMCDR, CIM_BUSY, 0))
                return 0;
            uint32_t value = reg_read(CPM, CIMCDR);
            value &= ~(BIT(31) | BIT(30) | CIM_LATCH | CIM_BUSY | CIM_STOP | UINT32_C(0xff));
            value |= (parent << 30) | (divider - 1);
            reg_write(CPM, CIMCDR, value | CIM_LATCH);
            if (!wait_bits(CPM, CIMCDR, CIM_BUSY, 0))
                return 0;
            reg_write(CPM, CIMCDR, value);
            return 1;
        }
    }
    return 0;
}

static void configure_pins(void)
{
    const uint32_t bus_and_clock = SENSOR_BUS_PINS | SENSOR_MCLK_PIN;
    reg_write(GPIO_A, GPIO_INT_CLR, bus_and_clock | SENSOR_RESET);
    reg_write(GPIO_A, GPIO_PAT0_SET, bus_and_clock | SENSOR_RESET);
    reg_write(GPIO_A, GPIO_PAT1_CLR, bus_and_clock | SENSOR_RESET);
    reg_write(GPIO_A, GPIO_MASK_CLR, bus_and_clock);
    reg_write(GPIO_A, GPIO_MASK_SET, SENSOR_RESET);
    delay_us(5000);
    reg_write(GPIO_A, GPIO_PAT0_CLR, SENSOR_RESET);
    delay_us(10000);
    reg_write(GPIO_A, GPIO_PAT0_SET, SENSOR_RESET);
    delay_us(10000);
}

static int i2c_enable(unsigned enabled)
{
    reg_write(I2C0, I2C_ENABLE, enabled);
    return wait_bits(I2C0, I2C_ENABLE_STATUS, BIT(0), enabled);
}

static int configure_i2c(uint32_t apb_rate)
{
    if (!i2c_enable(0))
        return 0;
    reg_write(I2C0, I2C_INTR_MASK, 0);
    reg_write(I2C0, I2C_DMA_CR, 0);
    reg_write(I2C0, I2C_CON, 0x63); /* Master, standard speed, restart, slave disabled. */
    reg_write(I2C0, I2C_TAR, GC1084_ADDRESS);
    uint32_t half = apb_rate / 200000;
    uint32_t quarter = apb_rate / 400000;
    reg_write(I2C0, I2C_SS_HCNT, half > 14 ? half - 8 : 6);
    reg_write(I2C0, I2C_SS_LCNT, half > 9 ? half - 1 : 8);
    reg_write(I2C0, I2C_SDA_HOLD, quarter);
    reg_write(I2C0, I2C_SDA_SETUP, quarter > 256 ? 255 : quarter - 1);
    (void)reg_read(I2C0, I2C_CLR_INTR);
    return i2c_enable(1);
}

static enum result read_sensor(uint16_t address, uint8_t *value)
{
    if (!wait_bits(I2C0, I2C_STATUS, I2C_MSTACT, 0))
        return SENSOR_TIMEOUT;
    (void)reg_read(I2C0, I2C_CLR_INTR);
    const uint32_t words[] = {address >> 8, address & 0xff, I2C_RESTART | I2C_READ | I2C_STOP};
    for (unsigned i = 0; i < 3; ++i) {
        if (!wait_bits(I2C0, I2C_STATUS, I2C_TFNF, I2C_TFNF))
            return SENSOR_TIMEOUT;
        reg_write(I2C0, I2C_DATA_CMD, words[i]);
    }
    int received = 0;
    for (unsigned tries = 0; tries < 30; ++tries) {
        uint32_t irq = reg_read(I2C0, I2C_RAW_INTR);
        if (irq & I2C_TX_ABRT) {
            uint32_t source = reg_read(I2C0, I2C_ABORT_SOURCE);
            (void)reg_read(I2C0, I2C_CLR_INTR);
            return (source & 0x9) ? SENSOR_NACK : SENSOR_TIMEOUT;
        }
        if (!received && (reg_read(I2C0, I2C_STATUS) & I2C_RFNE)) {
            *value = reg_read(I2C0, I2C_DATA_CMD);
            received = 1;
        }
        if (received && (irq & I2C_STOP_DET)) {
            (void)reg_read(I2C0, I2C_CLR_INTR);
            return SENSOR_ID_OK;
        }
        delay_us(1000);
    }
    return SENSOR_TIMEOUT;
}

enum result probe_gc1084(void)
{
    uint32_t apb_rate;
    if (!configure_clock(&apb_rate))
        return SENSOR_CLOCK;
    configure_pins();
    uint32_t old_gate = reg_read(CPM, CLKGR);
    reg_write(CPM, CLKGR, old_gate & ~I2C_GATE);
    enum result result = SENSOR_TIMEOUT;
    if (configure_i2c(apb_rate)) {
        uint8_t high = 0, low = 0;
        result = read_sensor(0x03f0, &high);
        if (result == SENSOR_ID_OK)
            result = read_sensor(0x03f1, &low);
        if (result == SENSOR_ID_OK && (high != 0x10 || low != 0x84))
            result = SENSOR_WRONG_ID;
        if (result == SENSOR_ID_OK)
            puts("QC5_GC1084_ID 1084");
    }
    if (!i2c_enable(0) && result == SENSOR_ID_OK)
        result = SENSOR_TIMEOUT;
    reg_write(CPM, CLKGR, (reg_read(CPM, CLKGR) & ~I2C_GATE) | (old_gate & I2C_GATE));
    return result;
}

#ifndef QC5_SENSOR_TEST
static enum result map_sensor_registers(int fd)
{
    static const off_t bases[] = {0x10000000, 0x10010000, 0x10050000};
    for (unsigned i = 0; i < 3; ++i) {
        regs[i] = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, bases[i]);
        if (regs[i] == MAP_FAILED)
            return SENSOR_MAP;
    }
    return SENSOR_ID_OK;
}

#ifndef QC5_SENSOR_NO_MAIN
int main(void)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0)
        return SENSOR_MAP;
    enum result result = map_sensor_registers(fd);
    close(fd);
    return result == SENSOR_ID_OK ? probe_gc1084() : result;
}
#endif
#endif
