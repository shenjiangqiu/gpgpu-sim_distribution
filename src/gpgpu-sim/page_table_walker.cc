#include "page_table_walker.hpp"
#include<unordered_map>
//#define TLBDEBUG
#include"debug_macro.h"

extern unsigned long long  gpu_sim_cycle;
extern unsigned long long  gpu_tot_sim_cycle;
latency_queue::latency_queue(unsigned long long latency, unsigned size) : m_size_limit(size),
                                                                          m_latency(latency)
{
}

bool latency_queue::add(mem_fetch* v, unsigned long long time)
{
    if (m_elements.size() >= m_size_limit)//TODO what is size limit
    {
        throw std::runtime_error("can't exceed the limit of pagewalker size\n");
    }
    printdbg_tlb("l2 pw add,mf mf: %p,addr:%llX\n",v,v->get_addr());

    auto temp = std::make_pair(time + m_latency, v);
    
    m_elements.push(temp);
    return true;
}
bool latency_queue::ready(unsigned long long time)
{
    if(m_elements.empty()){
        return false;
    }
    auto front = m_elements.front();
    if (time >= front.first)
    {
        return true;
    }
    else
    {
        return false;
    }
}
mem_fetch* latency_queue::get()
{
    assert(!m_elements.empty());
    auto v=m_elements.front().second;
    //printdbg_tlb("l2 pw get,mf iD: %u,mf:%llX\n",v->sm_next_mf_request_uid,v->get_addr());
    return v;
}
//extern std::unordered_map<mem_fetch*,unsigned long long> mf_map;
mem_fetch* latency_queue::pop()
{
    assert(!m_elements.empty());
    auto v = m_elements.front().second;
    printdbg_tlb("pop: mf %p\n",v);
    #ifdef TLBDEBUG
    fflush(stdout);
    fflush(stderr);
    #endif
    //printdbg_tlb("l2 pw pop,mf iD: %u,mf:%llX\n",v->sm_next_mf_request_uid,v->get_addr());
    m_elements.pop();
    return v;
}
page_table_walker::page_table_walker(unsigned int size, unsigned int latency):
m_latency_queue(latency,size)
{

}
bool latency_queue::free(){
    return m_elements.size()<m_size_limit;
}

bool page_table_walker::free(){
    return m_latency_queue.free();
}
bool page_table_walker::ready(){
    return !m_response_queue.empty();
}
void page_table_walker::cycle(){
    while(m_latency_queue.ready(gpu_sim_cycle+gpu_tot_sim_cycle)){
        //auto temp=m_latency_queue.get();
        auto temp= m_latency_queue.pop();
        m_response_queue.push(temp);
    }
}
bool page_table_walker::send(mem_fetch  *mf){
    return m_latency_queue.add(mf,gpu_sim_cycle+gpu_tot_sim_cycle);
}
mem_fetch *page_table_walker::recv(){
    assert(!m_response_queue.empty());
    auto temp=m_response_queue.front();
    m_response_queue.pop();
    return temp;
}
mem_fetch *page_table_walker::recv_probe(){
    assert(!m_response_queue.empty());
    return m_response_queue.front();
}