#ifndef PAGE_MANAGER_HPP
#define PAGE_MANAGER_HPP
using addr_type=unsigned long long;
class page_manager
{
private:
    /* data */
public:

    page_manager(/* args */);
    ~page_manager();
    addr_type get_vir_addr(addr_type origin);
};



#endif // !PAGE_MANAGER_HPP
