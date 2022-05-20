#ifndef UTILS_H
#define UTILS_H 1

#include <stdint.h>
#include <stdbool.h>

#include <ifaddrs.h>


#define COUNTOF(array) (sizeof(array) / sizeof(array[0]))


char *get_ieee_mac_addr(const char *mac);

char *to_ieee_mac_addr(char *mac);

char *get_iso8601_time();

uint8_t netmask_prefix(struct sockaddr *netmask);

uint64_t get_age(char *age_str);

#endif /* utils.h */
