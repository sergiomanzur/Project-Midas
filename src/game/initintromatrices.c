#include <ultra64.h>
#include <memp.h>
#include "title.h"
#include "initintromatrices.h"

void alloc_intro_matrices(void)
{
    D_8002A7D0 = 0;
    matrixBufferRareLogo0     = mempAllocBytesInBank(0x80, MEMPOOL_STAGE);
    matrixBufferGunbarrel0    = mempAllocBytesInBank(0x40, MEMPOOL_STAGE);
    matrixBufferRareLogo1     = mempAllocBytesInBank(0x80, MEMPOOL_STAGE);
    matrixBufferRareLogo2     = mempAllocBytesInBank(0x80, MEMPOOL_STAGE);
    matrixBufferGunbarrel1    = mempAllocBytesInBank(0x80, MEMPOOL_STAGE);
    matrixBufferIntroBackdrop = mempAllocBytesInBank(0x80, MEMPOOL_STAGE);
    matrixBufferIntroBond     = mempAllocBytesInBank(0x80, MEMPOOL_STAGE);
    {
        extern void portConLogf(const char *fmt, ...);
        portConLogf("ge: intromtx logo0=%p g0=%p logo1=%p logo2=%p g1=%p bd=%p bond=%p\n",
                (void *) matrixBufferRareLogo0, (void *) matrixBufferGunbarrel0,
                (void *) matrixBufferRareLogo1, (void *) matrixBufferRareLogo2,
                (void *) matrixBufferGunbarrel1, (void *) matrixBufferIntroBackdrop,
                (void *) matrixBufferIntroBond);
    }
}


