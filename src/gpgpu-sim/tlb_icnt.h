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
    bool busy();
    bool ready(unsigned to, unsigned long long time);
    mem_fetch *recv(unsigned to);
    mem_fetch *recv_probe(unsigned to)
    {
        auto &q = send_queue[to];
        if (q.empty())
            throw std::runtime_error("Queue empty!");
        auto ret = q.front().first;
        //total_inside--;
        assert(total_inside >= 0);
        assert(ret);
        //q.pop();
        return ret;
    }

private:
    queue_set send_queue;
    unsigned queue_size;
    unsigned latency;
    unsigned total_inside;
};

extern tlb_icnt *global_tlb_icnt;

#endif