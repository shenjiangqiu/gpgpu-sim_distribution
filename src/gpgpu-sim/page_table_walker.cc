#include "l2_tlb.hpp"
#include"tlb_icnt.h"
#include "page_table_walker.hpp"
#include <unordered_map>
//#define TLBDEBUG
#define PWDEBUG
#define TLBDEBUG
#include "debug_macro.h"
#include "icnt_wrapper.h"
unsigned total_mf = 0;
extern unsigned long long gpu_sim_cycle;
extern unsigned long long gpu_tot_sim_cycle;
latency_queue::latency_queue(unsigned long long latency, unsigned size) : m_size_limit(size),
                                                                          m_latency(latency)
{
}

bool latency_queue::add(mem_fetch *v, unsigned long long time)
{
    if (m_elements.size() >= m_size_limit) //TODO what is size limit
    {
        throw std::runtime_error("can't exceed the limit of pagewalker size\n");
    }
    printdbg_tlb("l2 pw add,mf mf: %p,addr:%llX\n", v, v->get_physic_addr());

    auto temp = std::make_pair(time + m_latency, v);

    m_elements.push(temp);
    return true;
}
bool latency_queue::ready(unsigned long long time) const
{
    if (m_elements.empty())
    {
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

mem_fetch *latency_queue::get() const
{
    assert(!m_elements.empty());
    auto v = m_elements.front().second;
    //printdbg_tlb("l2 pw get,mf iD: %u,mf:%llX\n",v->sm_next_mf_request_uid,v->get_physic_addr());
    return v;
}
//extern std::unordered_map<mem_fetch*,unsigned long long> mf_map;
mem_fetch *latency_queue::pop()
{
    assert(!m_elements.empty());
    auto v = m_elements.front().second;
    printdbg_tlb("pop: mf %p\n", v);
#ifdef TLBDEBUG
    fflush(stdout);
    fflush(stderr);
#endif
    //printdbg_tlb("l2 pw pop,mf iD: %u,mf:%llX\n",v->sm_next_mf_request_uid,v->get_physic_addr());
    m_elements.pop();
    return v;
}
page_table_walker::page_table_walker(unsigned int size, unsigned int latency) : m_latency_queue(latency, size)
{
}
void page_table_walker_config::reg_option(option_parser_t opp)
{
    option_parser_register(opp, "-page_table_walker_waiting_size", option_dtype::OPT_UINT32, &waiting_queue_size, "waiting queue size", "1000");
    option_parser_register(opp, "-page_table_walker_concurrent_size", option_dtype::OPT_UINT32, &walker_size, "the walker_size", "8");
    option_parser_register(opp, "-page_table_walker_cache_size", option_dtype::OPT_UINT32, &cache_size, "the cache_size", "32");
    option_parser_register(opp, "-page_table_walker_cache_assoc", option_dtype::OPT_UINT32, &cache_assoc, "the cache_assoc", "2");
    option_parser_register(opp, "-page_table_walker_mshr_size", option_dtype::OPT_UINT32, &mshr_size, "mshr_size", "16");
    option_parser_register(opp, "-page_table_walker_mshr_max_merge", option_dtype::OPT_UINT32, &mshr_max_merge, "mshr_max_merge", "8");
}
bool latency_queue::free() const
{
    return m_elements.size() < m_size_limit;
}

bool page_table_walker::free() const
{
    return m_latency_queue.free();
}
bool page_table_walker::ready() const
{
    return !m_response_queue.empty();
}
void page_table_walker::cycle()
{
    while (m_latency_queue.ready(gpu_sim_cycle + gpu_tot_sim_cycle))
    {
        //auto temp=m_latency_queue.get();
        auto temp = m_latency_queue.pop();
        m_response_queue.push(temp);
    }
}
bool page_table_walker::send(mem_fetch *mf)
{
    return m_latency_queue.add(mf, gpu_sim_cycle + gpu_tot_sim_cycle);
}
mem_fetch *page_table_walker::recv()
{
    assert(!m_response_queue.empty());
    auto temp = m_response_queue.front();
    m_response_queue.pop();
    return temp;
}
mem_fetch *page_table_walker::recv_probe() const
{
    assert(!m_response_queue.empty());
    return m_response_queue.front();
}

real_page_table_walker::real_page_table_walker(page_table_walker_config m_config) : m_config(m_config),
                                                                                    m_tag_arrays(m_config.cache_size * m_config.cache_assoc),
                                                                                    m_mshr(new mshr_table(m_config.mshr_size, m_config.mshr_max_merge))
{
    for (unsigned i = 0; i < m_config.cache_size * m_config.cache_assoc; i++)
    {
        m_tag_arrays[i] = std::make_shared<line_cache_block>();
    }
}

bool real_page_table_walker::free() const
{
    return waiting_queue.size() < m_config.waiting_queue_size;
}

bool real_page_table_walker::ready() const
{
    return !response_queue.empty();
}
void set_pw_mf(mem_fetch *new_mf, page_table_level level,mem_fetch* parent)
{
    auto page_table_addr = global_page_manager->get_pagetable_physic_addr(new_mf->get_virtual_addr(), level);

    new_mf->m_data_size = 8;
    new_mf->m_type = READ_REQUEST;
    new_mf->pw_origin = parent;
    new_mf->physic_addr = page_table_addr; //need to fetch a cache line ,
    new_mf->reset_raw_addr();
}
void real_page_table_walker::cycle()
{
    //TODO:1,send waiting queue to working_walker,//
    //send working walker  to access,and to miss queue;or send to response queue
    //send miss queue to icnt;
    //get from icnt and deal with working worker, or send to response queue;
    if (working_walker.size() < m_config.walker_size and !waiting_queue.empty()) //it's from waiting queue, to working set.
    {
        auto mf = waiting_queue.front();
        auto vir_addr=mf->get_virtual_addr();
        printdbg_PW("new mf to enter walker: mf addr:%llx.",vir_addr);
        waiting_queue.pop();
        auto new_mf =mf->get_copy();
        total_mf++;
        assert(total_mf <= m_config.walker_size);
        set_pw_mf(new_mf,page_table_level::L4_ROOT,mf);
        assert(working_walker.find(mf) == working_walker.end());
        working_walker[mf] = std::make_tuple(page_table_level::L4_ROOT, new_mf, false);
        ready_to_send.push(mf);
        assert(ready_to_send.size() < 10);
        //TODO design walker
    }

    if (!ready_to_send.empty()) //it's from working set to cache or miss queue.
    {
        auto mf = ready_to_send.front();
        if (std::get<0>(working_walker[mf]) == page_table_level::L1_LEAF)
        { //send leaf access to l2 cache
            auto next_mf = std::get<1>(working_walker[mf]);
            assert(std::get<2>(working_walker[mf]) == false);
            //assert(total_mf<m_config.walker_size);
            next_mf->m_data_size = 8;
            next_mf->m_type = READ_REQUEST;

            next_mf->pw_origin = mf;
            auto &target_status = working_walker[mf];
            assert(next_mf->get_is_write() == false);
            auto page_table_addr = global_page_manager->get_pagetable_physic_addr(mf->get_virtual_addr(), page_table_level::L1_LEAF);

            next_mf->physic_addr = page_table_addr;
            next_mf->reset_raw_addr();
            // std::get<0>(target_status) = pagetab
            std::get<1>(target_status) = next_mf;
            miss_queue.push(next_mf);
            assert(miss_queue.size() < 10);
            std::get<2>(working_walker[mf]) = true; //already sent
            ready_to_send.pop();
        }
        else
        {
            auto child_mf = std::get<1>(working_walker[mf]);
            auto result = access(child_mf); //that will chage the status of working worker//that will access the cache
            switch (result)
            {
            case tlb_result::hit:
            {
                auto &target_status = working_walker[mf];
                auto current_level = std::get<0>(target_status);
                std::get<0>(target_status) = get_next_level(current_level);

                delete child_mf;
                //total_mf--;

                auto next_mf = mf->get_copy();
                //total_mf++;

                assert(total_mf <= m_config.walker_size);
                set_pw_mf(next_mf,get_next_level(current_level),mf);
                std::get<1>(target_status) = next_mf;
                std::get<2>(target_status)=false;
                ready_to_send.pop();
                ready_to_send.push(mf);
                assert(ready_to_send.size() < m_config.walker_size);
                break;
            }
            case tlb_result::hit_reserved:
            {
                assert(std::get<2>(working_walker[mf]) == false);

                std::get<2>(working_walker[mf]) = true; //it's on_going;
                ready_to_send.pop();
                break;
            }
            case tlb_result::miss:
            {
                assert(std::get<2>(working_walker[mf]) == false);
                std::get<2>(working_walker[mf]) = true;
                ready_to_send.pop();
                break;
            }
            case tlb_result::resfail:
                //do nothing
                break;
            default:

                break;
            }
        }
    }
    if (!miss_queue.empty())
    {
        //TODO set destin L2 partition
            auto mf = miss_queue.front();
        // if (::icnt_has_buffer(global_l2_tlb_index, 8u))
        if(global_tlb_icnt->free(global_l2_tlb_index,mf->get_sub_partition_id()+global_n_cores+1))
        {
            miss_queue.pop();
            auto subpartition_id = mf->get_sub_partition_id();

            // ::icnt_push(global_l2_tlb_index, subpartition_id + global_n_cores + 1, mf, 8u);
            global_tlb_icnt->send(global_l2_tlb_index, subpartition_id + global_n_cores + 1,mf,gpu_sim_cycle+gpu_tot_sim_cycle);
            printdbg_PW("push mf to icnt:mf->address:%llx,from %u,to: %u\n", mf->get_physic_addr(), global_l2_tlb_index, subpartition_id + global_n_cores + 1);
        }
        else
        {
            printdbg_PW("INCT not has buffer!\n");
        }
    }
    //start to recv from icnt//TODO change l2 icnt push decition
    //auto child_mf = static_cast<mem_fetch *>(::icnt_pop(global_l2_tlb_index));
    if (!icnt_response_buffer.empty())
    {
        auto child_mf = icnt_response_buffer.front();
        icnt_response_buffer.pop();
        if (child_mf)
        {
            auto mf_origin = child_mf->pw_origin;
            auto vir_addr=mf_origin->get_virtual_addr();
            printdbg_PW("from icnt to pagewalker! origin mf:%llx,level:%u\n",vir_addr,std::get<0>(working_walker[mf_origin]));
            auto &level = std::get<0>(working_walker[mf_origin]);
            if (level == page_table_level::L1_LEAF)
            {
                assert(std::get<2>(working_walker[mf_origin]) == true);
                delete child_mf;
                total_mf--;


                working_walker.erase(mf_origin);
                
                response_queue.push(mf_origin);
                assert(response_queue.size() < 10);
            }
            else
                fill(child_mf);
        }
    }
    while (m_mshr->access_ready())
    {
        auto child_mf = m_mshr->next_access();
        auto mf_origin = child_mf->pw_origin;
        assert(mf_origin != NULL);
        auto &status = working_walker[mf_origin]; //fix bug, that should be reference!!!!!!
        auto &level = std::get<0>(status);        //attention! it's reference not copy!!!
        
        delete child_mf;
        total_mf--;

        assert(level != page_table_level::L1_LEAF);

        //it is not the last level, we need keep going.
        //1,change working status,to next level, to next mf, and not outgoing
        level = get_next_level(level); //change to next level,1

        auto next_mf =mf_origin->get_copy();
        total_mf++;

        assert(total_mf <= m_config.walker_size);

        set_pw_mf(next_mf, level,mf_origin);

        std::get<1>(status) = next_mf; //set next mf;/2
        assert(std::get<2>(status) == true);
        std::get<2>(status) = false; //set next outgoing bit/3
        ready_to_send.push(mf_origin);
        assert(ready_to_send.size() <= m_config.walker_size);
    }
}

void page_table_walker_config::init()
{
}
tlb_result real_page_table_walker::access(mem_fetch *child_mf)
{
    auto parent_mf = child_mf->pw_origin;
    assert(std::get<0>(working_walker[parent_mf]) != page_table_level::L1_LEAF); //leaf shouldn't access cache
    if (child_mf == nullptr)
    {
        throw std::runtime_error("accessed mf cann't be null");
    }
    else
    {
        /* code */
        auto addr = child_mf->get_physic_addr();
        auto n_set = m_config.cache_size;
        auto n_assoc = m_config.cache_assoc;

        // get the set_index and tag for searching the tag array.
        auto set_index = (addr >> 3) & static_cast<addr_type>(n_set - 1); //first get the page number, then get the cache index.
        auto tag = addr & (~static_cast<addr_type>(8 - 1));               //only need the bits besides page offset
        assert(tag == addr);
        auto block_addr = addr >> 3;
        auto start = m_tag_arrays.begin() + n_assoc * set_index;
        auto end = start + n_assoc;
        //to find a place to access
        auto has_free_line = false;

        unsigned long long oldest_access_time = gpu_sim_cycle + gpu_tot_sim_cycle;
        auto time = oldest_access_time;
        decltype(start) free_line;
        decltype(start) hit_line;
        decltype(start) last_line;
        auto mask = child_mf->get_access_sector_mask();
        for (; start < end; start++)
        {
            if (!(*start)->is_invalid_line())
            {
                if ((*start)->get_last_access_time() < oldest_access_time)
                {
                    oldest_access_time = (*start)->get_last_access_time();
                    last_line = start;
                }
                if ((*start)->m_tag == tag) //ok we find
                {

                    auto status = (*start)->get_status(mask);
                    switch (status)
                    {
                    case RESERVED:
                    {
                        if (m_mshr->full(block_addr))
                        {
                            printdbg_tlb("reserved! and mshr full\n");
                            return tlb_result::resfail;
                        }
                        else
                        {
                            printdbg_tlb("hit reserved! add to mfshr\n");

                            m_mshr->add<2>(block_addr, child_mf); //not new

                            //ready_to_send.pop();
                            (*start)->set_last_access_time(time, mask);
                            return tlb_result::hit_reserved;
                        }
                        break;
                    }
                    case VALID:
                    {
                        printdbg_tlb("child mf hit: mf:%llX\n", child_mf->get_virtual_addr());

                        return tlb_result::hit;
                        break;
                    }
                    case MODIFIED:
                    {

                        throw std::runtime_error("tlb cache can't be modified, it's read only!");
                        return tlb_result::resfail;
                        break;
                    }
                    default:
                        throw std::runtime_error("error");
                        return tlb_result::resfail;
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
        if (m_mshr->full(block_addr))
        {
            return tlb_result::resfail;
        }
        auto next_line = has_free_line ? free_line : last_line;
        (*next_line)->allocate(tag, block_addr, time, mask);
        m_mshr->add<1>(block_addr, child_mf);
        miss_queue.push(child_mf);
        assert(miss_queue.size() < 10);
        //ready_to_send.pop();

        //printdbg_tlb("outgoing insert! size:%lu\n", outgoing_mf.size());
        return tlb_result::miss;
    }
}

bool real_page_table_walker::send(mem_fetch *mf)
{
    assert(waiting_queue.size() < m_config.waiting_queue_size);
    if (waiting_queue.size() < m_config.waiting_queue_size)
    {
        waiting_queue.push(mf);
        printdbg_PW("waiting queu,size is %lu\n", waiting_queue.size());

        //assert(waiting_queue.size()<=m_config.waiting_queue_size);
        return true;
    }
    else
    {
        return false;
    }
}
mem_fetch *real_page_table_walker::recv()
{
    if (response_queue.size() == 0)
    {
        throw std::runtime_error("error, response queue size is 0");
    }
    auto mf = response_queue.front();
    response_queue.pop();
    return mf;
}
mem_fetch *real_page_table_walker::recv_probe() const
{
    if (response_queue.size() == 0)
    {
        throw std::runtime_error("error, response queue size is 0");
    }
    auto mf = response_queue.front();
    return mf;
}

void real_page_table_walker::fill(mem_fetch *mf)
{
    auto addr = mf->get_physic_addr();
    auto n_set = m_config.cache_size;
    auto n_assoc = m_config.cache_assoc;

    // get the set_index and tag for searching the tag array.
    assert((n_set & (n_set - 1)) == 0);
    auto set_index = (addr >> 3) & (n_set - 1); //first get the page number, then get the cache index.
    auto tag = addr & ~(8 - 1);                 //only need the bits besides page offset
    auto block_addr = addr >> 3;
    bool has_atomic;
    m_mshr->mark_ready(block_addr, has_atomic);
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
                (*start)->fill(gpu_sim_cycle + gpu_tot_sim_cycle, mask);
                done = true;
                break;
            }
        }
    }
    assert(done);
    return;
}