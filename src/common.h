#ifndef COMMON_H
#define COMMON_H

#include <cassert>

#define STR(p) ((p) ? (p): "-")
#define assertf(cond, fmt, ...) { if (!(cond)) {printf("\e[34m%s.%d:\e[31mASSERT FAILED:\e[0m " fmt " \n", __PRETTY_FUNCTION__, __LINE__, ##__VA_ARGS__); exit(1);}}
#define P(fmt, ...) printf(fmt, ##__VA_ARGS__)
// colorings
#define PC(fmt, ...) printf(fmt, ##__VA_ARGS__)
#define PF(fmt, ...) printf("\e[34m%s.%d:\e[0m " fmt "\n", __PRETTY_FUNCTION__, __LINE__, ##__VA_ARGS__)
#define PT(fmt, ...) printf("\e[36m%s.%d:\e[0m " fmt "\n", __PRETTY_FUNCTION__, __LINE__, ##__VA_ARGS__)

#endif
