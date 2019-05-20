#ifndef PAGE_TABALE_WALKER_HPP
#define PAGE_TABALE_WALKER_HPP
#include"mem_fetch.h"
#include <queue>
#include <tuple>
class latency_queue
{
public:
    latency_queue(unsigned long long latency, unsigned size);
    bool add(mem_fetch* v, unsigned long long current_time);//can be failed
    bool ready(unsigned long long time);
    mem_fetch* get();
    mem_fetch* pop();
    bool free();

private:
    unsigned m_size_limit;
    unsigned long long m_latency;
    std::queue<std::pair<unsigned long long, mem_fetch*>> m_elements;
};

class page_table_walker
{
public:
    page_table_walker(unsigned size, unsigned latency);
    virtual bool free();//can accecpt new request
    virtual bool ready();//resonse_queue is ready to searve
    virtual void cycle();//put all the valiable request to response_queue
    virtual bool send(mem_fetch *mf);
    //virtual bool recv_ready();
    mem_fetch *recv();//recv and pop;
    mem_fetch *recv_probe();//recv not

private:
    latency_queue m_latency_queue;
    std::queue<mem_fetch* > m_response_queue;
};

#endif