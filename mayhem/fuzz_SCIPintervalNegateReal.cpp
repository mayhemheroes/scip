#include <stdint.h>
#include <stdio.h>
#include <climits>

#include <fuzzer/FuzzedDataProvider.h>
#include "scip.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FuzzedDataProvider provider(data, size);
    SCIP_Real x = provider.ConsumeFloatingPoint<SCIP_Real>();
    SCIPintervalNegateReal(x);

    return 0;
}