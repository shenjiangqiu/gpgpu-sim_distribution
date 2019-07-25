
#ifndef L1_TLB_H
#define L1_TLB_H
#include "../option_parser.h"
#include <string>
#include "gpu-cache.h"
#include "mem_fetch.h"
#include "page_manager.h"
#include <deque>
#include <utility>
#include <memory>
#include <unordered_set>
template <typename Iter>
Iter find_entry_to_fill(Iter start, Iter end)
{
    Iter fill_entry;
    bool have_free_line = false;
    Iter free_line;
    unsigned long long oldest_access_time = gpu_sim_cycle + gpu_tot_sim_cycle;
    while (start != end)
    {

        if (!(*start)->is_invalid_line()) //previouse bug: cant assume is_valid_line, that would filter reserved line out!!!
        {

            if ((*start)->get_last_access_time() < oldest_access_time)
            {
                oldest_access_time = (*start)->get_last_access_time();
                fill_entry = start;
            }
        }
        else
        { //any time find the valid line, get it!
            //all_reserved = false;
            if (have_free_line == false)
            {
                have_free_line = true;
                free_line = start;
            }
        }
        start++;
    }
    if (have_free_line)
        return free_line;
    else
        return fill_entry;
}

class l1_tlb_config
{
public:
    l1_tlb_config();
    virtual void init();
    virtual void reg_option(option_parser_t opp); //step:1 .reg 2, parse, 3. init
    friend class l1_tlb;

    unsigned m_page_size;
    unsigned m_page_size_log2;
    unsigned n_sets;
    unsigned n_associate;
    unsigned m_icnt_index; //l2 index;
    unsigned n_mshr_entries;
    unsigned n_mshr_max_merge;
    unsigned response_queue_size;
    unsigned miss_queue_size;
    bool allocate_on_fill;
};

enum class tlb_result
{
    hit,
    miss,
    hit_reserved,
    resfail
};

class l1_tlb
{
    using ull = unsigned long long;
    using us_mf_it = std::set<mem_fetch *>::iterator;

public:
    void invalidate()
    {
        for (unsigned i = 0; i < m_config.n_sets * m_config.n_associate; i++)
        {
            assert(m_tag_arrays[i]->get_status(mem_access_sector_mask_t()) != RESERVED);
            m_tag_arrays[i]->set_status(INVALID, mem_access_sector_mask_t());
        }
    }
    l1_tlb(l1_tlb_config &m_config, page_manager *, const std::string &name);
    l1_tlb() = delete;
    l1_tlb(l1_tlb &other) = delete;
    l1_tlb(l1_tlb &&other) = delete;
    void init();
    // template<int type>
    tlb_result access(mem_fetch *mf, unsigned long long time)
    {
        if (m_config.allocate_on_fill)
        {
            return access_alloc_on_fill(mf, time);
        }
        else
        {
            return access_alloc_on_miss(mf, time);
        }
    }

    tlb_result access_alloc_on_miss(mem_fetch *mf, unsigned long long time);
    tlb_result access_alloc_on_fill(mem_fetch *mf, unsigned long long time);

    mem_fetch *get_top_response();
    void pop_response();
    bool reponse_empty();
    void cycle();
    bool is_outgoing(mem_fetch *mf);
    void del_outgoing(mem_fetch *mf);
    void fill(mem_fetch *mf, unsigned long long time){
        if(m_config.allocate_on_fill){
            fill_allocate_on_fill(mf,time);
        }else{
            fill_allocate_on_miss(mf,time);
        }
    }
    void fill_allocate_on_miss(mem_fetch *mf, unsigned long long time);
    void fill_allocate_on_fill(mem_fetch *mf, unsigned long long time);

    unsigned outgoing_size();
    void print_stat(FILE *file) const
    {

        fprintf(file, "%s  access: %llu\n", name.c_str(), access_times);
        fprintf(file, "%s  hit: %llu\n", name.c_str(), hit_times);
        fprintf(file, "%s  miss: %llu\n", name.c_str(), miss_times);
        fprintf(file, "%s  resfail_all_res: %llu\n", name.c_str(), resfail_all_res_times);
        fprintf(file, "%s  resfail_entry_full: %llu\n", name.c_str(), resfail_mshr_entry_full_times);
        fprintf(file, "%s  resfail_merge: %llu\n", name.c_str(), resfail_mshr_merge_full_times);
        fprintf(file, "%s  resfail_missq: %llu\n", name.c_str(), resfail_mshr_missq_full_times);
    }

protected:
    // unsigned long long latency_dist[50]={0};

    unsigned id;
    l1_tlb_config m_config;       //init in constructor
    page_manager *m_page_manager; //init in constructor

    mshr_table *m_mshrs;
    std::vector<cache_block_t *> m_tag_arrays;
    std::deque<mem_fetch *> m_miss_queue;
    //enum mem_fetch_status m_miss_queue_status;
    std::deque<mem_fetch *> m_response_queue;
    std::set<mem_fetch *> outgoing_mf;

    ull access_times = 0;
    ull hit_times = 0;
    ull hit_reserved_times = 0;
    ull miss_times = 0;
    ull resfail_mshr_entry_full_times = 0;
    ull resfail_mshr_merge_full_times = 0;
    ull resfail_mshr_missq_full_times = 0;
    ull resfail_all_res_times = 0;

    std::string name;
};

class l1I_tlb_config : public l1_tlb_config
{
public:
    virtual void reg_option(option_parser_t opp);
};

#endif