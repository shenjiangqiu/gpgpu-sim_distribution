#include "tlb_icnt.h"
#include <iostream>
using namespace std; // namespace  std

int test_main()
{
    mem_fetch *mf1[100];
    for (long long  i = 0; i < 100; i++)
    {
        mf1[i] = (mem_fetch*)static_cast<long long >(i);
    }
    for (int i = 0; i < 28; i++)
    {
        for (int j = 0; j < 30; j++)
        {
            if (global_tlb_icnt->free(0, i))
            {
                global_tlb_icnt->send(0, i, mf1[j], 0);
            }
            else
            {
                cout << "test error,send queue not full" << endl;
            }
        }
        if (global_tlb_icnt->free(0, i))
        {
            cout << "test error,send queue is full" << endl;
        }
        for (int j = 0; j <= 5; j++)
            if (global_tlb_icnt->ready(i, j))
            {
                cout << "test error,send queue is not ready" << endl;
            }
        if (!global_tlb_icnt->ready(i, 6))
        {
            cout << "test error, send queue is ready" << endl;
        }
        for (int j = 0; j < 30; j++)
        {
            if (global_tlb_icnt->ready(i, 6))
            {
                auto mf = global_tlb_icnt->recv(i);
                if (mf != mf1[j])
                {
                    cout << "test error, not match" << endl;
                }
            }
            else
            {
                cout << "should be ready" << endl;
            }
        }
        if (global_tlb_icnt->ready(i, 6))
        {
            cout << "error, shouldn't be ready" << endl;
        }
        try
        {
            global_tlb_icnt->recv(i);
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
            cout << "good! should throw" << endl;
        }
        cout << "finished test " << i << "\n\n\n\n"
             << endl;
    }
    return 0;
}