#include "l1_tlb.h"
#include "gpu-cache.h"
#include "icnt_wrapper.h"
#include <deque>
extern unsigned long long gpu_sim_cycle;
extern unsigned long long gpu_tot_sim_cycle;
#define SJQDEBUG
#ifdef SJQDEBUG
#define printdbg(...)                                                                                 \
    do                                                                                                \
    {                                                                                                 \
        printf("%s:%d:%s:%llu____", __FILE__, __LINE__, __func__, gpu_sim_cycle + gpu_tot_sim_cycle); \
        printf(__VA_ARGS__);                                                                          \
    } while (0)
#elif
#define printdbg(x)
#endif
l1_tlb::l1_tlb(l1_tlb_config config, std::shared_ptr<page_manager> tlb_page_manager) : m_config(config),
                                                                                       m_page_manager(tlb_page_manager),
                                                                                       m_mshrs(std::make_shared<mshr_table>(config.n_mshr_entries, config.n_mshr_max_merge)),
                                                                                       m_tag_arrays(config.n_sets * config.n_associate)
{
    for (unsigned i = 0; i < config.n_sets * config.n_associate; i++)
    {
        m_tag_arrays[i] = std::make_shared<line_cache_block>();
    }
}
l1_tlb_config::l1_tlb_config() {} //in constructor, just allocate the memory, and then parse configur,then

void l1_tlb_config::init()
{
    assert(m_page_size > 0);
    if (m_page_size == 4096)
        m_page_size_log2 = 12;
    else
    {
        throw std::runtime_error("can't accept other page size now!");
    }
}
void l1_tlb_config::parse_option(option_parser_t opp)
{

    option_parser_register(opp, "-l1tlbsets", option_dtype::OPT_INT32, &n_sets, "the sets of l1 tlb", "64");
    option_parser_register(opp, "-l1tlbassoc", option_dtype::OPT_UINT32, &n_associate, "the set associate", "2");
    option_parser_register(opp, "-l1tlb_mshr_entries", option_dtype::OPT_UINT32, &n_mshr_entries, "the number of mshr entries", "16");
    option_parser_register(opp, "-l1tlb_mshr_maxmerge", option_dtype::OPT_UINT32, &n_mshr_max_merge, "the max resqust that mshr can merge", "8");
    option_parser_register(opp, "-l1tlb_response_queue_size", option_dtype::OPT_UINT32, &response_queue_size, "the size of response queue: 0=unlimited", "0");
    option_parser_register(opp, "-l1tlb_miss_queue_size", option_dtype::OPT_UINT32, &miss_queue_size, "the size of miss queue: 0=unlimited", "0");
    option_parser_register(opp, "-l1tlb_page_size", option_dtype::OPT_UINT32, &m_page_size, "the tlb_line_size,currently we only support 4096", "4096");
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

tlb_result l1_tlb::access(mem_fetch *mf, unsigned time)
{
    if (mf == nullptr)
    {
        throw std::runtime_error("accessed mf cann't be null");
    }
    else
    {
        /* code */
        //auto result=tlb_result::hit;
        auto addr = mf->get_addr();
        auto v_addr = m_page_manager->get_vir_addr(addr);
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
        printdbg("l1 access mf:%llX\n", mf->get_addr());
        for (; start < end; start++)
        {
            if (!(*start)->is_invalid_line())//previouse bug: cant assume is_valid_line, that would filter reserved line out!!!
            {
                if ((*start)->get_last_access_time() < oldest_access_time)
                {
                    oldest_access_time = (*start)->get_last_access_time();
                    last_line = start;
                }
                if ((*start)->m_tag == tag)
                {

                    auto status = (*start)->get_status(mask);
                    switch (status)
                    {
                    case RESERVED:
                        if (m_mshrs->full(block_addr))
                        {
                            printdbg("l1 hit_reserved and mshr full\n");
                            return tlb_result::resfail;
                        }
                        else
                        {
                            m_mshrs->add<2>(block_addr, mf);//adding to existing entry
                            printdbg("l1 hit_reserved and push to mshr\n");
                            (*start)->set_last_access_time(time, mask);
                            return tlb_result::hit_reserved;
                        }
                        break;
                    case VALID:
#ifdef SJQDEBUG
                        printdbg("push to response queu: mf: %llX\n", mf->get_addr());
#endif                                                   // SJQDEBUG
                        m_response_queue.push_front(mf); //only at this time ,we need push front, and we can pop front now.
                        return tlb_result::hit;

                        break;

                    case MODIFIED:
                        throw std::runtime_error("tlb cache can't be modified, it's read only!");
                    default:
                        throw std::runtime_error("error");
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
        if (m_mshrs->full(block_addr) || (m_config.miss_queue_size > 0 && m_miss_queue.size() >= m_config.miss_queue_size))
        {
            printdbg("miss and mshr fail: mf:%llX\n", mf->get_addr());
            return tlb_result::resfail;
        }
        auto next_line = has_free_line ? free_line : last_line;
        printdbg("miss and allocate, send to miss queue and mshr, mf:%llX\n", mf->get_addr());
        (*next_line)->allocate(tag, block_addr, time, mask);
        m_mshrs->add<1>(block_addr, mf);//adding to new entry
        m_miss_queue.push_back(mf); //miss
        outgoing_mf.insert(mf);
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
        auto size = mf->get_ctrl_size(); //read only need 8 bytes
        if (::icnt_has_buffer(mf->get_tpc(), size))
        {
            printdbg("from miss queue to icnt,mftpc: %u;mf :%llX\n", mf->get_tpc(), mf->get_addr());
            ::icnt_push(mf->get_tpc(), m_config.m_icnt_index, mf, size);
            m_miss_queue.pop_front(); //successfully pushed to icnt
        }
    }
    while (m_mshrs->access_ready())
    { //push all the ready access to response Queue
        m_response_queue.push_back(m_mshrs->next_access());
    }
}
void l1_tlb::fill(mem_fetch *mf, unsigned long long time)
{ //in ldst cycle, will call fill(that's from icnt)
    auto addr = mf->get_addr();
    auto v_addr = m_page_manager->get_vir_addr(addr);
    auto m_page_size_log2 = m_config.m_page_size_log2;
    auto n_set = m_config.n_sets;
    auto n_assoc = m_config.n_associate;

    // get the set_index and tag for searching the tag array.
    auto set_index = (v_addr >> m_page_size_log2) & static_cast<addr_type>(n_set - 1); //first get the page number, then get the cache index.
    auto tag = v_addr & (~static_cast<addr_type>(m_config.m_page_size - 1));           //only need the bits besides page offset
    auto block_addr = v_addr >> m_page_size_log2;
    printdbg("l1 tlb fill, mf:%llX, core id:%u, blockaddr:%llX\n",mf->get_addr(),mf->get_sid(),block_addr);
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
 
    assert(done&& "fill must succeed");
}