#include "l1_tlb.h"
#include "tlb_icnt.h"
#include "gpu-cache.h"
#include "icnt_wrapper.h"
#include <deque>
extern unsigned long long gpu_sim_cycle;
extern unsigned long long gpu_tot_sim_cycle;

#include "debug_macro.h"
l1_tlb::l1_tlb(l1_tlb_config &config, page_manager *tlb_page_manager, const std::string &name) : m_config(config),
                                                                                                 m_page_manager(tlb_page_manager),
                                                                                                 m_mshrs(new mshr_table(config.n_mshr_entries, config.n_mshr_max_merge)),
                                                                                                 m_tag_arrays(config.n_sets * config.n_associate),
                                                                                                 access_times(0),
                                                                                                 hit_times(0),
                                                                                                 miss_times(0),
                                                                                                 name(name)
{
    for (unsigned i = 0; i < config.n_sets * config.n_associate; i++)
    {
        m_tag_arrays[i] = new line_cache_block();
    }
}
l1_tlb_config::l1_tlb_config() {} //in constructor, just allocate the memory, and then parse configur,then

void l1_tlb_config::init()
{
    //for test
    //assert(false);
    assert(m_page_size > 0);
    if (m_page_size == 4096)
        m_page_size_log2 = 12;
    else
    {
        throw std::runtime_error("can't accept other page size now!");
    }
}
void l1_tlb_config::reg_option(option_parser_t opp)
{

    option_parser_register(opp, "-l1tlbsets", option_dtype::OPT_INT32, &n_sets, "the sets of l1 tlb", "1");
    option_parser_register(opp, "-l1tlbassoc", option_dtype::OPT_UINT32, &n_associate, "the set associate", "64");
    option_parser_register(opp, "-l1tlb_mshr_entries", option_dtype::OPT_UINT32, &n_mshr_entries, "the number of mshr entries", "64");
    option_parser_register(opp, "-l1tlb_mshr_maxmerge", option_dtype::OPT_UINT32, &n_mshr_max_merge, "the max resqust that mshr can merge", "50");
    option_parser_register(opp, "-l1tlb_response_queue_size", option_dtype::OPT_UINT32, &response_queue_size, "the size of response queue: 0=unlimited", "0");
    option_parser_register(opp, "-l1tlb_miss_queue_size", option_dtype::OPT_UINT32, &miss_queue_size, "the size of miss queue: 0=unlimited", "0");
    option_parser_register(opp, "-l1tlb_page_size", option_dtype::OPT_UINT32, &m_page_size, "the tlb_line_size,currently we only support 4096", "4096");
    option_parser_register(opp, "-l1tlb_allocate_on_fill", option_dtype::OPT_BOOL, &allocate_on_fill, "allocate on fill", "0");
}
void l1_tlb::init()
{
    // m_mshrs=std::make_shared<mshr_table>(m_config.n_mshr_entries,m_config.n_mshr_max_merge);
}

bool l1_tlb::reponse_empty()
{
    return m_response_queue.empty();
}

void l1_tlb::pop_response()
{
    assert(!m_response_queue.empty() && "the m_response_queue can't be empty");
    m_response_queue.pop_front();
}
mem_fetch *l1_tlb::get_top_response()
{
    assert(!m_response_queue.empty() && "the m_response_queue can't be empty");
    return m_response_queue.front();
}

tlb_result l1_tlb::access_alloc_on_miss(mem_fetch *mf, unsigned long long time)
{
    if (mf == nullptr)
    {
        throw std::runtime_error("accessed mf cann't be null");
    }
    else
    {
        /* code */
        //auto result=tlb_result::hit;
        access_times++;

        auto v_addr = mf->get_virtual_addr();
        //auto v_addr = m_page_manager->get_vir_addr(addr);
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
        printdbg_tlb("l1 access mf:%llX\n", mf->get_virtual_addr());
        for (; start < end; start++)
        {
            if (!(*start)->is_invalid_line()) //previouse bug: cant assume is_valid_line, that would filter reserved line out!!!
            {
                auto status = (*start)->get_status(mask);
                if (status != RESERVED) //for eviction
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
                }

                if ((*start)->m_tag == tag)
                {

                    auto status = (*start)->get_status(mask);
                    switch (status)
                    {
                    case RESERVED:
                    {
                        unsigned reason = 0;
                        if (m_mshrs->full(block_addr, reason))
                        {
                            printdbg_tlb("l1 hit_reserved and mshr full\n");

                            reason == 1 ? resfail_mshr_merge_full_times++ : resfail_mshr_entry_full_times++;
                            return tlb_result::resfail;
                        }
                        else
                        {
                            m_mshrs->add<2>(block_addr, mf); //adding to existing entry
                            printdbg_tlb("l1 hit_reserved and push to mshr\n");
                            (*start)->set_last_access_time(time, mask);
                            mf->finished_tlb = true;
                            hit_reserved_times++;

                            return tlb_result::hit_reserved;
                        }
                        break;
                    }
                    case VALID:
                    {
                        printdbg_tlb("push to response queu: mf: %llX\n", mf->get_virtual_addr());
                        mf->finished_tlb = true;
                        m_response_queue.push_front(mf); //only at this time ,we need push front, and we can pop front now.
                        hit_times++;
                        (*start)->set_last_access_time(time, mask);
                        return tlb_result::hit;

                        break;
                    }

                    case MODIFIED:
                    {
                        throw std::runtime_error("tlb cache can't be modified, it's read only!");
                    }
                    default:
                    {
                        throw std::runtime_error("error");
                        break;
                    }
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
        if (all_reserved)
        {

            resfail_all_res_times++;
            return tlb_result::resfail;
        }
        // when run to here, means no hit line found,It's a miss;
        unsigned reason;
        if (m_mshrs->full(block_addr, reason))
        {
            printdbg_tlb("miss and mshr fail: mf:%llX\n", mf->get_virtual_addr());
            reason == 1 ? resfail_mshr_merge_full_times++ : resfail_mshr_entry_full_times++;
            return tlb_result::resfail;
        }
        if ((m_config.miss_queue_size > 0 && m_miss_queue.size() >= m_config.miss_queue_size))
        {
            resfail_mshr_missq_full_times++;
            return tlb_result::resfail;
        }
        auto next_line = has_free_line ? free_line : last_line;
        printdbg_tlb("miss and allocate, send to miss queue and mshr, mf:%llX\n", mf->get_virtual_addr());
        (*next_line)->allocate(tag, block_addr, time, mask);
        m_mshrs->add<1>(block_addr, mf); //adding to new entry
        m_miss_queue.push_back(mf);      //miss
        outgoing_mf.insert(mf);
        miss_times++;
        return tlb_result::miss;
    }
}
tlb_result l1_tlb::access_alloc_on_fill(mem_fetch *mf, unsigned long long time)
{
    if (mf == nullptr)
    {
        throw std::runtime_error("accessed mf cann't be null");
    }
    else
    {
        /* code */
        //auto result=tlb_result::hit;
        access_times++;

        auto v_addr = mf->get_virtual_addr();

        //auto v_addr = m_page_manager->get_vir_addr(addr);
        auto m_page_size_log2 = m_config.m_page_size_log2;
        auto n_set = m_config.n_sets;
        auto n_assoc = m_config.n_associate;

        // get the set_index and tag for searching the tag array.
        auto set_index = (v_addr >> m_page_size_log2) & static_cast<addr_type>(n_set - 1); //first get the page number, then get the cache index.
        auto tag = v_addr & (~static_cast<addr_type>(m_config.m_page_size - 1));           //only need the bits besides page offset
        auto block_addr = v_addr >> m_page_size_log2;
        /* if (block_addr == 802824 && mf->get_tpc() == 2)
            printf("\n\naccess!,core:%u\n", mf->get_tpc()); */
        auto start = m_tag_arrays.begin() + n_assoc * set_index;
        auto end = start + n_assoc;
        //to find a place to access
        // auto has_free_line = false;

        // unsigned long long oldest_access_time = time;
        // decltype(start) free_line;
        decltype(start) hit_line;
        // decltype(start) last_line;
        auto mask = mf->get_access_sector_mask();
        // bool all_reserved = true;
        printdbg_tlb("l1 access mf:%llX\n", mf->get_virtual_addr());
        for (; start < end; start++)
        {
            if (!(*start)->is_invalid_line()) //previouse bug: cant assume is_valid_line, that would filter reserved line out!!!
            {
                if ((*start)->m_tag == tag)
                {
                    printdbg_tlb("push to response queu: mf: %llX\n", mf->get_virtual_addr());
                    mf->finished_tlb = true;
                    m_response_queue.push_front(mf); //only at this time ,we need push front, and we can pop front now.
                    hit_times++;
                    (*start)->set_last_access_time(time, mask);
                    /*  if (block_addr == 802824 && mf->get_tpc() == 2)
                        printf("hit!\n"); */
                    return tlb_result::hit;
                    break;
                }
            }
        }

        // when run to here, means no hit line found,It's a miss;
        unsigned reason;

        if (m_mshrs->full(block_addr, reason))
        {
            printdbg_tlb("miss and mshr fail: mf:%llX\n", mf->get_virtual_addr());
            reason == 1 ? resfail_mshr_merge_full_times++ : resfail_mshr_entry_full_times++;
            return tlb_result::resfail;
        }
        if ((m_config.miss_queue_size > 0 && m_miss_queue.size() >= m_config.miss_queue_size))
        {
            resfail_mshr_missq_full_times++;
            return tlb_result::resfail;
        }
        //auto next_line = has_free_line ? free_line : last_line;
        printdbg_tlb("miss and allocate, send to miss queue and mshr, mf:%llX\n", mf->get_virtual_addr());
        //(*next_line)->allocate(tag, block_addr, time, mask); do not allocate
        bool already_in_mshr = m_mshrs->probe(block_addr);
        /* if (block_addr == 802824 && mf->get_tpc() == 2)
            printf("mf,addr:%llu,block addr:%llu,is in mshr? %s\n", mf->virtual_addr, block_addr, already_in_mshr ? "yes" : "no");
         */
        m_mshrs->add<1>(block_addr, mf); //adding to new entry
        if (!already_in_mshr)
        {
            /* if (block_addr == 802824 && mf->get_tpc() == 2)
                printf("add to missqueue\n\n");
             */
            m_miss_queue.push_back(mf); //miss
            outgoing_mf.insert(mf);
        }
        else
        {
            mf->finished_tlb = true;
        }
        miss_times++;
        return tlb_result::miss;
    }
}
bool l1_tlb::is_outgoing(mem_fetch *mf)
{
    return (outgoing_mf.find(mf) != outgoing_mf.end());
}
void l1_tlb::del_outgoing(mem_fetch *mf)
{
    outgoing_mf.erase(mf);
}
void l1_tlb::cycle()
{
    if (!m_miss_queue.empty())
    { //send the request to L2 tlb
        auto mf = m_miss_queue.front();
        //auto size = mf->get_ctrl_size(); //read only need 8 bytes
        //if (::icnt_has_buffer(mf->get_tpc(), size))
        if (global_tlb_icnt->free(mf->get_tpc(), m_config.m_icnt_index))
        {
            printdbg_tlb("from miss queue to icnt,mftpc: %u;mf :%llX\n", mf->get_tpc(), mf->get_virtual_addr());
            //::icnt_push(mf->get_tpc(), m_config.m_icnt_index, mf, size);
            global_tlb_icnt->send(mf->get_tpc(), m_config.m_icnt_index, mf, gpu_sim_cycle + gpu_tot_sim_cycle);
            /* if(mf->virtual_addr==0xc03b5000){
                printf("from l1 tlb to l2tlb!core:%u,cycle:%llu\n",mf->get_tpc(),gpu_sim_cycle+gpu_tot_sim_cycle);
            } */
            auto block_addr = mf->virtual_addr >> 12;
            /* if (block_addr == 802824 && mf->get_tpc() == 2)
                printf("\nsending to  icnt!:%u\n\n", mf->get_tpc());
 */
            printdbg_ICNT("ICNT:CORE to L2:from Core:%u,mf:%llx\n", mf->get_tpc(), mf->get_virtual_addr());
            printdbg_PW("push from core:%u,send to %u\n", mf->get_tpc(), m_config.m_icnt_index);
            m_miss_queue.pop_front(); //successfully pushed to icnt
        }
        else
        {

            printdbg_ICNT("ICNT:CORE to L2:try to send but not free,To L2\n");
        }
    }
    while (m_mshrs->access_ready())
    { //push all the ready access to response Queue

        m_response_queue.push_back(m_mshrs->next_access());
    }
}
void l1_tlb::fill_allocate_on_miss(mem_fetch *mf, unsigned long long time)
{
    //in ldst cycle, will call fill(that's from icnt)
    auto v_addr = mf->get_virtual_addr();
    //auto addr = m_page_manager->translate(v_addr);
    auto m_page_size_log2 = m_config.m_page_size_log2;
    auto n_set = m_config.n_sets;
    auto n_assoc = m_config.n_associate;

    // get the set_index and tag for searching the tag array.
    auto set_index = (v_addr >> m_page_size_log2) & static_cast<addr_type>(n_set - 1); //first get the page number, then get the cache index.
    auto tag = v_addr & (~static_cast<addr_type>(m_config.m_page_size - 1));           //only need the bits besides page offset
    auto block_addr = v_addr >> m_page_size_log2;
    printdbg_tlb("l1 tlb fill, mf:%llX, core id:%u, blockaddr:%llX\n", mf->get_virtual_addr(), mf->get_sid(), block_addr);
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

    assert(done && "fill must succeed");
    //throw std::runtime_error("can't be here");
}

void l1_tlb::fill_allocate_on_fill(mem_fetch *mf, unsigned long long time)
{
    //this time we need to find which entry  to fill
    //in ldst cycle, will call fill(that's from icnt)
    auto v_addr = mf->get_virtual_addr();
    //auto addr = m_page_manager->translate(v_addr);
    auto m_page_size_log2 = m_config.m_page_size_log2;
    auto n_set = m_config.n_sets;
    auto n_assoc = m_config.n_associate;

    // get the set_index and tag for searching the tag array.
    auto set_index = (v_addr >> m_page_size_log2) & static_cast<addr_type>(n_set - 1); //first get the page number, then get the cache index.
    auto tag = v_addr & (~static_cast<addr_type>(m_config.m_page_size - 1));           //only need the bits besides page offset
    auto block_addr = v_addr >> m_page_size_log2;
    printdbg_tlb("l1 tlb fill, mf:%llX, core id:%u, blockaddr:%llX\n", mf->get_virtual_addr(), mf->get_sid(), block_addr);
    bool has_atomic;
    /* if (block_addr == 802824 && mf->get_tpc() == 2)
        printf("mf comming back and fill,core:%u\n", mf->get_tpc()); */
    m_mshrs->mark_ready(block_addr, has_atomic);
    auto start = m_tag_arrays.begin() + set_index * n_assoc;
    auto end = start + n_assoc;
    auto fill_entry = find_entry_to_fill(start, end);
    auto mask = mf->get_access_sector_mask();
    (*fill_entry)->allocate(tag, block_addr, time, mask);

    (*fill_entry)->fill(time, mask);
    //(*fill_entry)->set_last_access_time(time,mem_access_sector_mask_t());//otherwise the  access time will be 0!
}
unsigned int l1_tlb::outgoing_size()
{
    return outgoing_mf.size();
}

void l1I_tlb_config::reg_option(option_parser_t opp)
{
    option_parser_register(opp, "-l1Itlbsets", option_dtype::OPT_INT32, &n_sets, "the sets of l1 tlb", "1");
    option_parser_register(opp, "-l1Itlbassoc", option_dtype::OPT_UINT32, &n_associate, "the set associate", "32");
    option_parser_register(opp, "-l1Itlb_mshr_entries", option_dtype::OPT_UINT32, &n_mshr_entries, "the number of mshr entries", "4");              //number of instruction
    option_parser_register(opp, "-l1Itlb_mshr_maxmerge", option_dtype::OPT_UINT32, &n_mshr_max_merge, "the max resqust that mshr can merge", "50"); //number of cores
    option_parser_register(opp, "-l1Itlb_response_queue_size", option_dtype::OPT_UINT32, &response_queue_size, "the size of response queue: 0=unlimited", "0");
    option_parser_register(opp, "-l1Itlb_miss_queue_size", option_dtype::OPT_UINT32, &miss_queue_size, "the size of miss queue: 0=unlimited", "0");
    option_parser_register(opp, "-l1Itlb_page_size", option_dtype::OPT_UINT32, &m_page_size, "the tlb_line_size,currently we only support 4096", "4096");
}