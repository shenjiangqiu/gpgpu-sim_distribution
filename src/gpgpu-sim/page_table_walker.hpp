#ifndef PAGE_TABALE_WALKER_HPP
#define PAGE_TABALE_WALKER_HPP
#include "mem_fetch.h"
#include <queue>
#include <tuple>
#include <memory>
#include "gpu-cache.h"
#include "l1_tlb.h"
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
    virtual void send_to_recv_buffer(mem_fetch* mf)=0;

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

class real_page_table_walker : public abstract_page_table_walker
{
public:
    real_page_table_walker(page_table_walker_config m_config);
    virtual bool free() const override;  //can accecpt new request
    virtual bool ready() const override; //resonse_queue is ready to searve
    virtual void cycle() override;       //put all the valiable request to response_queue
    virtual bool send(mem_fetch *mf) override;
    //virtual bool recv_ready() override;
    virtual mem_fetch *recv() override;             //recv and pop override;
    virtual mem_fetch *recv_probe() const override; //recv not
    tlb_result access(mem_fetch *mf);
    virtual void send_to_recv_buffer(mem_fetch* mf){
        icnt_response_buffer.push(mf);
    }
private:
    page_table_walker_config m_config;
    std::vector<std::shared_ptr<cache_block_t>> m_tag_arrays;
    mshr_table *m_mshr;
    //std::vector<std::tuple<addr_type,unsigned>> working_walker; old design

    std::unordered_map<mem_fetch *, std::tuple<page_table_level, mem_fetch *, bool>> working_walker;
    std::queue<mem_fetch *> response_queue;       //response to l2 tlb
    std::queue<mem_fetch *> waiting_queue;        //from l2 tlb
    std::queue<mem_fetch *> miss_queue;           //send to icnt
    std::queue<mem_fetch *> ready_to_send;        //it's virtual,dosn't exist in real hardware
    std::queue<mem_fetch *> icnt_response_buffer; //recv from icnt
};

#endif