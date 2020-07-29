#ifndef __QSORT_H__
#define __QSORT_H__

#include "types.h"

void qsort(void *array, int nitems, int size,
           __regargs int (*cmp)(const void *, const void *));

#endif
