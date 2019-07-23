#ifndef PAGE_TABALE_WALKER_HPP
#define PAGE_TABALE_WALKER_HPP
#include "mem_fetch.h"
#include <queue>
#include <tuple>
#include <memory>
#include "gpu-cache.h"
#include "l1_tlb.h"
#include "debug_macro.h"

class abstract_page_table_walker
{
public:
    virtual bool free() const = 0;  //can accecpt new request
    virtual bool ready() const = 0; //resonse_queue is ready to searve
    virtual void cycle() = 0;       //put all the valiable request to response_queue
    virtual bool send(mem_fetch *mf) = 0;
    //virtual bool recv_ready();
    virtual mem_fetch *recv() = 0;             //recv and pop;
    virtual mem_fetch *recv_probe() const = 0; //recv not
    virtual void send_to_recv_buffer(mem_fetch *mf) = 0;
    virtual void print_stat(FILE *file) const = 0;
    virtual void flush() = 0;
};
class latency_queue
{
public:
    latency_queue(unsigned long long latency, unsigned size);
    bool add(mem_fetch *v, unsigned long long current_time); //can be failed
    bool ready(unsigned long long time) const;
    mem_fetch *get() const;
    mem_fetch *pop();
    bool free() const;

private:
    unsigned m_size_limit;
    unsigned long long m_latency;
    std::queue<std::pair<unsigned long long, mem_fetch *>> m_elements;
};

class page_table_walker : public abstract_page_table_walker
{
public:
    page_table_walker(unsigned size, unsigned latency);
    virtual bool free() const override;  //can accecpt new request
    virtual bool ready() const override; //resonse_queue is ready to searve
    virtual void cycle() override;       //put all the valiable request to response_queue
    virtual bool send(mem_fetch *mf) override;
    //virtual bool recv_ready() override;
    virtual mem_fetch *recv() override;             //recv and pop override;
    virtual mem_fetch *recv_probe() const override; //recv not
    virtual void print_stat(FILE *file) const override {}

private:
    latency_queue m_latency_queue;
    std::queue<mem_fetch *> m_response_queue;
};

struct page_table_walker_config
{
    unsigned m_icnt_index; //TODO remember to set this !!!
    unsigned waiting_queue_size;
    unsigned walker_size;
    unsigned cache_size;
    unsigned cache_assoc;
    unsigned mshr_size;
    unsigned mshr_max_merge;
    unsigned m_range_size;
    bool enable_neighborhood;
    bool enable_range_pt;
    bool enable_32_bit;
    void reg_option(option_parser_t opp);
    void init();
};
//static constexpr addr_type neighbor_masks[4] = {0xFF8000000000, 0x7FC0000000, 0x3FE00000, 0x1FF000};

static constexpr unsigned long long masks_accumulate_64[4] = {0xFF8000000000, 0xFFFFC0000000, 0xFFFFFFE00000, 0xFFFFFFFFF000};
static constexpr unsigned long long masks_accumulate_32[4] = {0xF8000000, 0xFFC00000, 0xFFFE0000, 0xFFFFF000};
static constexpr unsigned mask_offset_64[4] = {39, 30, 21, 12};
static constexpr unsigned mask_offset_32[4] = {27, 22, 17, 12};

class real_page_table_walker : public abstract_page_table_walker
{
public:
    real_page_table_walker(page_table_walker_config m_config);
    virtual bool free() const override
    {

        return waiting_buffer.size() < m_config.waiting_queue_size;
    }                                    //can accecpt new request
    virtual bool ready() const override; //resonse_queue is ready to searve
    virtual void cycle() override;       //put all the valiable request to response_queue
    virtual bool send(mem_fetch *mf) override;
    //virtual bool recv_ready() override;
    virtual mem_fetch *recv() override;             //recv and pop override;
    virtual mem_fetch *recv_probe() const override; //recv not
    tlb_result access(mem_fetch *mf);
    virtual void send_to_recv_buffer(mem_fetch *mf)
    {
        icnt_response_buffer.push(mf);
    }
    virtual void print_stat(FILE *file) const override
    {

        fprintf(file, "pwc_access: %llu\n", access_times);
        fprintf(file, "pwc_hit: %llu\n", hit_times);
        fprintf(file, "pwc_miss: %llu\n", miss_times);
        fprintf(file, "pwc_resfail_all_res: %llu\n", resfail_all_res_times);
        fprintf(file, "pwc_resfail_entry_full: %llu\n", resfail_mshr_entry_full_times);
        fprintf(file, "pwc_resfail_merge: %llu\n", resfail_mshr_merge_full_times);
        fprintf(file, "pwc_resfail_missq: %llu\n", resfail_mshr_missq_full_times);

        fprintf(file, "range cache access: %llu\n", range_cache_access);
        fprintf(file, "range cache hit: %llu\n", range_cache_hit);
        fprintf(file, "range cache miss: %llu\n", range_cache_miss);
    }
    virtual void flush() override
    {
        if (working_walker.size() != 0 or waiting_buffer.size() != 0)
        {
            throw "error,when kernel end, this should be empty!";
        }
        for (auto blk : m_tag_arrays)
        {
            blk->set_status(INVALID, mem_access_sector_mask_t());
        }
    }

private:
    void fill(mem_fetch *mf); //fill pw cache;
    page_table_walker_config m_config;
    std::vector<std::list<cache_block_t *>> new_tag_arrays; //new algorithms to replace lru!

    std::vector<cache_block_t *> m_tag_arrays;
    mshr_table *m_mshr;
    //std::vector<std::tuple<addr_type,unsigned>> working_walker; old design

    std::map<mem_fetch *, std::tuple<page_table_level, mem_fetch *, bool>> working_walker;
    std::queue<mem_fetch *> response_queue; //response to l2 tlb
    //std::queue<mem_fetch *> waiting_queue;        //from l2 tlb
    std::queue<mem_fetch *> miss_queue;           //send to icnt
    std::queue<mem_fetch *> ready_to_send;        //it's virtual,dosn't exist in real hardware
    std::queue<mem_fetch *> icnt_response_buffer; //recv from icnt
    std::list<std::tuple<bool, mem_fetch *, bool, page_table_level, addr_type>> waiting_buffer;
    //0:coaled  1:mf  2:wating?  3:level  4:addr

    static bool is_neighbor(const mem_fetch *origin, const mem_fetch *target, page_table_level the_level, unsigned addr_space)
    {
        printdbg_NEI("try to judge if it's neiborhood\n");
        auto num_leve = (unsigned)the_level;
        addr_type addr_origin;
        addr_type addr_target;
        if (addr_space == 64)
        {
            addr_origin = (origin->get_virtual_addr() & masks_accumulate_64[num_leve]) >> mask_offset_64[num_leve];
            addr_target = (target->get_virtual_addr() & masks_accumulate_64[num_leve]) >> mask_offset_64[num_leve];
        }
        else
        {
            addr_origin = (origin->get_virtual_addr() & masks_accumulate_32[num_leve]) >> mask_offset_32[num_leve];
            addr_target = (target->get_virtual_addr() & masks_accumulate_32[num_leve]) >> mask_offset_32[num_leve];
        }
        if ((addr_origin ^ addr_target) <= 15ull) //128 byte cache line contains 8 entries of pt
        {
            printdbg_NEI("YES:It is:addr 1: %llx,2:%llx,level:%u\n", origin->get_virtual_addr(), target->get_virtual_addr(), 4 - (unsigned)the_level);

            return true;
        }
        else
        {
            printdbg_NEI("NO\n");
            return false;
        }
    }

    using ull = unsigned long long;
    // std::get<0>: vir_addr;std::get<1>: pt_physic_addr,std::get<2>: size,std::get<3>: last access time;//allocate on fill
    using pt_range_cache = std::tuple<addr_type, addr_type, unsigned, unsigned long long, bool>;
    unsigned m_range_cache_size;
    std::list<pt_range_cache> m_range_cache;
    //queue for record incoming request
    std::queue<std::pair<addr_type, unsigned long long>> range_latency_queue;
    void fill_to_range_cache(addr_type addr)
    {
        auto range_entry = global_page_manager->get_range_entry(addr & ~(global_bit == 32 ? 0x1ffff : 0x1fffff)); //different mask
        if (m_range_cache.empty())
        {
            m_range_cache.push_back(std::make_tuple(std::get<0>(range_entry),
                                                    std::get<1>(range_entry),
                                                    std::get<2>(range_entry),
                                                    gpu_sim_cycle + gpu_tot_sim_cycle,
                                                    true));
        }
        else if (access_range<0>(addr))
        { //hit
            //it' s in the cache already, do nothing
        }
        else if (m_range_cache.size() < m_range_cache_size)
        { //miss and add
            m_range_cache.push_back(std::make_tuple(std::get<0>(range_entry),
                                                    std::get<1>(range_entry),
                                                    std::get<2>(range_entry),
                                                    gpu_sim_cycle + gpu_tot_sim_cycle,
                                                    true));
        }
        else
        { //miss and replace
            //find the oldeast;
            unsigned long long oldest = gpu_sim_cycle + gpu_tot_sim_cycle;
            decltype(m_range_cache.begin()) oldest_itr;
            for (auto start = m_range_cache.begin(); start != m_range_cache.end(); start++)
            {
                if (std::get<4>(*start))
                {
                    if (std::get<3>(*start) < oldest)
                    {
                        oldest = std::get<3>(*start);
                        oldest_itr = start;
                    }
                }
            }
            m_range_cache.erase(oldest_itr);

            m_range_cache.push_back(std::make_tuple(std::get<0>(range_entry),
                                                    std::get<1>(range_entry),
                                                    std::get<2>(range_entry),
                                                    gpu_sim_cycle + gpu_tot_sim_cycle,
                                                    true));
        }
    }
    template <int N>
    bool access_range(addr_type virtual_addr)
    {
        ///range_cache_access++;
        if (N == 0)
        {
            printdbg_PTRNG("\n\n-----start to access range_cache, not real!!,just fill test!-----\n");
        }
        else
        {
            printdbg_PTRNG("\n\n-----start to access range_cache, It's real!!,!!!----------------\n");
        }

        virtual_addr &= ~(global_bit == 32 ? 0x1ffff : 0x1fffff);
        if (m_range_cache.empty())
        {
            //if(N==0)
            printdbg_PTRNG("access but range empty!\n");
            //range_cache_miss++;
            if (N == 0)
            {
                printdbg_PTRNG("\n\n-----end accessing!, not real!!,just fill test!-----\n");
            }
            else
            {
                printdbg_PTRNG("\n\n-----end accessing!, It's real!!,!!!----------------\n");
            }

            return false;
        }

        printdbg_PTRNG("current cache size:%lu __ access good,addr:%llx,v_addr,index:%llu\n ", m_range_cache.size(), virtual_addr, virtual_addr >> (global_bit == 32 ? 17 : 21));

        PRINTRNG_CACHE(m_range_cache);
        for (auto entry : m_range_cache)
        {
            printdbg_PTRNG("acces here!\n");
            auto v_addr = std::get<0>(entry);
            auto sz = std::get<2>(entry);
            auto valid = std::get<4>(entry);
            if (valid)
            {
                auto gap = (virtual_addr - v_addr) >> (global_bit == 32 ? 17 : 21);
                printdbg("gap=%llu\n", gap);
                if (gap >= 0 and gap < sz)
                { //size=2,gap=1 good, size=2 gap=2 not good!!!
                    std::get<3>(entry) = gpu_sim_cycle + gpu_tot_sim_cycle;
                    //range_cache_hit++;
                    if (N == 0)
                    {
                        printdbg_PTRNG("\n\n-----end accessing!, not real!!,just fill test!-----\n");
                    }
                    else
                    {
                        printdbg_PTRNG("\n\n-----end accessing!, It's real!!,!!!----------------\n");
                    }
                    return true;
                }
            }
        }
        //range_cache_miss++;
        if (N == 0)
        {
            printdbg_PTRNG("\n\n-----end accessing!, not real!!,just fill test!-----\n");
        }
        else
        {
            printdbg_PTRNG("\n\n-----start accessing!, It's real!!,!!!----------------\n");
        }
        return false;
    }

    unsigned long long range_cache_access = 0;
    unsigned long long range_cache_hit = 0;
    unsigned long long range_cache_miss = 0;

    ull access_times = 0;
    ull hit_times = 0;
    ull hit_reserved_times = 0;
    ull miss_times = 0;
    ull resfail_mshr_entry_full_times = 0;
    ull resfail_mshr_merge_full_times = 0;
    ull resfail_mshr_missq_full_times = 0;
    ull resfail_all_res_times = 0;
};

#endif