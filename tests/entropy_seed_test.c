/* Public dummy seeds and mock storage: tests the Mbed TLS NV contract only.
 * This is not a Wii storage/provisioning implementation or a secret seed. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <mbedtls/entropy.h>
#include <mbedtls/platform.h>
#include <mbedtls/ctr_drbg.h>
static unsigned char seed[MBEDTLS_ENTROPY_BLOCK_SIZE];
static unsigned stored_length, reads, writes;
static int read_failure, write_failure;
static int read_seed(unsigned char *out, size_t n) {
    ++reads;
    /* The API checks only negativity, so callbacks must reject short I/O. */
    if (read_failure || n != stored_length) return -1;
    memcpy(out, seed, n);
    return (int)n;
}
static int write_seed(unsigned char *in, size_t n) {
    ++writes;
    /* Mock short write, close or commit failure as the required negative result. */
    if (write_failure || n != sizeof seed) return -1;
    memcpy(seed, in, n);
    return (int)n;
}
static int extract(unsigned char *out) {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    int result;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    assert(entropy.MBEDTLS_PRIVATE(source_count) == 1); /* NV only; no fake source */
    result=mbedtls_ctr_drbg_seed(&drbg,mbedtls_entropy_func,&entropy,
                                (const unsigned char *)"public fixture",14);
    if (!result) {
        assert(writes > 0); /* Rotation completes before application output. */
        result=mbedtls_ctr_drbg_random(&drbg,out,32);
    }
    /* Never retry an entropy context after a failed NV update. */
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return result;
}
int main(void) {
    unsigned char a[32],b[32],old[sizeof seed];
    unsigned i;
    assert(!mbedtls_platform_set_nv_seed(read_seed,write_seed));
    for(i=0;i<sizeof seed;i++)seed[i]=(unsigned char)i;
    stored_length=sizeof seed;memcpy(old,seed,sizeof seed);
    assert(!extract(a));assert(reads && writes==1);assert(memcmp(seed,old,sizeof seed));
    assert(!extract(b));assert(writes==2);assert(memcmp(a,b,sizeof a));
    stored_length=0;assert(extract(b)!=0); /* missing */
    stored_length=sizeof seed-1;assert(extract(b)!=0); /* truncated */
    stored_length=sizeof seed;read_failure=1;assert(extract(b)!=0);read_failure=0;
    memcpy(old,seed,sizeof seed);write_failure=1;
    /* Every storage failure must fail the whole new context. */
    for(i=0;i<4;i++) {
        memset(b,0xa5,sizeof b);assert(extract(b)!=0);
        assert(memcmp(seed,old,sizeof seed)==0);
        for(unsigned j=0;j<sizeof b;j++)assert(b[j]==0xa5);
    }
    write_failure=0;assert(!extract(b));
    puts("entropy: NV-only rotation, missing/truncated/read/commit errors and context disposal pass (mock storage)");
    return 0;
}
