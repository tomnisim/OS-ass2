#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"


int main(int argc, int** argv)
{
    int cpu_number_for_tom = get_cpu();
    printf("%d\n", cpu_number_for_tom);
    printf("working");


    exit(0);
}
