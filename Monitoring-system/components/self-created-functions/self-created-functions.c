#include "self-created-functions.h"
#include <stdio.h>


int avgCalcu(int *values, int valCo)
{

    int sum = 0.0;
    for (int i = 0; i < valCo; i++)
    {
        sum += values[i];
    }

    /*
    for (int i = 0; i < valCo; i++)
    {
        printf("%d \n",values[i] );
    } */

    return (sum / valCo);
}

char *printArray(int *x, int size, char *arr)
{

    int offset = 0;
    for (int i = 0; i < size; i++)
    {
        offset += sprintf(arr + offset, "%d", x[i]);
        if (i < size - 1)
        {
            offset += sprintf(arr + offset, ", ");
        }
    }

    return arr;
}