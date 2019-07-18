#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H
#include "debug_macro.h"
#include <bitset>
#include <map>
#include <set>
#include <iostream>
#include <vector>
#include<stdlib.h>
#include<assert.h>
using addr_type = unsigned long long;
using addr_map = std::map<addr_type, addr_type>;
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
template <class T>
struct comparator_by_start_point_pair
{
    constexpr bool operator()(const T &a, const T &b) const
    {
        return std::get<0>(a) < std::get<0>(b);
    }
};
using range_set_compare_by_size = std::set<range, comparator_by_size<range>>;
//a set contain ranges which sorted by start address
using range_set_compare_by_start = std::set<range, comparator_by_start_point<range>>;
using page_table_range_buffer_entry = std::tuple<addr_type, addr_type, unsigned>;
//a set contain pair<ranges,length> which sorted by start address of ranges
using pt_range_buffer_set = std::set<page_table_range_buffer_entry, comparator_by_start_point_pair<page_table_range_buffer_entry>>;

//stream start
std::ostream &operator<<(std::ostream &out, const range &m_range);

std::ostream &operator<<(std::ostream &out, const range_set_compare_by_size &freeranges);
//stream end

//because of gpgpusim bug(I didn't findout it,some times addr is larger than 32-bit number will cause error because of truncate to 32-bit!)
constexpr addr_type physic_start = 0x0000c0000000;
constexpr addr_type physic_end = 0x0000F0000000;      //that's code start
constexpr addr_type pagetable_start = 0x000000c00000; //
constexpr addr_type pagetable_end = 0x000000FFFFFF;   //
constexpr addr_type code_start = 0x0000f0000000;      //that is get from test.
constexpr addr_type virtual_start = 0x0000c0000000;
constexpr addr_type virtual_end = 0x0000F0000000;
class page_table;

#define printdbg_mset_pt(m_pt_set) \
            if (m_pt_set.empty())\
            {\
                printdbg_PTRNG("empty!\n\n");\
            }\
            else\
            {\
                for (auto entry : m_pt_set)\
                {\
                    printdbg_PTRNG("start_addr:%llx,physic_addr:%llx,size:%u,start_no:%llx;physic_no:%llx\n",\
                                   std::get<0>(entry),\
                                   std::get<1>(entry),\
                                   std::get<2>(entry),\
                                   std::get<0>(entry) >> 21,\
                                   std::get<1>(entry) >> 12);\
                }\
                printdbg_PTRNG("\n\n");\
            }\

class range_page_table
{
public:
    //every time we add a new pagetable we need to call this function,option:0:add,1 del
    void update(addr_type virtual_page_addr, addr_type pt_physic_addr, unsigned option = 0)
    {
        printdbg_PTRNG("start to update: virtual_page_addr:%llx, physic_page_addr:%llx,v_no:%llx,p_no:%llx\n",
                       /* vaddr */ virtual_page_addr,
                       /* pt_paddr */ pt_physic_addr,
                       /* l2 number */ virtual_page_addr >> 21,
                       /* pt_physic_number */ pt_physic_addr >> 12);
        printdbg_PTRNG("before update:\n");
        printdbg_mset_pt(m_pt_set);
        auto range_to_insert = std::make_tuple(virtual_page_addr, pt_physic_addr, 1);
        if (m_pt_set.empty())
        {
            m_pt_set.insert(range_to_insert);
            printdbg_PTRNG("after update:\n");
            printdbg_mset_pt(m_pt_set);
            return;
        }
        //find the first 
        auto range_pre=find_the_less_or_equal_range(range_to_insert);

        if(range_pre==m_pt_set.end()){
            //it's the smallest entry now;

            insert_and_merge(range_to_insert);
            //code here

            printdbg_PTRNG("after update:\n");
            printdbg_mset_pt(m_pt_set);

            return;
        }

        //here we find some small entry;
        switch (the_position_of_the_entry(range_pre,range_to_insert))
        {
        case 0:
            /* code */
            append_existing_entry_and_merge(range_pre);
            break;
        case 1:
            throw "that shouldn't occur!!";

            break;
        case 2:
            insert_and_merge(range_to_insert);
            break;
        case 3:
            insert_and_merge(range_to_insert);
            break;
        default:
            throw "can't reach there";
            break;
        }
        printdbg_PTRNG("after update:\n");
        printdbg_mset_pt(m_pt_set);

        //old code
    }

private:
    pt_range_buffer_set m_pt_set;
    void append_existing_entry_and_merge(decltype(m_pt_set.begin()) to_append){
        auto upper=m_pt_set.upper_bound(*to_append);
        if(upper==m_pt_set.end()) {
            auto replace_entry=*to_append;
            std::get<2>(replace_entry)++;
            m_pt_set.erase(to_append);
            m_pt_set.insert(replace_entry);
            return;
        }

        auto page_gap=get_page_gap(*to_append,*upper);
        if(page_gap==std::get<2>(*to_append)+1){
            auto replace_entry=*to_append;
            std::get<2>(replace_entry)+=(std::get<2>(*upper)+1);
            m_pt_set.erase(to_append);
            m_pt_set.insert(replace_entry);
            return;
        }

    }
    void insert_and_merge(const page_table_range_buffer_entry& to_insert){
        assert(std::get<2>(to_insert)==1);
        
        auto upper=m_pt_set.upper_bound(to_insert);//find bug here
        if(upper==m_pt_set.end()) {
            m_pt_set.insert(to_insert);
            return;
        }

        auto page_gap=get_page_gap(to_insert,*upper);
        if(page_gap==std::get<2>(to_insert)){//==1
            auto replace_entry=to_insert;
            std::get<2>(replace_entry)+=std::get<2>(*upper);
            //std::get<2>(replace_entry)+=(std::get<2>(*upper)+1);
            m_pt_set.erase(upper);
            m_pt_set.insert(replace_entry);
            return;
        }
        m_pt_set.insert(to_insert);

    }
    decltype(m_pt_set.begin()) find_the_less_or_equal_range(const page_table_range_buffer_entry & to_compare){
        if(m_pt_set.empty()) return m_pt_set.end();
        auto ret=m_pt_set.upper_bound(to_compare);
        if(ret!=m_pt_set.begin()){
            return std::prev(ret);
        }else{
            return m_pt_set.end();
        }
        
    }
    unsigned get_page_gap(const page_table_range_buffer_entry& from,const page_table_range_buffer_entry& to){
        return (std::get<0>(to)-std::get<0>(from))>>21;
    }

    unsigned get_pt_addr_gap(const page_table_range_buffer_entry& from,const page_table_range_buffer_entry& to){
        return (std::get<1>(to)-std::get<1>(from))>>12;
    }
    //return 0 mean need to append and connect, return 1 means inside and should throw, return 2 means we need a new entry and connect,3 means it's not belong to that range!;
    unsigned the_position_of_the_entry(decltype(m_pt_set.begin()) existing_entry,const page_table_range_buffer_entry& to_insert){
        auto page_num_gap=get_page_gap(*existing_entry,to_insert);
        auto pt_addr_gap=get_pt_addr_gap(*existing_entry,to_insert);
        //find a bug!
        if(page_num_gap!=pt_addr_gap){
            //that means is't not blong to one range!
            return 3;
        }

        assert(page_num_gap==pt_addr_gap);
        int size_gp=page_num_gap-std::get<2>(*existing_entry);
        if(size_gp==0){
            return 0;
        }else if(size_gp<0){
            return 1;
        }else{
            return 2;
        }
    }
};

class page_manager
{

public:
    addr_type translate(addr_type virtual_addr);
    addr_type get_pagetable_physic_addr(addr_type virtual_addr, page_table_level level);
    page_manager(/* args */);
    ~page_manager();
    void *cudaMalloc(size_t size);
    void update_range_pt(addr_type virtual_addr,addr_type pt_physic_addr){
        m_range_page_table.update(virtual_addr,pt_physic_addr);
    }
    //addr_type get_vir_addr(addr_type origin);
    void allocate_page(addr_type first_page, size_t size);

    //return one free physic page address in global space.
    addr_type get_valid_physic_page();

    //return one free physic page address in page-table space;
    addr_type get_valid_physic_page_page_table()
    {

        if (page_table_ranges_free.empty())
        { //run out of free page
            throw std::runtime_error("no physic page exist");
        }
        auto begin_page_found_itr = page_table_ranges_free.begin(); //find the first free page
        auto begin_page_found = *begin_page_found_itr;              //this is copy. so original can be erase.
        if (begin_page_found.size() == 4096)
        { //the free page can be delete with out more opration
            page_table_ranges_free.erase(begin_page_found_itr);
        }
        else
        {
            auto replace_page = begin_page_found;
            replace_page.set_start(begin_page_found.get_start() + 4096);
            page_table_ranges_free.erase(begin_page_found_itr);
            page_table_ranges_free.insert(replace_page);
        }

        //now we set used_range;
        auto result_page = range(begin_page_found);
        result_page.set_end(result_page.get_start() + 4096); //the real page to insert

        auto used_pre = page_table_ranges_used.lower_bound(result_page);
        auto used_after = page_table_ranges_used.upper_bound(result_page);
        bool extend = false;
        if (used_pre != page_table_ranges_used.end())
        { //exist pre page, just extend it without insert
            extend = true;
            auto replace_page = *used_pre;
            replace_page.set_end(result_page.get_end());
            page_table_ranges_used.erase(used_pre);
            page_table_ranges_used.insert(replace_page);
        }
        if (used_after != page_table_ranges_used.end())
        { //exist after page, try to extend if it's continouse
            if (used_after->get_start() == result_page.get_end())
            {
                extend = true;
                auto replace_page = *used_after;
                replace_page.set_start(result_page.get_start());
                page_table_ranges_used.erase(used_after);
                page_table_ranges_used.insert(replace_page);
            }
        }
        if (!extend)
        { //just insert
            page_table_ranges_used.insert(result_page);
        }
        return result_page.get_start();
    }
    //end function

    void add_page_table(addr_type virtual_page_number, addr_type physic_page_number);
    page_table *creat_new_page_table(page_table_level level);
    //void update_pt_range_buffer();

private:
    range_page_table m_range_page_table;
    unsigned pt_range_buffer_size = 10;

    addr_map virtual_to_physic; //all L1 entries
    addr_map physic_to_virtual;
    range_set_compare_by_size virtual_free_ranges; //virtual addr, only use mast , no shift, only responsible for malloc, not for other addr
    range_set_compare_by_start virtual_used_ranges;

    // range_set_compare_by_start physic_pt_free_ranges;
    // range_set_compare_by_start physic_pt_used_ranges;

    range_set_compare_by_start physic_free_ranges; //only  physic pages
    range_set_compare_by_start physic_used_ranges;

    range_set_compare_by_start page_table_ranges_free; //physic addr
    range_set_compare_by_start page_table_ranges_used;
    //addr_type end_addr;

    addr_type current_pagetable_end_addr; //current last addr
    /* data */
    page_table *root; //that can be used to access all the page table,it cantains 512 entries and each entry is a pointer to next level pagetable
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
