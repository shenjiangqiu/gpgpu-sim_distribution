#ifndef L2_TLB_HPP
#define L2_TLB_HPP
#include"l1_tlb.h"
#include"../option_parser.h"
#include<string>
#include"gpu-cache.h"
#include"mem_fetch.h"
#include "page_manager.hpp"
#include"page_table_walker.hpp"
#include<deque>
#include <utility>
#include<memory>
#include<unordered_set>
extern unsigned global_n_cores;

extern unsigned global_l2_tlb_index;
extern unsigned global_walkers;
class l2_tlb_config{
    public:
    l2_tlb_config();
    void init();
    void reg_option(option_parser_t opp);//step:1 .reg 2, parse, 3. init
    friend class l2_tlb;
    void set_icnt_index(unsigned idx);
    private:
    unsigned m_page_size;
    unsigned m_page_size_log2;
    unsigned n_sets;
    unsigned n_associate;
    unsigned m_icnt_index;
    unsigned n_mshr_entries;
    unsigned n_mshr_max_merge;
    unsigned response_queue_size;
    unsigned miss_queue_size;
    unsigned recv_buffer_size;
    unsigned m_pw_size;
    unsigned m_pw_latency;
    page_table_walker_config m_page_table_walker_config;
    //TODO add pw config, add regoption add init
};

class l2_tlb{
    using ull=unsigned long long;
    //TODO add abstract_page_walker, inti page_walker.
    using us_mf_it=std::unordered_set<mem_fetch*>::iterator;
    
    public:
    void invalidate(){
        for(unsigned i=0;i<m_config.n_sets*m_config.n_associate;i++){
            m_tag_arrays[i]->set_status(INVALID,mem_access_sector_mask_t());

        }
    }
    l2_tlb(l2_tlb_config m_config);
    void init();
    tlb_result access(mem_fetch* mf,unsigned time);
    mem_fetch* get_top_response();
    void pop_response();
    bool reponse_empty();
    void cycle();
    bool is_outgoing(mem_fetch* mf);
    void del_outgoing(mem_fetch* mf);
    void fill(mem_fetch* mf,unsigned long long time);

    void print_stat(FILE *file) const
    {
        fprintf(file, "l2tlb  access: %llu\n", access_times);
        fprintf(file, "l2tlb  hit: %llu\n", hit_times);
        fprintf(file, "l2tlb  miss: %llu\n", miss_times);
        fprintf(file, "l2tlb  resfail: %llu\n", resfail_times);

        m_page_table_walker->print_stat(file);
    }

protected:
    l2_tlb_config m_config;//init in constructor
    abstract_page_table_walker* m_page_table_walker;
    page_manager* m_page_manager;//init in constructor
    
	mshr_table* m_mshrs;
    std::vector<cache_block_t*> m_tag_arrays;
    std::deque<mem_fetch*> m_miss_queue;
    //enum mem_fetch_status m_miss_queue_status;
    std::deque<mem_fetch*>  m_response_queue;
    std::unordered_set<mem_fetch*> outgoing_mf;//we do not use multiple set
    std::queue<mem_fetch*> m_recv_buffer;

    ull access_times;
    ull hit_times;
    ull miss_times;
    ull resfail_times;
};

#endif