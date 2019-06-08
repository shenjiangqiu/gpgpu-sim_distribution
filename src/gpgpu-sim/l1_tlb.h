#ifndef L1_TLB_H
#define L1_TLB_H
#include"../option_parser.h"
#include<string>
#include"gpu-cache.h"
#include"mem_fetch.h"
#include "page_manager.hpp"
#include<deque>
#include <utility>
#include<memory>
#include<unordered_set>
class l1_tlb_config{
    public:
    l1_tlb_config();
    virtual void init();
    virtual void reg_option(option_parser_t opp);//step:1 .reg 2, parse, 3. init
    friend class l1_tlb;

    unsigned m_page_size;
    unsigned m_page_size_log2;
    unsigned n_sets;
    unsigned n_associate;
    unsigned m_icnt_index;//l2 index;
    unsigned n_mshr_entries;
    unsigned n_mshr_max_merge;
    unsigned response_queue_size;
    unsigned miss_queue_size;
};

enum class tlb_result{
    hit,miss,hit_reserved,resfail
};

class l1_tlb{
    using us_mf_it=std::unordered_set<mem_fetch*>::iterator;
    
    public:
    l1_tlb(l1_tlb_config &m_config,page_manager* );
    l1_tlb()=delete;
    l1_tlb(l1_tlb& other)=delete;
    l1_tlb(l1_tlb&& other)=delete;
    void init();
    tlb_result access(mem_fetch* mf,unsigned time);
    mem_fetch* get_top_response();
    void pop_response();
    bool reponse_empty();
    void cycle();
    bool is_outgoing(mem_fetch* mf);
    void del_outgoing(mem_fetch* mf);
    void fill(mem_fetch* mf,unsigned long long time);
    unsigned outgoing_size();

    
    protected:
    l1_tlb_config m_config;//init in constructor
    page_manager* m_page_manager;//init in constructor
    
	std::shared_ptr<mshr_table> m_mshrs;
    std::vector<std::shared_ptr<cache_block_t>> m_tag_arrays;
    std::deque<mem_fetch*> m_miss_queue;
    //enum mem_fetch_status m_miss_queue_status;
    std::deque<mem_fetch*>  m_response_queue;
    std::unordered_set<mem_fetch*> outgoing_mf;

};

class l1I_tlb_config: public l1_tlb_config{
    public:
    virtual void reg_option(option_parser_t opp);
};

#endif