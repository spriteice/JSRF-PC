
/* Every method address up to the end of the 3D class, each with a few values,
 * outside Begin/End and inside it: a line with whether imm_method handled it
 * and a hash of what it left (the attributes and any vertex it sent). */
static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    while (n--) { h ^= *b++; h *= 0x100000001B3ull; }
    return h;
}

int main(void)
{
    static const uint32_t params[] = { 0x3F800000u, 0xBF000000u, 0x12345678u, 0x8001FFFFu };
    uint32_t method, prim, k;
    for (prim = 0; prim <= 5; prim += 5)
        for (method = 0; method < 0x2000; method += 4)
            for (k = 0; k < sizeof params / sizeof params[0]; k++) {
                uint64_t h = 0xCBF29CE484222325ull;
                int r;
                memset(&S, 0, sizeof S);
                S.prim = prim;
                g_imm_n = 0;
                r = imm_method(method, params[k]);
                h = fnv(h, &S, sizeof S);
                h = fnv(h, &g_imm_n, sizeof g_imm_n);
                if (g_imm_n) h = fnv(h, g_imm[0], sizeof g_imm[0]);
                printf("%u %04X %08X %d %016llX\n", prim, method, params[k], r,
                       (unsigned long long)h);
            }
    return 0;
}
