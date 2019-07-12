#include "l2_tlb.hpp"
#include"tlb_icnt.h"
#include "gpu-cache.h"
#include "icnt_wrapper.h"
#include <memory>
#include <deque>
//#define TLBDEBUG
#define PWDEBUG
#define TLBDEBUG
#include "debug_macro.h"
extern unsigned long long gpu_sim_cycle;
extern unsigned long long gpu_tot_sim_cycle;
using std::cout;
using std::endl;

unsigned global_l2_tlb_index;
unsigned global_n_cores;
unsigned global_walkers;


l2_tlb::l2_tlb(l2_tlb_config config) : m_config(config),
                                       m_page_table_walker(new real_page_table_walker(config.m_page_table_walker_config)),
                                       m_page_manager(global_page_manager),
                                       m_mshrs(new mshr_table(m_config.n_mshr_entries, m_config.n_mshr_max_merge)),
                                       m_tag_arrays(m_config.n_sets * m_config.n_associate), //null shared point
                                       access_times(0),
                                       hit_times(0),
                                       miss_times(0),
                                       resfail_times(0)
{
    for (unsigned i = 0; i < m_config.n_sets * m_config.n_associate; i++)
    {
        m_tag_arrays[i] = new line_cache_block;
    }
    global_l2_tlb_index = m_config.m_icnt_index;
    global_walkers = m_config.m_page_table_walker_config.walker_size;
}
l2_tlb_config::l2_tlb_config() {} //in constructor, just allocate the memory, and then parse configur,then

void l2_tlb_config::init()
{
    m_page_table_walker_config.init();
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
    m_page_table_walker_config.reg_option(opp);
    option_parser_register(opp, "-l2tlbsets", option_dtype::OPT_INT32, &n_sets, "the sets of l2 tlb", "32");
    option_parser_register(opp, "-l2tlbassoc", option_dtype::OPT_UINT32, &n_associate, "the set associate", "16");
    option_parser_register(opp, "-l2tlb_mshr_entries", option_dtype::OPT_UINT32, &n_mshr_entries, "the mshr size", "32");
    option_parser_register(opp, "-l2tlb_mshr_maxmerge", option_dtype::OPT_UINT32, &n_mshr_max_merge, "the max merge size", "32");
    option_parser_register(opp, "-l2tlb_response_queue_size", option_dtype::OPT_UINT32, &response_queue_size, "the response queue size 0=unlimited", "40");
    option_parser_register(opp, "-l2tlb_miss_queue_size", option_dtype::OPT_UINT32, &miss_queue_size, "the miss queue size 0=unlimited", "0");
    option_parser_register(opp, "-l2tlb_page_size", option_dtype::OPT_UINT32, &m_page_size, "the page size", "4096");
    option_parser_register(opp, "-l2tlb_pw_size", option_dtype::OPT_UINT32, &m_pw_size, "the size of pw size", "16");
    option_parser_register(opp, "-l2tlb_pw_latency", option_dtype::OPT_UINT32, &m_pw_latency, "the latency of pw ", "500");
    option_parser_register(opp, "-l2tlb_recv_buffer_size", option_dtype::OPT_UINT32, &recv_buffer_size, "the size of recv buffer from icnt", "1000");
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
void l2_tlb_config::set_icnt_index(unsigned int idx)
{
    m_icnt_index = idx;
}
tlb_result l2_tlb::access(mem_fetch *mf, unsigned time)
{
    if (mf == nullptr)
    {
        throw std::runtime_error("accessed mf cann't be null");
    }
    else
    {
        /* code */
        
        printdbg_PW("the entry:1:tag:%llx,2:tag:%llx\n", m_tag_arrays[61 * 2] ? m_tag_arrays[61 * 2]->m_tag : 0, m_tag_arrays[61 * 2] ? m_tag_arrays[61 * 2 + 1]->m_tag : 0);
        access_times++;
        auto v_addr = mf->get_virtual_addr();
        auto m_page_size_log2 = m_config.m_page_size_log2;
        auto n_set = m_config.n_sets;
        auto n_assoc = m_config.n_associate;

        // get the set_index and tag for searching the tag array.
        auto set_index = (v_addr >> m_page_size_log2) & static_cast<addr_type>(n_set - 1); //first get the page number, then get the cache index.
        auto tag = v_addr & (~static_cast<addr_type>(m_config.m_page_size - 1));           //only need the bits besides page offset
        auto block_addr = v_addr >> m_page_size_log2;
        auto start = m_tag_arrays.begin() + n_assoc * set_index;
        auto end = start + n_assoc;
        //to find a place to access
        auto has_free_line = false;

        unsigned long long oldest_access_time = time;
        decltype(start) free_line;
        decltype(start) hit_line;
        decltype(start) last_line;
        auto mask = mf->get_access_sector_mask();
        bool all_reserved = true;
        for (; start < end; start++)
        {
            if (!(*start)->is_invalid_line())
            {
                auto status = (*start)->get_status(mask);
                if (status != RESERVED)
                {
                    assert(status == VALID);
                    all_reserved = false;
                    if ((*start)->get_last_access_time() < oldest_access_time)
                    {
                        oldest_access_time = (*start)->get_last_access_time();
                        last_line = start;
                    }
                }
                else
                {
                    assert(time - (*start)->get_alloc_time() < 5000);
                }

                if ((*start)->m_tag == tag) //ok we find// this is what the identity entry/try to find that entry
                {

                    auto status = (*start)->get_status(mask);
                    switch (status)
                    {
                    case RESERVED:
                        if (m_mshrs->full(block_addr))
                        {
                            printdbg_tlb("reserved! and mshr full\n");
                            resfail_times++;
                            return tlb_result::resfail;
                        }
                        else
                        {
                            printdbg_tlb("hit reserved! add to mfshr\n");
                            m_mshrs->add<2>(block_addr, mf); //not new
                            (*start)->set_last_access_time(time, mask);
                            miss_times++;
                            return tlb_result::hit_reserved;
                        }
                        break;
                    case VALID:

                        all_reserved = false;
                        printdbg_tlb("push to response queu: mf:%llX\n", mf->get_virtual_addr());
                        if(m_config.response_queue_size!=0 and m_config.response_queue_size<=m_response_queue.size()){
                            resfail_times++;
                            return tlb_result::resfail;
                        }
                        m_response_queue.push_front(mf); //only at this time ,we need push front, and we can pop front now.
                        
                        //assert(m_response_queue.size() < 100);
                        hit_times++;
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
                all_reserved = false;
                if (has_free_line == false)
                {
                    has_free_line = true;
                    free_line = start;
                }
            }
        }
        // when run to here, means no hit line found,It's a miss;
        if (all_reserved)
        {
            resfail_times++;
            return tlb_result::resfail;
        }
        if (m_mshrs->full(block_addr) || (m_config.miss_queue_size > 0 && m_miss_queue.size() >= m_config.miss_queue_size))
        {
            resfail_times++;
            return tlb_result::resfail;
        }
        auto next_line = has_free_line ? free_line : last_line;
        (*next_line)->allocate(tag, block_addr, time, mask);
        printdbg_PW("the entry:1:tag:%llx,2:tag:%llx\n", m_tag_arrays[61 * 2] ? m_tag_arrays[61 * 2]->m_tag : 0, m_tag_arrays[61 * 2] ? m_tag_arrays[61 * 2 + 1]->m_tag : 0);

        m_mshrs->add<1>(block_addr, mf);
        m_miss_queue.push_back(mf);
        assert(m_miss_queue.size() < 10);
        outgoing_mf.insert(mf);
        printdbg_tlb("outgoing insert! size:%lu\n", outgoing_mf.size());
        miss_times++;
        return tlb_result::miss;
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
    if (!m_response_queue.empty())
    { //from response queue to icnt
        auto mf = m_response_queue.front();
        printdbg_tlb("send mf:%llX, to icnt\n", mf->get_virtual_addr());
        //auto size = 8 + 8; //ctrl size plus one tlb targe address
        // if (::icnt_has_buffer(m_config.m_icnt_index, size))
        if(global_tlb_icnt->free(m_config.m_icnt_index,mf->get_tpc()))
        {
            // ::icnt_push(m_config.m_icnt_index, mf->get_tpc(), mf, size);
            global_tlb_icnt->send(m_config.m_icnt_index,mf->get_tpc(),mf,gpu_sim_cycle+gpu_tot_sim_cycle);
            printdbg_ICNT("ICNT:L2 to core:To Core:%u,mf:%llx\n",mf->get_tpc(),mf->get_virtual_addr());
            
            m_response_queue.pop_front();
            printdbg_tlb("successfully send to icnt\n");
        }
        else
        {
            printdbg_ICNT("ICNT:L2 to core:try to send but not free,To Core:%u\n", mf->get_tpc());

            //printdbg_tlb("fail to send mf:%llX\n", mf->get_virtual_addr());
        }
    }
    while (m_mshrs->access_ready() and (m_config.response_queue_size == 0 or (m_config.response_queue_size != 0 and m_response_queue.size() < m_config.response_queue_size))) //from mshr to response queue
    {                                                                                                                                                                         //push all the ready access to response Queue
        printdbg_tlb("send m_mshr next access to m response queue\n");
        m_response_queue.push_back(m_mshrs->next_access());
        assert(m_response_queue.size() < 100);
        printdbg_tlb("the mf is:%llX\n", m_response_queue.back()->get_virtual_addr());
    }
    while (m_page_table_walker->ready())
    { //from page_table_walker to l2 tlb
        auto mf = m_page_table_walker->recv();
        printdbg_tlb("the next page walker access to l2 tlb:fill()! mf:%llX\n", mf->get_virtual_addr());

        fill(mf, gpu_sim_cycle + gpu_tot_sim_cycle);
    }
    if (!m_miss_queue.empty()) //from l2tlb to page walker/different from l1 tlb
    {                          //send the request to
        auto mf = m_miss_queue.front();
        //auto vir_addr = mf->get_virtual_addr();
        printdbg_tlb("sending mf:%llX to page walker\n", mf->get_virtual_addr());
        if (m_page_table_walker->free())
        {
            auto ret = m_page_table_walker->send(mf);
            assert(ret); //that must be true;
            m_miss_queue.pop_front();
            printdbg_tlb("send success, miss queue pop,miss queue size:%lu\n", m_miss_queue.size());
        }
        else
        {
            printdbg_tlb("send faild, page walker full,miss queue not pop,miss queue size:%lu\n", m_miss_queue.size());
        }
    }
    mem_fetch *mf = nullptr;
    if (m_recv_buffer.size() < m_config.recv_buffer_size) //recv the request ,if it is a pw requst, send to pwalker, if it's a mf from l1.send it to the recv queue
    {
        // mf = static_cast<mem_fetch *>(::icnt_pop(m_config.m_icnt_index));
        if(global_tlb_icnt->ready(m_config.m_icnt_index,gpu_sim_cycle+gpu_tot_sim_cycle)){
            mf=global_tlb_icnt->recv(m_config.m_icnt_index);
            printdbg_ICNT("ICNT:L2 RECV:TO L2:%u,mf:%llx\n", m_config.m_icnt_index, mf->get_virtual_addr());

            if (mf->pw_origin != NULL)
            { //it's a pw resquest
                m_page_table_walker->send_to_recv_buffer(mf);
                assert(mf->icnt_from>=16);
                printdbg_ICNT("this is a page walker request from memory\n");
                printdbg_PW("from icnt send to page walker!\n");
            }
            else
            {
                assert(mf->icnt_from<=14);
                printdbg_ICNT("this is from Core\n");
                printdbg_tlb("get mf from icnt!access mf:%llX,from cluster%u\n", mf->get_virtual_addr(), mf->get_tpc());
                m_recv_buffer.push(mf);
                printdbg_PW("m recv buffer size:%lu\n", m_recv_buffer.size());
            }
        }else{
                printdbg_ICNT("ICNT:L2 RECV:TO L2:%u,NOT READY!\n",  m_config.m_icnt_index);

        }
    }
    else
    {
        printdbg_tlb("in this cycle:%llu, the icnt recv buffer is full\n", gpu_sim_cycle + gpu_tot_sim_cycle);
    }

    if (!m_recv_buffer.empty())
    { //from icnt to l2 tlb
        auto mf = m_recv_buffer.front();
        //in access(), will according to result, add to response queue, modify mshr , miss queue, and out of the access(), we need to deal with the mf.
        auto result = access(mf, gpu_sim_cycle + gpu_tot_sim_cycle); //this call will automatically add mf to miss queue, response queue or mshr
        printdbg_tlb("access result: %s\n", result == tlb_result::hit ? "hit" : result == tlb_result::hit_reserved ? "hit res" : result == tlb_result::miss ? "miss" : "res fail");
        switch (result)
        {
        case tlb_result::resfail: //in this case , we do not pop m_recv_buffer
            printdbg_tlb("l2 tlb access res fail!\n");
            break;

        default: //hit,hit reserved, miss.
            printdbg_tlb("l2 tlb access res success!\n");
            m_recv_buffer.pop();
            break;
        }
    }

    m_page_table_walker->cycle(); //put all the ready request to his response queue
}
void l2_tlb::fill(mem_fetch *mf, unsigned long long time) //that will be called from l2_tlb.cycle
{
    printdbg_PW("the entry:1:tag:%llx,2:tag:%llx\n", m_tag_arrays[61 * 2] ? m_tag_arrays[61 * 2]->m_tag : 0, m_tag_arrays[61 * 2] ? m_tag_arrays[61 * 2 + 1]->m_tag : 0);
    auto v_addr = mf->get_virtual_addr();
    auto m_page_size_log2 = m_config.m_page_size_log2;
    auto n_set = m_config.n_sets;
    auto n_assoc = m_config.n_associate;

    // get the set_index and tag for searching the tag array.
    auto set_index = (v_addr >> m_page_size_log2) & (n_set - 1);             //first get the page number, then get the cache index.
    auto tag = v_addr & (~static_cast<addr_type>(m_config.m_page_size - 1)); //only need the bits besides page offset
    auto block_addr = v_addr >> m_page_size_log2;
    bool has_atomic;
    m_mshrs->mark_ready(block_addr, has_atomic);
    auto start = m_tag_arrays.begin() + set_index * n_assoc;
    auto end = start + n_assoc;
    auto mask = mf->get_access_sector_mask();
    auto done = false;
    for (; start < end; start++)
    {
        if (!(*start)->is_invalid_line())
        {
            if ((*start)->m_tag == tag)
            {
                assert((*start)->get_status(mask) == RESERVED);
                (*start)->fill(time, mask);
                done = true;
                break;
            }
        }
    }
    assert(done);
    this->del_outgoing(mf);
    printdbg_tlb("out going del: size: %lu\n", outgoing_mf.size());
    return;
}