
#include <stdio.h>

    int CheckCPU()
    {
        union
        {
            int a;
            char b;
        }c;
        c.a = 1;
        return (c.b == 1);
    }

    int main()
    {    
        if (CheckCPU())
        {
            printf("Little_endian\n");
        }
        else
        {
            printf("Big_endian\n");
        }

        return 0;
    }
