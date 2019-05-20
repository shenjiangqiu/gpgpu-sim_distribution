#include "l2_tlb.hpp"
#include "gpu-cache.h"
#include "icnt_wrapper.h"
#include<memory>
#include <deque>
#define SJQDEBUG
extern unsigned long long  gpu_sim_cycle;
extern unsigned long long  gpu_tot_sim_cycle;

l2_tlb::l2_tlb(l2_tlb_config config,
               std::shared_ptr<page_manager> tlb_page_manager) : m_page_table_walker(new page_table_walker(m_config.m_pw_size, m_config.m_pw_latency)),
                                       m_config(config),
                                       m_page_manager(tlb_page_manager),
                                       m_mshrs(std::make_shared<mshr_table>(m_config.n_mshr_entries, m_config.n_mshr_max_merge)),
                                       m_tag_arrays(m_config.n_sets * m_config.n_associate)
{
    for (int i = 0; i < m_config.n_sets * m_config.n_associate; i++)
    {
        m_tag_arrays[i] = std::make_shared<line_cache_block>();
    }
}
l2_tlb_config::l2_tlb_config() {} //in constructor, just allocate the memory, and then parse configur,then

void l2_tlb_config::init()
{
    assert(m_page_size > 0);
    if (m_page_size == 4096)
        m_page_size_log2 = 12;
    else
    {
        throw std::runtime_error("can't accept other page size now!");
    }
}
void l2_tlb_config::reg_option(option_parser_t opp)
{

    option_parser_register(opp, "-l2tlbsets", option_dtype::OPT_INT32, &n_sets, "the sets of l2 tlb", "64");
    option_parser_register(opp, "-l2tlbassoc", option_dtype::OPT_UINT32, &n_associate, "the set associate", "2");
    option_parser_register(opp, "-l2tlb_mshr_entries", option_dtype::OPT_UINT32, &n_mshr_entries, "the mshr size", "16");
    option_parser_register(opp, "-l2tlb_mshr_maxmerge", option_dtype::OPT_UINT32, &n_mshr_max_merge, "the max merge size", "8");
    option_parser_register(opp, "-l2tlb_response_queue_size", option_dtype::OPT_UINT32, &response_queue_size, "the response queue size 0=unlimited", "0");
    option_parser_register(opp, "-l2tlb_miss_queue_size", option_dtype::OPT_UINT32, &miss_queue_size, "the miss queue size 0=unlimited", "0");
    option_parser_register(opp, "-l2tlb_page_size", option_dtype::OPT_UINT32, &m_page_size, "the page size", "4096");
    option_parser_register(opp, "-l2tlb_pw_size", option_dtype::OPT_UINT32, &m_pw_size, "the size of pw size", "16");
    option_parser_register(opp, "-l2tlb_pw_latency", option_dtype::OPT_UINT32, &m_pw_latency, "the latency of pw ", "500");
}
void l2_tlb::init()
{

}

bool l2_tlb::reponse_empty()
{
    return m_response_queue.empty();
}

void l2_tlb::pop_response()
{
    assert(!m_response_queue.empty() && "the m_response_queue can't be empty");
    m_response_queue.pop_front();
}
mem_fetch *l2_tlb::get_top_response()
{
    assert(!m_response_queue.empty() && "the m_response_queue can't be empty");
    return m_response_queue.back();
}
void l2_tlb_config::set_icnt_index(unsigned int idx){
    m_icnt_index=idx;
}
tlb_result l2_tlb::access(mem_fetch *mf, unsigned time)
{
    if (mf == nullptr)
    {
        throw std::runtime_error( "accessed mf cann't be null");
    }
    else
    {
        /* code */
        auto addr = mf->get_addr();
        auto v_addr = m_page_manager->get_vir_addr(addr);
        auto m_page_size_log2 = m_config.m_page_size_log2;
        auto n_set = m_config.n_sets;
        auto n_assoc = m_config.n_associate;

        // get the set_index and tag for searching the tag array.
        auto set_index = (v_addr >> m_page_size_log2) & (n_set - 1); //first get the page number, then get the cache index.
        auto tag = v_addr & (~m_config.m_page_size - 1);            //only need the bits besides page offset
        auto block_addr = v_addr >> m_page_size_log2;
        auto start = m_tag_arrays.begin() + n_assoc * set_index;
        auto end = start + n_assoc;
        //to find a place to access
        auto has_free_line = false;
        auto find_line = false;

        unsigned long long oldest_access_time = time;
        decltype(start) free_line;
        decltype(start) hit_line;
        decltype(start) last_line;
        auto mask = mf->get_access_sector_mask();
        for (; start < end; start++)
        {
            if ((*start)->is_valid_line())
            {
                if ((*start)->get_last_access_time() < oldest_access_time)
                {
                    oldest_access_time = (*start)->get_last_access_time();
                    last_line = start;
                }
                if ((*start)->m_tag == tag)//ok we find
                {

                    find_line = true;
                    auto status = (*start)->get_status(mask);
                    switch (status)
                    {
                    case RESERVED:
                        if (m_mshrs->full(block_addr))
                        {
                            return tlb_result::resfail;
                        }
                        else
                        {
                            m_mshrs->add(block_addr, mf);
                            (*start)->set_last_access_time(time, mask);
                            return tlb_result::hit_reserved;
                        }
                        break;
                    case VALID:
                        #ifdef SJQDEBUG
                        std::cout << "push to response queu: mf:" << mf->get_addr() << '\n';
                        #endif// SJQDEBUG
                        m_response_queue.push_front(mf); //only at this time ,we need push front, and we can pop front now.
                        return tlb_result::hit;

                        break;

                    case MODIFIED:
                        throw std::runtime_error("tlb cache can't be modified, it's read only!");
                    default:
                        throw std::runtime_error("error");
                        break;
                    }
                    break;
                }
            }
            else
            { //any time find the valid line, get it!
                if (has_free_line == false)
                {
                    has_free_line = true;
                    free_line = start;
                }
            }
        }
        // when run to here, means no hit line found,It's a miss;
        if (m_mshrs->full(block_addr) || (m_config.miss_queue_size>0&& m_miss_queue.size() >= m_config.miss_queue_size))
        {
            return tlb_result::resfail;
        }
        auto next_line = has_free_line ? free_line : last_line;
        (*next_line)->allocate(tag, block_addr, time, mask);
        m_mshrs->add(block_addr, mf);
        m_miss_queue.push_back(mf);
        outgoing_mf.insert(mf);
    }
}
bool l2_tlb::is_outgoing(mem_fetch *mf)
{
    return (outgoing_mf.find(mf) != outgoing_mf.end());
}
void l2_tlb::del_outgoing(mem_fetch *mf)
{
    outgoing_mf.erase(mf);
}
void l2_tlb::cycle()
{
    if(!m_response_queue.empty()){//from response queue to icnt
        auto mf=m_response_queue.front();
        auto size=mf->get_ctrl_size()+mf->get_data_size();
        if(::icnt_has_buffer(m_config.m_icnt_index,size)){
            ::icnt_push(m_config.m_icnt_index,mf->get_tpc(),mf,size);
            m_response_queue.pop_front();
        }
    }
    while (m_mshrs->access_ready())//from mshr to response queue
    { //push all the ready access to response Queue
        m_response_queue.push_back(m_mshrs->next_access());
    }
    while(m_page_table_walker->ready()){//from page_table_walker to l2 tlb
        auto mf=m_page_table_walker->recv();
        fill(mf,gpu_sim_cycle+gpu_tot_sim_cycle);
    }
    if (!m_miss_queue.empty())//from l2tlb to page walker/different from l1 tlb
    { //send the request to 
        auto mf = m_miss_queue.front();
        auto size = 40;
        if(m_page_table_walker->free()){
            m_page_table_walker->send(mf);
            m_miss_queue.pop_front();
        }
    }
    mem_fetch* mf=static_cast<mem_fetch*>( ::icnt_pop(m_config.m_icnt_index));
    if(mf){//from icnt to l2 tlb
        auto result=access(mf,gpu_sim_cycle+gpu_tot_sim_cycle);//this call will automatically add mf to miss queue, response queue or mshr
        switch (result)
        {
        case tlb_result::hit:
            /* code */

            break;
        
        default:
            break;
        }
    }

    m_page_table_walker->cycle();
    
}
void l2_tlb::fill(mem_fetch *mf, unsigned long long time)//that will be called from l2_tlb.cycle
{
    auto addr = mf->get_addr();
    auto v_addr = m_page_manager->get_vir_addr(addr);
    auto m_page_size_log2 = m_config.m_page_size_log2;
    auto n_set = m_config.n_sets;
    auto n_assoc = m_config.n_associate;

    // get the set_index and tag for searching the tag array.
    auto set_index = (v_addr >> m_page_size_log2) & (n_set - 1); //first get the page number, then get the cache index.
    auto tag = v_addr & (~m_config.m_page_size - 1);            //only need the bits besides page offset
    auto block_addr = v_addr >> m_page_size_log2;
    bool has_atomic;
    m_mshrs->mark_ready(block_addr, has_atomic);
    auto start = m_tag_arrays.begin() + set_index * n_assoc;
    auto end = start + n_assoc;
    auto mask = mf->get_access_sector_mask();
    auto done=false;
    for (; start < end; start++)
    {
        if ((*start)->is_valid_line())
        {
            if ((*start)->m_tag == tag)
            {
                assert((*start)->get_status(mask) == RESERVED);
                (*start)->fill(time, mask);
                done=true;
                break;
            }
        }
    }
    assert(done);
    return;
}