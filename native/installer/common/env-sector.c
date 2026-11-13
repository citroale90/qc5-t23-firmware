/* Exact 4 KiB ENV erase helper for cold first installation. */
typedef unsigned char u8;
typedef unsigned int u32;

struct transfer {
    const u8 *command;
    const u8 *send;
    u8 *receive;
    u32 count;
};

#ifdef QC5_ENV_ERASE
#define MODULE_NAME "qc5_env_sector"
#else
#define MODULE_NAME "qc5_sfc_probe"
#endif

struct kernel_module_layout {
    u8 prefix[12];
    char name[56];
    u8 padding[120];
    int (*init)(void);
    u8 before_exit[88];
    void (*exit)(void);
    u32 tail;
};

extern int init_module(void);
extern void cleanup_module(void);

__attribute__((used, section(".gnu.linkonce.this_module")))
struct kernel_module_layout module = {
    .name = MODULE_NAME,
    .init = init_module,
    .exit = cleanup_module,
};

__attribute__((used, section(".modinfo")))
static const char info[] =
    "license=GPL\0vermagic=3.10.14__isvp_pike_1.0__ preempt mod_unload MIPS32_R1 32BIT ";

static u32 word(u32 address)
{
    return *(volatile const u32 *)address;
}

#ifdef QC5_ENV_ERASE
typedef int (*driver_transfer)(void *, const struct transfer *);

__attribute__((used,section(".qc5.prefix")))
static const volatile u8 qc5_env_expected_prefix[16]="QC5ENVUNITPREFIX";

static int exchange(driver_transfer transfer, void *state, const u8 *command, u8 *reply, u32 size)
{
    struct transfer request = { .command = command, .receive = reply, .count = size };
    return transfer(state, &request) == (int)size;
}

/* Caller holds the original SFC mutex. The only erase address is 0x03c000. */
static int erase_environment(driver_transfer transfer, void *state)
{
    const u8 wren[5] = {0x06};
    const u8 wrdi[5] = {0x04};
    const u8 status_command[5] = {0x05};
    const u8 read_env[5] = {0x03, 0x03, 0xc0, 0x00, 0x00};
    const u8 erase[5] = {0x20, 0x03, 0xc0, 0x00, 0x00};
    u8 status = 0xff;
    u8 before[16];
    int ok;
    int (*wait_ready)(void *) = (int (*)(void *))0x801f32f4;
    if (word(0x801f32f4) != 0x27bdffa8 || word(0x801f32f8) != 0xafb10040)
        return 0;
    if (wait_ready(state) || !exchange(transfer, state, read_env, before, sizeof before))
        return 0;
    for (u32 i = 0; i < sizeof before; ++i)
        if (before[i] != qc5_env_expected_prefix[i])
            return 0;
    if (!exchange(transfer, state, wren, (u8 *)0, 0))
        return 0;
    ok = exchange(transfer, state, status_command, &status, 1) &&
         (status & 3) == 2 &&
         exchange(transfer, state, erase, (u8 *)0, 0) &&
         !wait_ready(state);
    if (!exchange(transfer, state, wrdi, (u8 *)0, 0) || !ok)
        return 0;
    if (!exchange(transfer, state, read_env, before, sizeof before))
        return 0;
    for (u32 i = 0; i < sizeof before; ++i)
        if (before[i] != 0xff)
            return 0;
    return 1;
}
#endif

int init_module(void)
{
    u32 state;
    u8 command = 0x9f;
    u8 id[3] = {0};
    /* Match the driver's JEDEC ioctl. A receive buffer at offset eight
     * selects the SFC read direction. */
    struct transfer request = { .command = &command, .receive = id, .count = sizeof id };
    int result;
    void (*lock)(void *) = (void (*)(void *))0x80313b28;
    void (*unlock)(void *) = (void (*)(void *))0x8031395c;
    int (*transfer)(void *, const struct transfer *) =
        (int (*)(void *, const struct transfer *))0x801f2a78;

    if (word(0x801f2a78) != 0x27bdffe0 || word(0x801f2a7c) != 0xafb00014 ||
        word(0x80313b28) != 0x27bdffe8 || word(0x80313b2c) != 0xafb00010 ||
        word(0x8031395c) != 0xc0820000 || word(0x80313960) != 0x24430001)
        return -19;
    state = word(0x8040a1ec);
    if (state < 0x80000000 || state > 0x81fff000 || state % 4 ||
        word(state + 0x10) != 0x8000 || (*(volatile const u8 *)(state + 0x260)) != 3)
        return -19;

    lock((void *)(state + 0x248));
    result = transfer((void *)state, &request);
    result = result == 3 && id[0] == 0x20 && id[1] == 0x40 && id[2] == 0x17;
#ifdef QC5_ENV_ERASE
    if (result)
        result = erase_environment(transfer, (void *)state);
#endif
    unlock((void *)(state + 0x248));

    return result ? 0 : -5;
}

void cleanup_module(void)
{
}
