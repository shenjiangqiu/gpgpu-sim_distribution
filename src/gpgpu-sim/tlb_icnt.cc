#include "tlb_icnt.h"

#include "mem_fetch.h"

tlb_icnt *global_tlb_icnt = []() { return new tlb_icnt(28, 40, 6); }();

bool tlb_icnt::free(unsigned int from, unsigned int to)
{
    return send_queue[to].size() < queue_size;
}
void tlb_icnt::send(unsigned int from, unsigned int to, mem_fetch *mf, unsigned long long time)
{
    mf->icnt_from = from;
    mf->icnt_to = to;
    total_inside++;
    send_queue[to].push(std::make_pair(mf, time));
}
bool tlb_icnt::ready(unsigned int to, unsigned long long time)
{
    auto &q = send_queue[to];
    if (q.empty())
        return false;
    else
        return (q.front().second + latency) <= (time); //be care when using minus betwee two unsigned number!!!
}

mem_fetch *tlb_icnt::recv(unsigned int to)
{
    auto &q = send_queue[to];
    if (q.empty())
        throw std::runtime_error("Queue empty!");
    auto ret = q.front().first;
    total_inside--;
    assert(total_inside>=0);
    assert(ret);
    q.pop();
    return ret;
}

tlb_icnt::tlb_icnt(unsigned num_queues, unsigned queue_size, unsigned latency) : queue_size(queue_size), latency(latency), total_inside(0)
{
    send_queue.resize(num_queues);
}
bool tlb_icnt::busy()
{
    return total_inside != 0;
}