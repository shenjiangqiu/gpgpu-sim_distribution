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
    void reg_option(option_parser_t opp);
    void init();
};
//static constexpr addr_type neighbor_masks[4] = {0xFF8000000000, 0x7FC0000000, 0x3FE00000, 0x1FF000};
static constexpr addr_type masks_accumulate[4] = {0xFF8000000000, 0xFFFFC0000000, 0xFFFFFFE00000, 0xFFFFFFFFF000};
static constexpr unsigned mask_offset[4] = {39, 30, 21, 12};

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
        fprintf(file, "pw cache access: %llu\n", access_times);
        fprintf(file, "pw cache hit: %llu\n", hit_times);
        fprintf(file, "pw cache miss: %llu\n", miss_times);
        fprintf(file, "pw cache resfail: %llu\n", resfail_times);
    }
    virtual void flush() override
    {
        if(working_walker.size()!=0 or waiting_buffer.size()!=0){
            throw "error,when kernel end, this should be empty!";
        }
        for(auto blk:m_tag_arrays){
            blk->set_status(INVALID,mem_access_sector_mask_t());
        }
        
    }

private:
    void fill(mem_fetch *mf); //fill pw cache;
    page_table_walker_config m_config;
    std::vector<cache_block_t*> m_tag_arrays;
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

    static bool is_neighbor(const mem_fetch *origin, const mem_fetch *target, page_table_level the_level)
    {
        printdbg_NEI("try to judge if it's neiborhood\n");
        auto num_leve = (unsigned)the_level;
        auto addr_origin = (origin->get_virtual_addr() & masks_accumulate[num_leve]) >> mask_offset[num_leve];
        auto addr_target = (target->get_virtual_addr() & masks_accumulate[num_leve]) >> mask_offset[num_leve];
        if ((addr_origin ^ addr_target) <= 15ull) //128 byte cache line contains 8 entries of pt
        {
            printdbg_NEI("YES:It is:addr 1: %llx,2:%llx,level:%u\n", origin->get_virtual_addr(), target->get_virtual_addr(), 4-(unsigned)the_level);

            return true;
        }
        else
        {
            printdbg_NEI("NO\n");
            return false;
        }
    }

    using ull = unsigned long long;
    ull access_times;
    ull hit_times;
    ull miss_times;
    ull resfail_times;
};

#endif