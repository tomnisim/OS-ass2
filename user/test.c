#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"


int main(int argc, int** argv)
{
    // int cpu_number_for_tom = get_cpu();
    // printf("%d\n", cpu_number_for_tom);
    // printf("working");

    // for (int i=0;i<= 1;i++)
    
    int num = cpu_process_count(1);
    printf("%d\n",num);
    // fork();
    


    exit(0);
}
