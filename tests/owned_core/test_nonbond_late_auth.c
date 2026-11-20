/* Normal overlapping authentication completion for each supported bonding policy. */
#define main normal_ssp_full_fixture_main
#include "test_normal_ssp_review.c"
#undef main
int main(void) {
    const uint8_t policies[] = {0, 2, 4};
    for (unsigned n = 0; n < sizeof policies; ++n) {
        const uint8_t requirement = policies[n];
        begin(requirement, 4, true);
        CHECK(pairing(0) == BTH_OK);
        fresh_key(4); encryption(1); CHECK(size_result(0, 16) == BTH_OK);
        CHECK(bth_links_secure(&links, 0x25));
        CHECK(saves == (requirement ? 1u : 0u));
        printf("before_late_auth policy=%u secure=1 bonded=%u\n", requirement, links.classic.bonded);
        const uint8_t authenticated[] = {0, 0x25, 0};
        CHECK(event(6, authenticated, sizeof authenticated) == BTH_OK);
        CHECK(bth_links_secure(&links, 0x25) && !links.classic.disconnecting && commands.used == 0);
        CHECK(saves == (requirement ? 1u : 0u));
    }
    printf("NONBOND_LATE_AUTH_PASS cases=%u checks=%u synthetic_only=1\n", cases, checks);
    return 0;
}
