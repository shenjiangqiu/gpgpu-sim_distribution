#include "page_manager.hpp"
#include <stdlib.h>
#include <assert.h>
//range

page_manager* global_page_manager;

class global_page_manager_init{
    public:
    global_page_manager_init(){
        global_page_manager=new page_manager();
    }

};
global_page_manager_init m_global_page_manager_init;


range::range() : start(0), end(0) {}
range::range(addr_type start, addr_type end) : start(start), end(end) {}
bool range::equel_size(const range &other) const
{
    return size() == other.size();
}
bool range::smaller_size(const range &other) const
{
    return size() < other.size();
}
bool range::smaller_start(const range &other) const
{
    return start < other.start;
}
addr_type range::get_start() const
{
    return start;
}
void range::set_start(addr_type start)
{
    this->start = start;
}
addr_type range::get_end() const
{
    return end;
}
size_t range::size() const
{
    return end - start;
}
//range end

//stream
std::ostream &operator<<(std::ostream &out, const range &m_range)
{
    out << "" << m_range.get_start() << " - " << m_range.get_end() << "";
    return out;
}
std::ostream &operator<<(std::ostream &out, const range_set_compare_by_size &freeranges)
{
    out << "";
    for (auto range : freeranges)
    {
        out << " | " << range << "";
    }
    out << "";
    return out;
}

//stream end


//page_manager start
page_manager::page_manager(/* args */) : virtual_to_physic(),
                                         physic_to_virtual(),
                                         virtual_free_ranges(),
                                         virtual_used_ranges(),
                                         physic_free_ranges(),
                                         physic_used_ranges(),
                                         page_table_ranges_free(),
                                         page_table_ranges_used(),
                                         current_pagetable_end_addr(pagetable_start),
                                         root(nullptr)
{
    virtual_free_ranges.insert(range(virtual_start, virtual_end));
    physic_free_ranges.insert(range(physic_start,physic_end));
    page_table_ranges_free.insert(range(pagetable_start, pagetable_end));
}

page_manager::~page_manager()
{
    delete root;
}
addr_type page_manager::translate(addr_type virtual_addr){

    auto page_id=virtual_addr & ~(4095ull);
    auto it=virtual_to_physic.find(page_id);
    if(it!=virtual_to_physic.end()){
        return it->second;
    }else{
        //this address is not from malloc. maybe a instruction addr.
        auto physic_page=get_valid_physic_page();
        add_page_table(page_id,physic_page);
        virtual_to_physic[page_id]=physic_page;
        physic_to_virtual[physic_page]=page_id;
    }
}

/* addr_type page_manager::get_vir_addr(addr_type origin)
{
    return origin;
} */


void *page_manager::cudaMalloc(size_t size) //redesigned at 6/3
{
    assert(size != 0);
    if (size % 256 != 0) //align to 256
    {
        size += 256;
        size &= ALLOCATE_MASK;
    }
    auto new_range = range();
    auto it = virtual_free_ranges.upper_bound(range(0, size - 1)); //find the good free page of size=size
    if (it != virtual_free_ranges.end())
    { //good this block deal with free_range
        auto this_range = *it;
        if (this_range.size() == size)
        {
            new_range = this_range;
            virtual_free_ranges.erase(it);

        }
        else
        {
            new_range = range(this_range.get_start(), this_range.get_start() + size);
            virtual_free_ranges.erase(it);
            virtual_free_ranges.insert(new_range);
        }
        
    }
    else //can't find free range
    {
        throw std::runtime_error("run out of virtual memory");
        /* code */
    }
    //start to deal with used addr

    auto ret = virtual_used_ranges.insert(new_range);
    assert(ret.second);
    auto new_it = ret.first;

    addr_type first_page = new_range.get_start() & ~4095ull; // addr/4096
    addr_type last_page = (new_range.get_end() - 1) & ~4095ull ;
    auto prev_range=std::prev(new_it);
    auto after_range=std::next(new_it);
    if(prev_range!=virtual_used_ranges.end()){
        if(prev_range->get_end()>first_page){//if end=4097 first page=4096. OK, if end=4096, it belongs to prev page;
            first_page+=4096;
        }
    }
    if(after_range!=virtual_used_ranges.end()){
        if(after_range->get_start()< last_page+4096){ //if last page=4096 4096~4096+4095's start point is covered
            last_page-=4096;
        }
    }
    if (first_page <= last_page)
    {
        allocate_page(first_page, last_page - first_page + 1); //find out physic pages.
    }



    return (void *)new_range.get_start();
}


void page_manager::allocate_page(addr_type first_page, size_t size)
{
    for (auto i = 0; i < size ; i++)
    {
        auto vpn=first_page+i*4096;
        auto physic_page_number = get_valid_physic_page();
        add_page_table(vpn, physic_page_number);
        virtual_to_physic[vpn]=physic_page_number;
        physic_to_virtual[physic_page_number]=vpn;
    }
}


addr_type page_manager::get_valid_physic_page()
{
    if (physic_free_ranges.empty())
    { //run out of free page
        throw std::runtime_error("no physic page exist");
    }
    auto begin_page_found_itr = physic_free_ranges.begin(); //find the first free page
    auto begin_page_found = *begin_page_found_itr;          //this is copy. so original can be erase.
    if (begin_page_found.size() == 4096)
    { //the free page can be delete with out more opration
        physic_free_ranges.erase(begin_page_found_itr);
    }
    else
    {
        auto replace_page = begin_page_found;
        replace_page.set_start(begin_page_found.get_start() + 4096);
        physic_free_ranges.erase(begin_page_found_itr);
        physic_free_ranges.insert(replace_page);
    }

    //now we set used_range;
    auto result_page = range(begin_page_found);
    result_page.set_end(result_page.get_start() + 4096); //the real page to insert

    auto used_pre = physic_used_ranges.lower_bound(result_page);
    auto used_after = physic_used_ranges.upper_bound(result_page);
    bool extend = false;
    if (used_pre != physic_used_ranges.end())
    { //exist pre page, just extend it without insert
        extend = true;
        auto replace_page = *used_pre;
        replace_page.set_end(result_page.get_end());
        physic_used_ranges.erase(used_pre);
        physic_used_ranges.insert(replace_page);
    }
    if (used_after != physic_used_ranges.end())
    { //exist after page, try to extend if it's continouse
        if (used_after->get_start() == result_page.get_end())
        {
            extend = true;
            auto replace_page = *used_after;
            replace_page.set_start(result_page.get_start());
            physic_used_ranges.erase(used_after);
            physic_used_ranges.insert(replace_page);
        }
    }
    if (!extend)
    { //just insert
        physic_used_ranges.insert(result_page);
    }
    return result_page.get_start();
}

void page_manager::add_page_table(addr_type virtual_page_number, addr_type physic_page_number)
{
    if (!root)
    {
        root = creat_new_page_table(page_table_level::L4_ROOT);
    }
    root->add_page_table_entry(virtual_page_number, physic_page_number);
}

page_table *page_manager::creat_new_page_table(page_table_level level){
    auto physic_addr=get_valid_physic_page();
    return new page_table(level,physic_addr,this);

}
//page_manager end
std::set<unsigned short> all_free_set;//only init once
class build_all_free_set{
    public:
    build_all_free_set(){
        for(unsigned short i=0;i<512;i++){
            all_free_set.insert(i);
        }
    }
};
build_all_free_set m_build;//to init all_free_set;

//page table start
page_table::page_table(page_table_level level, addr_type physic_addr,page_manager* m_page_manager):
    m_page_manager(m_page_manager),
    valid_entries(all_free_set),
    m_level(level),
    entries(512,nullptr),
    m_mask(masks[(unsigned int)level]),
    num_entries(0),
    m_physic_address(physic_addr),
    offset(12+9*(3-(unsigned int)level)) 
{

}
void page_table::add_page_table_entry(addr_type virtual_page_number, addr_type physic_page_number)
{
    unsigned short index = get_m_index(virtual_page_number);
    if (m_level == page_table_level::L1_LEAF)
    {
        if (entries[index] != nullptr)
        {
            throw std::runtime_error("error, leaf entry must be nullptr");
        }
        entries[index] = (page_table *)physic_page_number;
        return;
    }
    else
    {
        if (entries[index] == nullptr)
        {
            entries[index] = m_page_manager->creat_new_page_table(get_next_level(m_level));
            entries[index]->add_page_table_entry(virtual_page_number,physic_page_number);
            return;
        }else
        {
            entries[index]->add_page_table_entry(virtual_page_number,physic_page_number);
        }
        
    }
}

unsigned short page_table::get_m_index(addr_type virtual_addr){
    return (unsigned short ) (virtual_addr&m_mask)>>offset;
}


page_table_level get_next_level(page_table_level this_level){
    if(this_level==page_table_level::L1_LEAF){
        throw std::runtime_error("no next level for L1");
    }else{
        return static_cast<page_table_level>(static_cast<unsigned>(this_level)+1);
    }

}