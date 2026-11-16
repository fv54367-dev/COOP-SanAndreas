#pragma once

// One line every 60 s in CoopAndreas_mirror.log: private memory, streaming, pools, FPS, network entities.
// For long sessions where the FPS die: the trend of these lines shows what grows.
class CPerfWatch
{
public:
    static void Process();
};
