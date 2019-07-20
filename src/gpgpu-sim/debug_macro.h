
#ifndef DEBUG_MACRO_H
#define DEBUG_MACRO_H



#define TLBDEBUG
#define FETCHDEBUG
#define PWDEBUG
#define TLBICNTDEBUG
#define COREQUEUEDEBUG
#define NEIDEBUG
#define PTRNGDEBUG


#undef TLBDEBUG
#undef FETCHDEBUG
#undef PWDEBUG
#undef TLBICNTDEBUG
#undef COREQUEUEDEBUG
#undef NEIDEBUG
// #undef PTRNGDEBUG


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


#ifdef TLBICNTDEBUG
extern unsigned long long gpu_sim_cycle;
extern unsigned long long gpu_tot_sim_cycle;

#define printdbg_ICNT(...) \
    printdbg(__VA_ARGS__)
#else
#define printdbg_ICNT(...) void(0)
#endif

#ifdef COREQUEUEDEBUG
extern unsigned long long gpu_sim_cycle;
extern unsigned long long gpu_tot_sim_cycle;
#define printdbg_COREQ(...) \
   printdbg(__VA_ARGS__)
#else
#define printdbg_COREQ(...) void(0)
#endif

#ifdef NEIDEBUG
extern unsigned long long gpu_sim_cycle;
extern unsigned long long gpu_tot_sim_cycle;
#define printdbg_NEI(...) \
   printdbg(__VA_ARGS__)
#else
#define printdbg_NEI(...) void(0)
#endif



#ifdef PTRNGDEBUG
extern unsigned long long gpu_sim_cycle;
extern unsigned long long gpu_tot_sim_cycle;
#define printdbg_PTRNG(...) \
   printdbg(__VA_ARGS__)
#else
#define printdbg_PTRNG(...) void(0)
#endif


#endif//