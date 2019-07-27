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
    bool using_new_lru;
    bool allocate_on_fill;
    unsigned i1_position;
    unsigned i2_position;
    unsigned i3_position;
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
    template <int lru_type>
    tlb_result access(mem_fetch *child_mf)
    {
        if (m_config.allocate_on_fill)
            return access_allocate_on_fill<lru_type>(child_mf);
        else
            return access_allocate_on_miss<lru_type>(child_mf);
    }
    template <int lru_type>
    tlb_result access_allocate_on_fill(mem_fetch *child_mf) //start to redesin the LRU replacement poly
    {
        auto parent_mf = child_mf->pw_origin;
        auto ilevel = parent_mf->trans_important_level;

        // assert(mf != nullptr);

        /* code */

        //printdbg_PW("the entry:1:tag:%llx,2:tag:%llx\n", m_tag_arrays[61 * 2] ? m_tag_arrays[61 * 2]->m_tag : 0, m_tag_arrays[61 * 2] ? m_tag_arrays[61 * 2 + 1]->m_tag : 0);
        access_times++;
        auto addr = child_mf->get_physic_addr();
        auto n_set = m_config.cache_size;
        auto n_assoc = m_config.cache_assoc;

        // get the set_index and tag for searching the tag array.
        auto set_index = (addr >> 3) & static_cast<addr_type>(n_set - 1); //first get the page number, then get the cache index.
        auto tag = addr & (~static_cast<addr_type>(8 - 1));               //only need the bits besides page offset
        assert(tag == addr);
        auto block_addr = addr >> 3;
        unsigned long long oldest_access_time = gpu_sim_cycle + gpu_tot_sim_cycle;
        auto time = oldest_access_time;
        auto mask = child_mf->get_access_sector_mask();
        // bool all_reserved = true;
        auto start = m_tag_arrays.begin() + n_assoc * set_index;
        auto end = start + n_assoc;
        if (lru_type == 1)
        {
            for (; start < end; start++)
            {
                if (!(*start)->is_invalid_line())
                {
                    if ((*start)->m_tag == tag) //ok we find// this is what the identity entry/try to find that entry
                    {

                        // all_reserved = false;

                        //m_response_queue.push_front(mf); //only at this time ,we need push front, and we can pop front now.

                        //assert(m_response_queue.size() < 100);
                        (*start)->set_last_access_time(time, mask);
                        hit_times++;
                        return tlb_result::hit;
                    }
                }
            }
            // when run to here, means no hit line found,It's a miss;

            unsigned reason;
            if (m_mshr->full(block_addr, reason))
            {
                reason == 1 ? resfail_mshr_merge_full_times++ : resfail_mshr_entry_full_times++;
                return tlb_result::resfail;
            }

            //printdbg_PW("the entry:1:tag:%llx,2:tag:%llx\n", m_tag_arrays[61 * 2] ? m_tag_arrays[61 * 2]->m_tag : 0, m_tag_arrays[61 * 2] ? m_tag_arrays[61 * 2 + 1]->m_tag : 0);
            bool already_in_mshr = m_mshr->probe(block_addr);

            m_mshr->add<1>(block_addr, child_mf);
            if (!already_in_mshr)
                miss_queue.push(child_mf);
            // outgoing_mf.insert(mf);
            //printdbg_tlb("outgoing insert! size:%lu\n", outgoing_mf.size());
            miss_times++;
            return tlb_result::miss;
        }
        else //type2 and allocate on fill, we only need to check , don't evict anything
        {
            auto &set = new_tag_arrays[set_index];
            for (auto bg = set.begin(); bg != set.end(); bg++)
            {
                if ((*bg)->m_tag == tag)
                {
                    (*bg)->set_last_access_time(time, mem_access_sector_mask_t());
                    auto entry = *bg;
                    set.erase(bg);
                    set.push_front(entry);
                    hit_times++;
                    return tlb_result::hit;
                }
            }
            unsigned reason;
            if (m_mshr->full(block_addr, reason))
            {
                reason == 1 ? resfail_mshr_merge_full_times++ : resfail_mshr_entry_full_times++;
                return tlb_result::resfail;
            }

            //printdbg_PW("the entry:1:tag:%llx,2:tag:%llx\n", m_tag_arrays[61 * 2] ? m_tag_arrays[61 * 2]->m_tag : 0, m_tag_arrays[61 * 2] ? m_tag_arrays[61 * 2 + 1]->m_tag : 0);
            bool already_in_mshr = m_mshr->probe(block_addr);

            m_mshr->add<1>(block_addr, child_mf);
            if (!already_in_mshr)
                miss_queue.push(child_mf);
            // outgoing_mf.insert(mf);
            //printdbg_tlb("outgoing insert! size:%lu\n", outgoing_mf.size());
            miss_times++;
            return tlb_result::miss;
        }
    }

    template <int lru_type>
    tlb_result access_allocate_on_miss(mem_fetch *child_mf) //start to redesin the LRU replacement poly
    {
        auto parent_mf = child_mf->pw_origin;
        auto ilevel = parent_mf->trans_important_level;

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
            unsigned long long oldest_access_time = gpu_sim_cycle + gpu_tot_sim_cycle;
            auto time = oldest_access_time;
            auto mask = child_mf->get_access_sector_mask();
            bool all_reserved = true;
            bool has_free_line = false;
            access_times++;
            if (lru_type == 1)
            {
                auto start = m_tag_arrays.begin() + n_assoc * set_index;
                auto end = start + n_assoc;
                //to find a place to access

                decltype(start) free_line;
                decltype(start) hit_line;
                decltype(start) last_line;

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
                            assert(time - (*start)->get_alloc_time() < 50000);
                        }

                        if ((*start)->m_tag == tag) //ok we find
                        {

                            auto status = (*start)->get_status(mask);
                            switch (status)
                            {
                            case RESERVED:
                            {
                                unsigned reason;
                                if (m_mshr->full(block_addr, reason))
                                {
                                    printdbg_tlb("reserved! and mshr full\n");
                                    reason == 1 ? resfail_mshr_merge_full_times++ : resfail_mshr_entry_full_times++;
                                    return tlb_result::resfail;
                                }
                                else
                                {
                                    printdbg_tlb("hit reserved! add to mfshr\n");

                                    m_mshr->add<2>(block_addr, child_mf); //not new

                                    //ready_to_send.pop();
                                    (*start)->set_last_access_time(time, mask);
                                    hit_reserved_times++;
                                    return tlb_result::hit_reserved;
                                }
                                break;
                            }
                            case VALID:
                            {
                                printdbg_tlb("child mf hit: mf:%llX\n", child_mf->get_virtual_addr());
                                hit_times++;
                                (*start)->set_last_access_time(time, mask);

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
                if (m_mshr->full(block_addr, reason))
                {
                    reason == 1 ? resfail_mshr_merge_full_times++ : resfail_mshr_entry_full_times++;
                    return tlb_result::resfail;
                }
                auto next_line = has_free_line ? free_line : last_line;
                (*next_line)->allocate(tag, block_addr, time, mask);
                m_mshr->add<1>(block_addr, child_mf);
                miss_queue.push(child_mf);
                assert(miss_queue.size() < 10);
                //ready_to_send.pop();
                miss_times++;
                //printdbg_tlb("outgoing insert! size:%lu\n", outgoing_mf.size());
                return tlb_result::miss;
            }
            else
            { //lru_type==2
                // assert(0 && "cannot go to here now!");
                // access_times++;
                //printdbg("\nnew lru enter!\n");
                auto &set = new_tag_arrays[set_index];
                for (auto bg = set.begin(); bg != set.end(); bg++)
                {
                    if ((*bg)->m_tag == tag)
                    {
                        if ((*bg)->is_valid_line())
                        {
                            auto entry = *bg;
                            set.erase(bg);
                            set.push_front(entry);
                            entry->set_last_access_time(time,mem_access_sector_mask_t());
                            hit_times++;
                            //printdbg("hit!mf:%llx\n,blockaddr:%llx\n",child_mf->get_physic_addr(),block_addr);
                            return tlb_result::hit;
                        }
                        else
                        { //it's reserved
                            unsigned reason;
                            if(m_mshr->full(block_addr,reason)){
                                reason == 1 ? resfail_mshr_merge_full_times++ : resfail_mshr_entry_full_times++;
                                //printdbg("resfail!mf:%llx\n,blockaddr:%llx\n",child_mf->get_physic_addr(),block_addr);

                                return tlb_result::resfail;
                            }
                            assert(((*bg)->is_reserved_line()));
                            auto entry = *bg;
                            set.erase(bg);
                            set.push_front(entry);
                            entry->set_last_access_time(time,mem_access_sector_mask_t());
                            hit_reserved_times++;
                            m_mshr->add<1>(block_addr,child_mf);
                            //printdbg("hit reserved!mf:%llx\n,blockaddr:%llx\n",child_mf->get_physic_addr(),block_addr);

                            return tlb_result::hit_reserved;
                        }
                    }
                }

                //miss

                unsigned reason;
                if (m_mshr->full(block_addr, reason))
                {
                    reason == 1 ? resfail_mshr_merge_full_times++ : resfail_mshr_entry_full_times++;
                            //printdbg("resfail!mf:%llx\n,blockaddr:%llx\n",child_mf->get_physic_addr(),block_addr);

                    return tlb_result::resfail;
                }


                unsigned insert_position = ilevel == 1 ? m_config.i1_position : ilevel == 2 ? m_config.i2_position : m_config.i3_position;
                auto new_line=new line_cache_block();
                new_line->allocate(tag,block_addr,time,mem_access_sector_mask_t());
                m_mshr->add(block_addr,child_mf);
                miss_queue.push(child_mf);
                if (set.size() < insert_position)
                {
                    set.push_back(new_line);
                            //printdbg("miss and insert to last!mf:%llx\n,blockaddr:%llx\n",child_mf->get_physic_addr(),block_addr);

                    //set.back()->allocate(tag, block_addr, time, mask);
                    miss_times++;
                    return tlb_result::miss;
                }
                else
                {
                    auto start = set.begin();
                    auto position_dest = std::next(start, insert_position - 1);
                    auto new_pos = set.insert(position_dest, new_line);
                    //(*new_pos)->allocate(tag, block_addr, time, mask);

                    miss_times++;
                    if(set.size()>m_config.cache_assoc){
                        delete set.back();
                        set.pop_back();
                    }
                            //printdbg("miss and insert to position:%u,mf::%llx\n,blockaddr:%llx\n",insert_position, child_mf->get_physic_addr(),block_addr);

                    return tlb_result::miss;
                }

                return tlb_result::miss;
            }
        }
    }
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
        fprintf(file, "pwc_resfail_mshr_entry_full: %llu\n", resfail_mshr_entry_full_times);
        fprintf(file, "pwc_resfail_mshr_merge: %llu\n", resfail_mshr_merge_full_times);
        fprintf(file, "pwc_resfail_missq: %llu\n", resfail_mshr_missq_full_times);

        fprintf(file, "range cache access: %llu\n", range_cache_access);
        fprintf(file, "range cache hit: %llu\n", range_cache_hit);
        fprintf(file, "range cache miss: %llu\n", range_cache_miss);
        fprintf(file, "range max size: %u\n", m_config.m_range_size);
        unsigned long all_ranges = 0;
        auto set = global_page_manager->m_range_page_table.get_m_set();
        for (auto entry : set)
        {
            if (std::get<2>(entry) > 1)
            {
                all_ranges++;
            }
        }

        fprintf(file, "max range built: %lu\n", all_ranges);
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
    void fill(mem_fetch *mf)
    {
        if (m_config.using_new_lru)
        {
            if (m_config.allocate_on_fill)
                fill_allocate_on_fill<2>(mf); //new
            else
                fill_allocate_on_miss<2>(mf); //new
        }
        else
        {
            if (m_config.allocate_on_fill)
                fill_allocate_on_fill<1>(mf); //old
            else
                fill_allocate_on_miss<1>(mf); //old
        }
    }
    template <int lru_type>
    void fill_allocate_on_fill(mem_fetch *mf); //fill pw cache;
    template <int lru_type>
    void fill_allocate_on_miss(mem_fetch *mf); //fill pw cache;

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
        {
            //miss and replace
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
                printdbg_PTRNG("gap=%llu\n", gap);
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