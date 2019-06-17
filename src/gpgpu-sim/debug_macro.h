#define printdbg(...)                              \
    do                                             \
    {                                              \
        printf("%s:%d:%s:%llu____",                \
               __FILE__, __LINE__, __func__,       \
               gpu_sim_cycle + gpu_tot_sim_cycle); \
        printf(__VA_ARGS__);                       \
    } while (0)

#ifdef TLBDEBUG
extern unsigned long long gpu_sim_cycle;
extern unsigned long long gpu_tot_sim_cycle;

#define printdbg_tlb(...) \
    printdbg(__VA_ARGS__)
#else
#define printdbg_tlb(...) void(0)
#endif

#ifdef FETCHDEBUG
extern unsigned long long gpu_sim_cycle;
extern unsigned long long gpu_tot_sim_cycle;

#define printdbg_fetch(...) \
    printdbg(__VA_ARGS__)
#else
#define printdbg_fetch(...) void(0)
#endif
//
#ifdef PWDEBUG
extern unsigned long long gpu_sim_cycle;
extern unsigned long long gpu_tot_sim_cycle;

#define printdbg_PW(...) \
    printdbg(__VA_ARGS__)
#else
#define printdbg_PW(...) void(0)
#endif