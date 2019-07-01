#ifndef TLB_ICNT_H
#define TLB_ICNT_H
#include <vector>
#include <queue>
#include <utility>
#include "mem_fetch.h"

class tlb_icnt
{
public:
    using queue_element = std::pair<mem_fetch *, unsigned long long>;
    using latency_queue = std::queue<queue_element>;
    using queue_set = std::vector<latency_queue>;

    tlb_icnt(unsigned num_queues, unsigned queue_size, unsigned latency);
    bool free(unsigned from, unsigned to);
    void send(unsigned from, unsigned to, mem_fetch *mf, unsigned long long time);

    bool ready(unsigned to, unsigned long long time);
    mem_fetch *recv(unsigned to);

private:
    queue_set send_queue;
    unsigned queue_size;
    unsigned latency;
};

extern tlb_icnt *global_tlb_icnt;

#endif