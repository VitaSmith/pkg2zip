#include "pkg2zip_aes.h"
#include "pkg2zip_utils.h"

#if defined(_MSC_VER)
#define PLATFORM_SUPPORTS_AESNI 1

#include <intrin.h>
static void get_cpuid(uint32_t level, uint32_t* arr)
{
    __cpuidex((int*)arr, level, 0);
}

#elif defined(__x86_64__) || defined(__i386__)
#define PLATFORM_SUPPORTS_AESNI 1

#include <cpuid.h>
static void get_cpuid(uint32_t level, uint32_t* arr)
{
    __cpuid_count(level, 0, arr[0], arr[1], arr[2], arr[3]);
}

#else
#define PLATFORM_SUPPORTS_AESNI 0
#endif

#if PLATFORM_SUPPORTS_AESNI
static int aes128_supported_x86()
{
    static int init = 0;
    static int supported;
    if (!init)
    {
        init = 1;

        uint32_t a[4];
        get_cpuid(0, a);

        if (a[0] >= 1)
        {
            get_cpuid(1, a);
            supported = ((a[2] & (1 << 9)) && (a[2] & (1 << 25)));
        }
    }
    return supported;
}

void aes128_init_x86(aes128_key* context, const uint8_t* key);
void aes128_ecb_encrypt_x86(const aes128_key* context, const uint8_t* input, uint8_t* output);
void aes128_ctr_xor_x86(const aes128_key* context, const uint8_t* iv, uint8_t* buffer, size_t size);
#endif

static const uint8_t rcon[] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36,
};

static const uint8_t Te[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static const uint32_t TE[256] = {
    0xc66363a5, 0xf87c7c84, 0xee777799, 0xf67b7b8d, 0xfff2f20d, 0xd66b6bbd, 0xde6f6fb1, 0x91c5c554,
    0x60303050, 0x02010103, 0xce6767a9, 0x562b2b7d, 0xe7fefe19, 0xb5d7d762, 0x4dababe6, 0xec76769a,
    0x8fcaca45, 0x1f82829d, 0x89c9c940, 0xfa7d7d87, 0xeffafa15, 0xb25959eb, 0x8e4747c9, 0xfbf0f00b,
    0x41adadec, 0xb3d4d467, 0x5fa2a2fd, 0x45afafea, 0x239c9cbf, 0x53a4a4f7, 0xe4727296, 0x9bc0c05b,
    0x75b7b7c2, 0xe1fdfd1c, 0x3d9393ae, 0x4c26266a, 0x6c36365a, 0x7e3f3f41, 0xf5f7f702, 0x83cccc4f,
    0x6834345c, 0x51a5a5f4, 0xd1e5e534, 0xf9f1f108, 0xe2717193, 0xabd8d873, 0x62313153, 0x2a15153f,
    0x0804040c, 0x95c7c752, 0x46232365, 0x9dc3c35e, 0x30181828, 0x379696a1, 0x0a05050f, 0x2f9a9ab5,
    0x0e070709, 0x24121236, 0x1b80809b, 0xdfe2e23d, 0xcdebeb26, 0x4e272769, 0x7fb2b2cd, 0xea75759f,
    0x1209091b, 0x1d83839e, 0x582c2c74, 0x341a1a2e, 0x361b1b2d, 0xdc6e6eb2, 0xb45a5aee, 0x5ba0a0fb,
    0xa45252f6, 0x763b3b4d, 0xb7d6d661, 0x7db3b3ce, 0x5229297b, 0xdde3e33e, 0x5e2f2f71, 0x13848497,
    0xa65353f5, 0xb9d1d168, 0x00000000, 0xc1eded2c, 0x40202060, 0xe3fcfc1f, 0x79b1b1c8, 0xb65b5bed,
    0xd46a6abe, 0x8dcbcb46, 0x67bebed9, 0x7239394b, 0x944a4ade, 0x984c4cd4, 0xb05858e8, 0x85cfcf4a,
    0xbbd0d06b, 0xc5efef2a, 0x4faaaae5, 0xedfbfb16, 0x864343c5, 0x9a4d4dd7, 0x66333355, 0x11858594,
    0x8a4545cf, 0xe9f9f910, 0x04020206, 0xfe7f7f81, 0xa05050f0, 0x783c3c44, 0x259f9fba, 0x4ba8a8e3,
    0xa25151f3, 0x5da3a3fe, 0x804040c0, 0x058f8f8a, 0x3f9292ad, 0x219d9dbc, 0x70383848, 0xf1f5f504,
    0x63bcbcdf, 0x77b6b6c1, 0xafdada75, 0x42212163, 0x20101030, 0xe5ffff1a, 0xfdf3f30e, 0xbfd2d26d,
    0x81cdcd4c, 0x180c0c14, 0x26131335, 0xc3ecec2f, 0xbe5f5fe1, 0x359797a2, 0x884444cc, 0x2e171739,
    0x93c4c457, 0x55a7a7f2, 0xfc7e7e82, 0x7a3d3d47, 0xc86464ac, 0xba5d5de7, 0x3219192b, 0xe6737395,
    0xc06060a0, 0x19818198, 0x9e4f4fd1, 0xa3dcdc7f, 0x44222266, 0x542a2a7e, 0x3b9090ab, 0x0b888883,
    0x8c4646ca, 0xc7eeee29, 0x6bb8b8d3, 0x2814143c, 0xa7dede79, 0xbc5e5ee2, 0x160b0b1d, 0xaddbdb76,
    0xdbe0e03b, 0x64323256, 0x743a3a4e, 0x140a0a1e, 0x924949db, 0x0c06060a, 0x4824246c, 0xb85c5ce4,
    0x9fc2c25d, 0xbdd3d36e, 0x43acacef, 0xc46262a6, 0x399191a8, 0x319595a4, 0xd3e4e437, 0xf279798b,
    0xd5e7e732, 0x8bc8c843, 0x6e373759, 0xda6d6db7, 0x018d8d8c, 0xb1d5d564, 0x9c4e4ed2, 0x49a9a9e0,
    0xd86c6cb4, 0xac5656fa, 0xf3f4f407, 0xcfeaea25, 0xca6565af, 0xf47a7a8e, 0x47aeaee9, 0x10080818,
    0x6fbabad5, 0xf0787888, 0x4a25256f, 0x5c2e2e72, 0x381c1c24, 0x57a6a6f1, 0x73b4b4c7, 0x97c6c651,
    0xcbe8e823, 0xa1dddd7c, 0xe874749c, 0x3e1f1f21, 0x964b4bdd, 0x61bdbddc, 0x0d8b8b86, 0x0f8a8a85,
    0xe0707090, 0x7c3e3e42, 0x71b5b5c4, 0xcc6666aa, 0x904848d8, 0x06030305, 0xf7f6f601, 0x1c0e0e12,
    0xc26161a3, 0x6a35355f, 0xae5757f9, 0x69b9b9d0, 0x17868691, 0x99c1c158, 0x3a1d1d27, 0x279e9eb9,
    0xd9e1e138, 0xebf8f813, 0x2b9898b3, 0x22111133, 0xd26969bb, 0xa9d9d970, 0x078e8e89, 0x339494a7,
    0x2d9b9bb6, 0x3c1e1e22, 0x15878792, 0xc9e9e920, 0x87cece49, 0xaa5555ff, 0x50282878, 0xa5dfdf7a,
    0x038c8c8f, 0x59a1a1f8, 0x09898980, 0x1a0d0d17, 0x65bfbfda, 0xd7e6e631, 0x844242c6, 0xd06868b8,
    0x824141c3, 0x299999b0, 0x5a2d2d77, 0x1e0f0f11, 0x7bb0b0cb, 0xa85454fc, 0x6dbbbbd6, 0x2c16163a,
};

static uint8_t byte32(uint32_t x, int n)
{
    return (uint8_t)(x >> (8 * n));
}

static uint32_t setup_mix(uint32_t temp)
{
    return (Te[byte32(temp, 2)] << 24) ^ (Te[byte32(temp, 1)] << 16) ^ (Te[byte32(temp, 0)] << 8) ^ Te[byte32(temp, 3)];
}

static uint32_t ror32(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

void aes128_init(aes128_key* context, const uint8_t* key)
{
#if PLATFORM_SUPPORTS_AESNI
    if (aes128_supported_x86())
    {
        aes128_init_x86(context, key);
        return;
    }
#endif

    uint32_t* rk = context->key;

    rk[0] = get32be(key +  0);
    rk[1] = get32be(key +  4);
    rk[2] = get32be(key +  8);
    rk[3] = get32be(key + 12);

    for (size_t i=0; i<10; i++)
    {
        uint32_t temp = rk[3];
        rk[4] = rk[0] ^ setup_mix(temp) ^ (rcon[i] << 24);
        rk[5] = rk[1] ^ rk[4];
        rk[6] = rk[2] ^ rk[5];
        rk[7] = rk[3] ^ rk[6];
        rk += 4;
    }
}

static void aes128_encrypt(const aes128_key* context, const uint8_t* input, uint8_t* output)
{
    uint32_t t0, t1, t2, t3;
    const uint32_t* key = context->key;

    uint32_t s0 = get32be(input + 0) ^ *key++;
    uint32_t s1 = get32be(input + 4) ^ *key++;
    uint32_t s2 = get32be(input + 8) ^ *key++;
    uint32_t s3 = get32be(input + 12) ^ *key++;

    for (size_t i = 0; i<4; i++)
    {
        t0 = TE[byte32(s0, 3)] ^ ror32(TE[byte32(s1, 2)], 8) ^ ror32(TE[byte32(s2, 1)], 16) ^ ror32(TE[byte32(s3, 0)], 24) ^ *key++;
        t1 = TE[byte32(s1, 3)] ^ ror32(TE[byte32(s2, 2)], 8) ^ ror32(TE[byte32(s3, 1)], 16) ^ ror32(TE[byte32(s0, 0)], 24) ^ *key++;
        t2 = TE[byte32(s2, 3)] ^ ror32(TE[byte32(s3, 2)], 8) ^ ror32(TE[byte32(s0, 1)], 16) ^ ror32(TE[byte32(s1, 0)], 24) ^ *key++;
        t3 = TE[byte32(s3, 3)] ^ ror32(TE[byte32(s0, 2)], 8) ^ ror32(TE[byte32(s1, 1)], 16) ^ ror32(TE[byte32(s2, 0)], 24) ^ *key++;
        s0 = TE[byte32(t0, 3)] ^ ror32(TE[byte32(t1, 2)], 8) ^ ror32(TE[byte32(t2, 1)], 16) ^ ror32(TE[byte32(t3, 0)], 24) ^ *key++;
        s1 = TE[byte32(t1, 3)] ^ ror32(TE[byte32(t2, 2)], 8) ^ ror32(TE[byte32(t3, 1)], 16) ^ ror32(TE[byte32(t0, 0)], 24) ^ *key++;
        s2 = TE[byte32(t2, 3)] ^ ror32(TE[byte32(t3, 2)], 8) ^ ror32(TE[byte32(t0, 1)], 16) ^ ror32(TE[byte32(t1, 0)], 24) ^ *key++;
        s3 = TE[byte32(t3, 3)] ^ ror32(TE[byte32(t0, 2)], 8) ^ ror32(TE[byte32(t1, 1)], 16) ^ ror32(TE[byte32(t2, 0)], 24) ^ *key++;
    }

    t0 = TE[byte32(s0, 3)] ^ ror32(TE[byte32(s1, 2)], 8) ^ ror32(TE[byte32(s2, 1)], 16) ^ ror32(TE[byte32(s3, 0)], 24) ^ *key++;
    t1 = TE[byte32(s1, 3)] ^ ror32(TE[byte32(s2, 2)], 8) ^ ror32(TE[byte32(s3, 1)], 16) ^ ror32(TE[byte32(s0, 0)], 24) ^ *key++;
    t2 = TE[byte32(s2, 3)] ^ ror32(TE[byte32(s3, 2)], 8) ^ ror32(TE[byte32(s0, 1)], 16) ^ ror32(TE[byte32(s1, 0)], 24) ^ *key++;
    t3 = TE[byte32(s3, 3)] ^ ror32(TE[byte32(s0, 2)], 8) ^ ror32(TE[byte32(s1, 1)], 16) ^ ror32(TE[byte32(s2, 0)], 24) ^ *key++;

    s0 = (Te[byte32(t0, 3)] << 24) ^ (Te[byte32(t1, 2)] << 16) ^ (Te[byte32(t2, 1)] << 8) ^ (Te[byte32(t3, 0)]) ^ *key++;
    s1 = (Te[byte32(t1, 3)] << 24) ^ (Te[byte32(t2, 2)] << 16) ^ (Te[byte32(t3, 1)] << 8) ^ (Te[byte32(t0, 0)]) ^ *key++;
    s2 = (Te[byte32(t2, 3)] << 24) ^ (Te[byte32(t3, 2)] << 16) ^ (Te[byte32(t0, 1)] << 8) ^ (Te[byte32(t1, 0)]) ^ *key++;
    s3 = (Te[byte32(t3, 3)] << 24) ^ (Te[byte32(t0, 2)] << 16) ^ (Te[byte32(t1, 1)] << 8) ^ (Te[byte32(t2, 0)]) ^ *key++;

    set32be(output + 0, s0);
    set32be(output + 4, s1);
    set32be(output + 8, s2);
    set32be(output + 12, s3);
}

void aes128_ecb_encrypt(const aes128_key* context, const uint8_t* input, uint8_t* output)
{
#if PLATFORM_SUPPORTS_AESNI
    if (aes128_supported_x86())
    {
        aes128_ecb_encrypt_x86(context, input, output);
        return;
    }
#endif
    aes128_encrypt(context, input, output);
}

static void ctr_add(uint8_t* counter, uint64_t n)
{
    for (int i=15; i>=0; i--)
    {
        n = n + counter[i];
        counter[i] = (uint8_t)n;
        n >>= 8;
    }
}

void aes128_ctr_xor(const aes128_key* context, const uint8_t* iv, uint64_t block, uint8_t* buffer, size_t size)
{
    uint8_t tmp[16];
    uint8_t counter[16];
    for (uint32_t i=0; i<16; i++)
    {
        counter[i] = iv[i];
    }
    ctr_add(counter, block);

#if PLATFORM_SUPPORTS_AESNI
    if (aes128_supported_x86())
    {
        aes128_ctr_xor_x86(context, counter, buffer, size);
        return;
    }
#endif

    while (size >= 16)
    {
        aes128_encrypt(context, counter, tmp);
        for (uint32_t i=0; i<16; i++)
        {
            *buffer++ ^= tmp[i];
        }
        ctr_add(counter, 1);
        size -= 16;
    }

    if (size != 0)
    {
        aes128_encrypt(context, counter, tmp);
        for (size_t i=0; i<size; i++)
        {
            *buffer++ ^= tmp[i];
        }
    }
}
