#include "page_table_walker.hpp"
extern unsigned long long  gpu_sim_cycle;
extern unsigned long long  gpu_tot_sim_cycle;
latency_queue::latency_queue(unsigned long long latency, unsigned size) : m_latency(latency),
                                                                               m_size_limit(size)
{
}

bool latency_queue::add(mem_fetch* v, unsigned long long time)
{
    if (m_elements.size() >= m_size_limit)//TODO what is size limit
    {
        return false;
    }
    auto temp = std::make_pair(time + m_latency, v);
    m_elements.push(temp);
    return true;
}
bool latency_queue::ready(unsigned long long time)
{
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
    return m_elements.front().second;
}
mem_fetch* latency_queue::pop()
{
    auto temp = m_elements.front().second;
    m_elements.pop();
    return temp;
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
    auto temp=m_response_queue.front();
    m_response_queue.pop();
    return temp;
}
mem_fetch *page_table_walker::recv_probe(){
    return m_response_queue.front();
}