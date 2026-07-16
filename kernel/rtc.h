#ifndef RTC_H
#define RTC_H
#include "types.h"

typedef struct {
    uint16_t year;
    uint8_t month, day;
    uint8_t hour, minute, second;
} rtc_datetime_t;

void rtc_read(rtc_datetime_t* out);

#endif
