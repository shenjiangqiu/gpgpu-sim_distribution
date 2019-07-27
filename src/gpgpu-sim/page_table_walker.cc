#include "l2_tlb.h"
#include "tlb_icnt.h"
#include "page_table_walker.h"
#include <map>
//#define TLBDEBUG

#include "debug_macro.h"
#include "icnt_wrapper.h"
#include <memory>
#include <functional>
// constexpr addr_type masks[4] = {0xFF8000000000, 0x7FC0000000, 0x3FE00000, 0x1FF000};
// constexpr addr_type masks_accumulate[4] = {0xFF8000000000, 0xFFFFC0000000, 0xFFFFFFE00000, 0xFFFFFFFFF000};
// constexpr unsigned mask_offset[4] = {39, 30, 21, 12};
unsigned global_bit;

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
//extern std::map<mem_fetch*,unsigned long long> mf_map;
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
    option_parser_register(opp, "-page_table_walker_waiting_size", option_dtype::OPT_UINT32, &waiting_queue_size, "waiting queue size", "256");
    option_parser_register(opp, "-page_table_walker_concurrent_size", option_dtype::OPT_UINT32, &walker_size, "the walker_size", "8");
    option_parser_register(opp, "-page_table_walker_cache_size", option_dtype::OPT_UINT32, &cache_size, "the cache_size", "64");
    option_parser_register(opp, "-page_table_walker_cache_assoc", option_dtype::OPT_UINT32, &cache_assoc, "the cache_assoc", "16");
    option_parser_register(opp, "-page_table_walker_mshr_size", option_dtype::OPT_UINT32, &mshr_size, "mshr_size", "256");
    option_parser_register(opp, "-page_table_walker_mshr_max_merge", option_dtype::OPT_UINT32, &mshr_max_merge, "mshr_max_merge", "256");
    option_parser_register(opp, "-page_table_walker_range_cache_size", option_dtype::OPT_UINT32, &m_range_size, "range size", "32");
    option_parser_register(opp, "-page_table_walker_enable_neighbor", option_dtype::OPT_BOOL, &enable_neighborhood, "neighborhood switch", "1");
    option_parser_register(opp, "-page_table_walker_enable_range", option_dtype::OPT_BOOL, &enable_range_pt, "range switch", "1");
    option_parser_register(opp, "-page_table_walker_enable_32_bit", option_dtype::OPT_BOOL, &enable_32_bit, "using 32 bit address space not 64", "0");
    option_parser_register(opp, "-page_table_walker_using_new_lru", option_dtype::OPT_BOOL, &using_new_lru, "using new lru", "0");
    option_parser_register(opp, "-page_table_walker_i1_position", option_dtype::OPT_UINT32, &i1_position, "i1_position", "1");
    option_parser_register(opp, "-page_table_walker_i2_position", option_dtype::OPT_UINT32, &i2_position, "i2_position", "6");

    option_parser_register(opp, "-page_table_walker_i3_position", option_dtype::OPT_UINT32, &i3_position, "i3_position", "12");
    option_parser_register(opp, "-page_table_walker_allocate_on_fill", option_dtype::OPT_BOOL, &allocate_on_fill, "allocate on fill", "0");
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
                                                                                    new_tag_arrays(m_config.cache_size),
                                                                                    m_tag_arrays(m_config.cache_size * m_config.cache_assoc),
                                                                                    m_mshr(new mshr_table(m_config.mshr_size, m_config.mshr_max_merge)),
                                                                                    access_times(0),
                                                                                    hit_times(0),
                                                                                    m_range_cache_size(m_config.m_range_size),
                                                                                    miss_times(0)

{
    for (unsigned i = 0; i < m_config.cache_size * m_config.cache_assoc; i++)
    {
        m_tag_arrays[i] = new line_cache_block();
    }

    for (unsigned i = 0; i < m_config.waiting_queue_size; i++)
    {
    }
}

bool real_page_table_walker::ready() const
{
    return !response_queue.empty();
}
void set_pw_mf(mem_fetch *new_mf, page_table_level level, mem_fetch *parent)
{
    auto page_table_addr = global_page_manager->get_pagetable_physic_addr(new_mf->get_virtual_addr(), level);

    new_mf->m_data_size = 8;
    new_mf->m_type = READ_REQUEST;
    new_mf->pw_origin = parent;
    new_mf->physic_addr = page_table_addr; //need to fetch a cache line ,
    new_mf->reset_raw_addr();
}
auto find_next_mf(std::list<std::tuple<bool, mem_fetch *, bool, page_table_level, addr_type>> &waiting_buffer) -> decltype(waiting_buffer.begin())
{
    auto start = waiting_buffer.begin();
    auto end = waiting_buffer.end();
    for (; start != end; start++)
    {
        if (std::get<0>(*start) == true && std::get<2>(*start) == true) //it is coalesced and It's watiing for response
        {
            continue;
        }
        else
        {
            return start;
        }
    }
    return waiting_buffer.end();
}

void real_page_table_walker::cycle()
{
    //TODO:1,send waiting queue to working_walker,//
    //send working walker  to access,and to miss queue;or send to response queue
    //send miss queue to icnt;
    //get from icnt and deal with working worker, or send to response queue;

    if (!range_latency_queue.empty())
    {
        //printdbg_PTRNG("\n\nlatency queue is not empty!!\n\n\n");

        if (range_latency_queue.front().second <= gpu_sim_cycle + gpu_tot_sim_cycle)
        {
            //printdbg_PTRNG("start to fill to range_latency_cache!\n\n");
            fill_to_range_cache(range_latency_queue.front().first);
            range_latency_queue.pop();
        }
    }

    if (working_walker.size() < m_config.walker_size and !waiting_buffer.empty()) //it's from waiting queue, to working set.
    {
        //this code is hard to understand, It's according to paper: neighborhood,Micro 18, please read the peper fist.
        //my email: jshen2@mtu.edu
        auto mf_itor = find_next_mf(waiting_buffer);
        if (mf_itor == waiting_buffer.end())
        {
        }
        else
        {

            auto mf = *mf_itor;

            printdbg_PW("new mf to enter walker: mf addr:%llx.\n", std::get<1>(mf)->get_virtual_addr());
            mf_itor = waiting_buffer.erase(mf_itor);
            auto new_mf = std::get<1>(mf)->get_copy();
            total_mf++;
            assert(total_mf <= m_config.walker_size);
            auto level_debug = std::get<3>(mf);
            /* if (level_debug == page_table_level::L1_LEAF)
            {
                int bbb = 100;
            } */
            set_pw_mf(new_mf, std::get<3>(mf), std::get<1>(mf));

            //assert(working_walker.find(std::get<1>(mf)) == working_walker.end());
            working_walker[std::get<1>(mf)] = std::make_tuple(std::get<3>(mf), new_mf, false);
            //printdbg("access new working worker")
            ready_to_send.push(std::get<1>(mf));
            assert(ready_to_send.size() < 10);
            //neigber hood, to check all the flags;
            //0:coaled  1:mf  2:wating?  3:level  4:addr

            printdbg_NEI("sending a mf from waiting queue to walker:wating queue size:%lu,walker size:%lu!\n", waiting_buffer.size(), working_walker.size());
            printdbg_NEI("before scan\n");
            printdbg_NEI("current waiting queue:\n");
            PRINT_WAITINGBUFFER(waiting_buffer);
            printdbg_NEI("current walking wakjer:\n");
            PRINT_WORINGWORKER(working_walker);
            if (m_config.enable_neighborhood)
            {
                for (auto &mf_in_waiting : waiting_buffer) //scan and set neiborhood waiting flags
                {
                    if (is_neighbor(std::get<1>(mf_in_waiting), std::get<1>(mf), std::get<3>(mf), global_bit)) //only that level can be processed
                    {
                        std::get<0>(mf_in_waiting) = true;
                        std::get<2>(mf_in_waiting) = true;
                        std::get<3>(mf_in_waiting) = std::get<3>(mf);
                    }
                }
            }
            printdbg_NEI("after scan\n");
            printdbg_NEI("current waiting queue:\n");
            PRINT_WAITINGBUFFER(waiting_buffer);
            printdbg_NEI("current walking wakjer:\n");
            PRINT_WORINGWORKER(working_walker);
        }
        //TODO design walker
    }

    if (!ready_to_send.empty()) //it's from working set to cache or miss queue.
    {
        auto mf = ready_to_send.front();
        if (std::get<0>(working_walker[mf]) == page_table_level::L1_LEAF || m_config.cache_assoc == 0 || m_config.cache_size == 0) //bypass pw cache
        {                                                                                                                          //send leaf access to l2 cache
            auto next_mf = std::get<1>(working_walker[mf]);
            assert(std::get<2>(working_walker[mf]) == false);
            //set_pw_mf(next_mf, page_table_level::L1_LEAF, mf);
            //assert(total_mf<m_config.walker_size);

            auto &target_status = working_walker[mf];
            //assert(next_mf->get_is_write() == false);
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
            //access_times++;
            tlb_result result;
            if (m_config.using_new_lru)
                result = access<2>(child_mf); //that will chage the status of working worker//that will access the cache
            else
            {
                result = access<1>(child_mf);
            }

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
                set_pw_mf(next_mf, get_next_level(current_level), mf);
                std::get<1>(target_status) = next_mf;
                std::get<2>(target_status) = false;
                ready_to_send.pop();
                ready_to_send.push(mf);
                assert(ready_to_send.size() <= m_config.walker_size);
                printdbg_NEI("HIT!\n");
                //neighborhood scan and set the flags
                printdbg_NEI("before scan\n");
                printdbg_NEI("current waiting queue:\n");
                PRINT_WAITINGBUFFER(waiting_buffer);
                printdbg_NEI("current walking wakjer:\n");
                PRINT_WORINGWORKER(working_walker);
                //send and hit//it's one type of return.don't ne confused!!!
                if (m_config.enable_neighborhood)
                {

                    for (auto &mf_in_waiting : waiting_buffer)
                    {
                        if (is_neighbor(std::get<1>(mf_in_waiting), mf, current_level, m_config.enable_32_bit ? 32 : 64))
                        {
                            if (is_neighbor(std::get<1>(mf_in_waiting), mf, get_next_level(current_level), m_config.enable_32_bit ? 32 : 64))
                            {
                                std::get<0>(mf_in_waiting) = true;
                                std::get<2>(mf_in_waiting) = true;
                                std::get<3>(mf_in_waiting) = get_next_level(current_level);
                            }
                            else
                            { //some mf is waiting for it!  be sure all the waiting mf are served.
                                std::get<0>(mf_in_waiting) = true;
                                std::get<2>(mf_in_waiting) = false;
                                std::get<3>(mf_in_waiting) = get_next_level(current_level);
                            }
                        }
                    }
                }
                printdbg_NEI("after scan\n");
                printdbg_NEI("current waiting queue:\n");
                PRINT_WAITINGBUFFER(waiting_buffer);
                printdbg_NEI("current walking wakjer:\n");
                PRINT_WORINGWORKER(working_walker);

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
                printdbg_ICNT("pagetable walker cache res fail,ready to send size:%lu\n", ready_to_send.size());
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
        if (global_tlb_icnt->free(global_l2_tlb_index, mf->get_sub_partition_id() + global_n_cores + 1))
        {
            miss_queue.pop();
            auto subpartition_id = mf->get_sub_partition_id();

            // ::icnt_push(global_l2_tlb_index, subpartition_id + global_n_cores + 1, mf, 8u);
            global_tlb_icnt->send(global_l2_tlb_index, subpartition_id + global_n_cores + 1, mf, gpu_sim_cycle + gpu_tot_sim_cycle);
            printdbg_ICNT("ICNT:L2 to Mem:To Mem:%u,mf:%llx\n", subpartition_id + global_n_cores + 1, mf->get_virtual_addr());

            printdbg_PW("push mf to icnt:mf->address:%llx,from %u,to: %u\n", mf->get_physic_addr(), global_l2_tlb_index, subpartition_id + global_n_cores + 1);
        }
        else
        {

            printdbg_ICNT("ICNT:L2 to Mem:try to send but not free,To Mem:%u\n", (unsigned)(mf->get_sub_partition_id() + global_n_cores + 1));

            printdbg_PW("INCT not has buffer!\n");
        }
    }
    //start to recv from icnt//TODO change l2 icnt push decition
    //auto child_mf = static_cast<mem_fetch *>(::icnt_pop(global_l2_tlb_index));
    if (!icnt_response_buffer.empty()) //that is gain from l2tlb
    {
        auto child_mf = icnt_response_buffer.front();
        icnt_response_buffer.pop();
        if (child_mf)
        {
            auto mf_origin = child_mf->pw_origin;
            printdbg_PW("from icnt to pagewalker! origin mf:%llx,level:%u\n", mf_origin->get_virtual_addr(), std::get<0>(working_walker[mf_origin]));
            auto &level = std::get<0>(working_walker[mf_origin]);
            if (level == page_table_level::L1_LEAF)
            {

                printdbg_NEI("levle 1 come from inct!\n");
                printdbg_NEI("current waiting queue:\n");
                printdbg_NEI("before scan\n");
                PRINT_WAITINGBUFFER(waiting_buffer);
                printdbg_NEI("current walking wakjer:\n");
                PRINT_WORINGWORKER(working_walker);
                /* this code is wrong, guess why!?
                for (auto start = waiting_buffer.begin(); start != waiting_buffer.end(); start++)
                {
                    if (is_neighbor(std::get<1>(*start), mf_origin, level))
                    {
                        response_queue.push(std::get<1>(*start));
                        start = waiting_buffer.erase(start);
                    }
                }
                */
                //l1 requset come back
                if (m_config.enable_neighborhood)
                {

                    for (auto start = waiting_buffer.begin(); start != waiting_buffer.end();)
                    {
                        if (is_neighbor(std::get<1>(*start), mf_origin, level, m_config.enable_32_bit ? 32 : 64))
                        {
                            response_queue.push(std::get<1>(*start));
                            start = waiting_buffer.erase(start);
                        }
                        else
                        {
                            start++;
                        }
                    }
                }
                /* using namespace std::placeholders;
                
                auto get_from_tuple=[](std::tuple< bool, mem_fetch *, bool, page_table_level, addr_type> tp){
                    return std::get<1>(tp);
                };
                auto is_neibor_wrapper=std::bind(is_neighbor,mf_origin,std::bind(get_from_tuple,_1),level);
                waiting_buffer.remove_if(is_neibor_wrapper); */
                printdbg_NEI("after scan\n");
                printdbg_NEI("current waiting queue:\n");
                PRINT_WAITINGBUFFER(waiting_buffer);
                printdbg_NEI("current walking wakjer:\n");
                PRINT_WORINGWORKER(working_walker);
                assert(std::get<2>(working_walker[mf_origin]) == true);
                delete child_mf;
                total_mf--;

                working_walker.erase(mf_origin);

                response_queue.push(mf_origin);
                //assert(response_queue.size() < 10);
            }
            else
            {
                if (m_config.cache_assoc == 0 || m_config.cache_size == 0) //bypass pw  cache
                {
                    //auto child_mf = m_mshr->next_access();
                    auto mf_origin = child_mf->pw_origin;
                    assert(mf_origin != NULL);
                    auto &status = working_walker[mf_origin]; //fix bug, that should be reference!!!!!!
                    auto &level = std::get<0>(status);        //attention! it's reference not copy!!!
                    printdbg_NEI("mshr ready!\n");
                    printdbg_NEI("before scan\n");
                    printdbg_NEI("current waiting queue:\n");
                    PRINT_WAITINGBUFFER(waiting_buffer);
                    printdbg_NEI("current walking wakjer:\n");
                    PRINT_WORINGWORKER(working_walker);
                    //non-l1 come back
                    if (m_config.enable_neighborhood)
                    {
                        for (auto &mf_in_waiting : waiting_buffer)
                        {
                            if (is_neighbor(std::get<1>(mf_in_waiting), mf_origin, level, m_config.enable_32_bit ? 32 : 64))
                            {
                                if (is_neighbor(std::get<1>(mf_in_waiting), mf_origin, get_next_level(level), m_config.enable_32_bit ? 32 : 64))
                                {
                                    std::get<0>(mf_in_waiting) = true;
                                    std::get<2>(mf_in_waiting) = true;
                                    std::get<3>(mf_in_waiting) = get_next_level(level);
                                }
                                else
                                {
                                    std::get<0>(mf_in_waiting) = true;
                                    std::get<2>(mf_in_waiting) = false;
                                    std::get<3>(mf_in_waiting) = get_next_level(level);
                                }
                            }
                        }
                    }
                    printdbg_NEI("after scan\n");
                    printdbg_NEI("current waiting queue:\n");
                    PRINT_WAITINGBUFFER(waiting_buffer);
                    printdbg_NEI("current walking wakjer:\n");
                    PRINT_WORINGWORKER(working_walker);

                    delete child_mf;
                    total_mf--;

                    assert(level != page_table_level::L1_LEAF);

                    //it is not the last level, we need keep going.
                    //1,change working status,to next level, to next mf, and not outgoing
                    level = get_next_level(level); //change to next level,1

                    auto next_mf = mf_origin->get_copy();
                    total_mf++;

                    assert(total_mf <= m_config.walker_size);

                    set_pw_mf(next_mf, level, mf_origin);

                    std::get<1>(status) = next_mf; //set next mf;/2
                    assert(std::get<2>(status) == true);
                    std::get<2>(status) = false; //set next outgoing bit/3
                    ready_to_send.push(mf_origin);
                    assert(ready_to_send.size() <= m_config.walker_size);
                }
                else
                    fill(child_mf);
            }
        }
    }
    while (m_mshr->access_ready())
    {
        auto child_mf = m_mshr->next_access();
        auto mf_origin = child_mf->pw_origin;
        assert(mf_origin != NULL);
        auto &status = working_walker[mf_origin]; //fix bug, that should be reference!!!!!!
        auto &level = std::get<0>(status);        //attention! it's reference not copy!!!
        printdbg_NEI("mshr ready!\n");
        printdbg_NEI("before scan\n");
        printdbg_NEI("current waiting queue:\n");
        PRINT_WAITINGBUFFER(waiting_buffer);
        printdbg_NEI("current walking wakjer:\n");
        PRINT_WORINGWORKER(working_walker);
        //non-l1 come back
        if (m_config.enable_neighborhood)
        {
            for (auto &mf_in_waiting : waiting_buffer)
            {
                if (is_neighbor(std::get<1>(mf_in_waiting), mf_origin, level, m_config.enable_32_bit ? 32 : 64))
                {
                    if (is_neighbor(std::get<1>(mf_in_waiting), mf_origin, get_next_level(level), m_config.enable_32_bit ? 32 : 64))
                    {
                        std::get<0>(mf_in_waiting) = true;
                        std::get<2>(mf_in_waiting) = true;
                        std::get<3>(mf_in_waiting) = get_next_level(level);
                    }
                    else
                    { //some mf_origin is waiting for it!  be sure all the waiting mf are served.
                        std::get<0>(mf_in_waiting) = true;
                        std::get<2>(mf_in_waiting) = false;
                        std::get<3>(mf_in_waiting) = get_next_level(level);
                    }
                }
            }
        }
        printdbg_NEI("after scan\n");
        printdbg_NEI("current waiting queue:\n");
        PRINT_WAITINGBUFFER(waiting_buffer);
        printdbg_NEI("current walking wakjer:\n");
        PRINT_WORINGWORKER(working_walker);

        delete child_mf;
        total_mf--;

        assert(level != page_table_level::L1_LEAF);

        //it is not the last level, we need keep going.
        //1,change working status,to next level, to next mf, and not outgoing
        level = get_next_level(level); //change to next level,1

        auto next_mf = mf_origin->get_copy();
        total_mf++;

        assert(total_mf <= m_config.walker_size);

        set_pw_mf(next_mf, level, mf_origin);

        std::get<1>(status) = next_mf; //set next mf;/2
        assert(std::get<2>(status) == true);
        std::get<2>(status) = false; //set next outgoing bit/3
        ready_to_send.push(mf_origin);
        assert(ready_to_send.size() <= m_config.walker_size);
    }
}

void page_table_walker_config::init()
{
    global_bit = enable_32_bit ? 32 : 64;
}

bool real_page_table_walker::send(mem_fetch *mf)
{
    assert(waiting_buffer.size() < m_config.waiting_queue_size);

    if (waiting_buffer.size() < m_config.waiting_queue_size)
    {
        if (m_config.enable_range_pt)
        {
            range_cache_access++;
            auto is_hit_in_range = access_range<1>(mf->get_virtual_addr());

            if (is_hit_in_range)
            {
                range_cache_hit++;
                waiting_buffer.push_back(std::make_tuple(false, mf, false, page_table_level::L1_LEAF, 0));
            }
            else
            {
                range_cache_miss++;
                //check if the range exist in memory!
                auto is_hit = global_page_manager->is_in_range(mf->get_virtual_addr() & ~0x1fffff);
                if (is_hit)
                {
                    range_latency_queue.push(std::make_pair(mf->get_virtual_addr(), gpu_sim_cycle + gpu_tot_sim_cycle + 200));
                }
                waiting_buffer.push_back(std::make_tuple(false, mf, false, page_table_level::L4_ROOT, 0));
            }
            printdbg_PW("waiting queu,size is %lu\n", waiting_buffer.size());

            //assert(waiting_queue.size()<=m_config.waiting_queue_size);
            return true;
        }
        else
        {
            //not allow range_pt;
            waiting_buffer.push_back(std::make_tuple(false, mf, false, page_table_level::L4_ROOT, 0));
            return true;
        }
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
template <int lru_type>
void real_page_table_walker::fill_allocate_on_miss(mem_fetch *mf)
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
    auto time = gpu_sim_cycle + gpu_tot_sim_cycle;
    if (lru_type == 1)
    {
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
    else
    {
        //lru type==2
        //printdbg("\nfill, type2,blockaddr:%llx\n",block_addr);
        auto &set = new_tag_arrays[set_index];
        for (auto bg = set.begin(); bg != set.end(); bg++)
        {
            if ((*bg)->m_tag == tag)
            {
                assert((*bg)->get_status(mask) == RESERVED);
                (*bg)->fill(time, mask);
                auto tmp = *bg;
                //set.erase(bg);
                //set.push_front(tmp);
                return;
            }
        }
        assert(false); //can't reach there!!
    }
}
template <int lru_type>
void real_page_table_walker::fill_allocate_on_fill(mem_fetch *mf)
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
    auto time = gpu_sim_cycle + gpu_tot_sim_cycle;
    if (lru_type == 1)
    {
        auto fill_entry = find_entry_to_fill(start, end);
        (*fill_entry)->allocate(tag, block_addr, gpu_sim_cycle + gpu_tot_sim_cycle, mask);

        (*fill_entry)->fill(gpu_sim_cycle + gpu_tot_sim_cycle, mask);
    }
    else
    {

        //test;
        auto &set = new_tag_arrays[set_index];
        for (auto bg = set.begin(); bg != set.end(); bg++)
        {
            if ((*bg)->m_tag == tag)
            {
                //ok we find the entry!igore!
                //I think code won't run to here,
                assert(false); //for test, remember to disable  this check!!
            }
        }
        //miss

        auto ilevel = mf->trans_important_level;
        unsigned insert_position = ilevel == 1 ? m_config.i1_position : ilevel == 2 ? m_config.i2_position : m_config.i3_position;
        auto new_block = new line_cache_block();
        new_block->allocate(tag, block_addr, time, mem_access_sector_mask_t());
        new_block->fill(time, mem_access_sector_mask_t());
        if (set.size() < insert_position) //"1,1 no!,10,10,no,9,10,yes!"
            set.push_back(new_block);
        else
        {
            auto pos = std::next(set.begin(), insert_position - 1); //pos=1,add at begin!
            set.insert(pos, new_block);
            if (set.size() > m_config.cache_assoc)
            {
                delete set.back();
                set.pop_back();
                return;
            }
        }
    }
    //(*fill_entry)->set_last_access_time(time,mem_access_sector_mask_t());//otherwise the  access time will be 0!
}
