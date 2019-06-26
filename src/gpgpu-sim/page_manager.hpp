#ifndef PAGE_MANAGER_HPP
#define PAGE_MANAGER_HPP

#include <bitset>
#include <unordered_map>
#include <set>
#include <iostream>
#include <vector>
using addr_type = unsigned long long;
using addr_map = std::unordered_map<addr_type, addr_type>;
constexpr unsigned long ALLOCATE_MASK = 0xFFFFFFFFFFFFFF00;
class page_manager;
extern page_manager *global_page_manager;
class range
{
public:
    range();
    range(addr_type start, addr_type end);
    bool equel_size(const range &other) const;
    bool smaller_size(const range &other) const;
    bool smaller_start(const range &other) const;
    addr_type get_start() const;
    void set_start(addr_type start);
    addr_type get_end() const;
    void set_end(addr_type end);
    size_t size() const;

private:
    addr_type start;
    addr_type end;
};

enum class page_table_level
{
    L4_ROOT = 0,
    L3 = 1,
    L2 = 2,
    L1_LEAF = 3
};
page_table_level get_next_level(page_table_level this_level);

template <class T>
struct comparator_by_size
{
    constexpr bool operator()(const T &a, const T &b) const
    {
        return (a.equel_size(b)) ? a.smaller_start(b) : a.smaller_size(b);
    }
};

template <class T>
struct comparator_by_start_point
{
    constexpr bool operator()(const T &a, const T &b) const
    {
        return a.smaller_start(b);
    }
};
using range_set_compare_by_size = std::set<range, comparator_by_size<range>>;
using range_set_compare_by_start = std::set<range, comparator_by_start_point<range>>;

//stream start
std::ostream &operator<<(std::ostream &out, const range &m_range);

std::ostream &operator<<(std::ostream &out, const range_set_compare_by_size &freeranges);
//stream end
constexpr addr_type physic_start =      0x0000c0000000;
constexpr addr_type physic_end =        0x0000F0000000;//that's code start
constexpr addr_type pagetable_start =   0x000000c00000;
constexpr addr_type pagetable_end =     0x000000FFFFFF;
constexpr addr_type code_start =        0x0000f0000000; //that is get from test.
constexpr addr_type virtual_start =     0x0000c0000000;
constexpr addr_type virtual_end =       0x0000F0000000;
class page_table;
class page_manager
{

public:
    addr_type translate(addr_type virtual_addr);
    addr_type get_pagetable_physic_addr(addr_type virtual_addr, page_table_level level);
    page_manager(/* args */);
    ~page_manager();
    void *cudaMalloc(size_t size);
    //addr_type get_vir_addr(addr_type origin);
    void allocate_page(addr_type first_page, size_t size);
    addr_type get_valid_physic_page();
    void add_page_table(addr_type virtual_page_number, addr_type physic_page_number);
    page_table *creat_new_page_table(page_table_level level);

private:
    addr_map virtual_to_physic; //all L1 entries
    addr_map physic_to_virtual;
    range_set_compare_by_size virtual_free_ranges; //virtual addr, only use mast , no shift, only responsible for malloc, not for other addr
    range_set_compare_by_start virtual_used_ranges;

    range_set_compare_by_start physic_free_ranges; //only  physic pages
    range_set_compare_by_start physic_used_ranges;

    range_set_compare_by_start page_table_ranges_free; //physic addr
    range_set_compare_by_start page_table_ranges_used;
    //addr_type end_addr;

    addr_type current_pagetable_end_addr; //current last addr
    /* data */
    page_table *root;
};
constexpr addr_type masks[] = {
    0xFF8000000000,
    0x007FC0000000,
    0x00003FE00000,
    0x0000001FF000};

class page_table
{
public:
    page_table(page_table_level level, addr_type physic_addr, page_manager *m_page_manager);
    void add_page_table_entry(addr_type virtual_page_number, addr_type physic_page_number);
    addr_type get_physic_addr(addr_type virtual_addr, page_table_level level);

private:
    unsigned short get_m_index(addr_type virtual_addr);
    page_manager *m_page_manager;           //don't delete
    std::set<unsigned short> valid_entries; //0 ~ 511
    page_table_level m_level;
    std::vector<page_table *> entries;
    addr_type m_mask;
    unsigned num_entries;
    unsigned m_physic_address;
    unsigned short offset;
};

#endif
