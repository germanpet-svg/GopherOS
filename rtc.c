// rtc.c - Reloj de tiempo real (CMOS RTC), fecha/hora reales del hardware.
//
// El RTC vive detrás de dos puertos de I/O (0x70 índice, 0x71 dato). Los
// valores pueden venir en BCD o binario, y la hora en formato 12 o 24h,
// según los bits del registro de Status B — hay que leerlo y adaptarse,
// no asumir un formato fijo.

#include "rtc.h"
#include "hal.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    io_wait();
    return inb(CMOS_DATA);
}

static bool rtc_update_in_progress(void) {
    return (cmos_read(0x0A) & 0x80) != 0;
}

static uint8_t bcd_to_bin(uint8_t v) {
    return (uint8_t)((v & 0x0F) + ((v / 16) * 10));
}

// Lee todos los registros relevantes en una sola pasada consistente
// (reintenta si el RTC estaba actualizándose a mitad de lectura).
static void rtc_read_raw(uint8_t* sec, uint8_t* min, uint8_t* hour,
                          uint8_t* day, uint8_t* month, uint8_t* year, uint8_t* century) {
    uint8_t s1, mi1, h1, d1, mo1, y1, c1 = 0;
    uint8_t s2, mi2, h2, d2, mo2, y2, c2 = 0;
    bool has_century = false;

    do {
        while (rtc_update_in_progress());
        s1 = cmos_read(0x00); mi1 = cmos_read(0x02); h1 = cmos_read(0x04);
        d1 = cmos_read(0x07); mo1 = cmos_read(0x08); y1 = cmos_read(0x09);
        c1 = cmos_read(0x32); // registro de siglo (no todos los BIOS lo implementan)

        while (rtc_update_in_progress());
        s2 = cmos_read(0x00); mi2 = cmos_read(0x02); h2 = cmos_read(0x04);
        d2 = cmos_read(0x07); mo2 = cmos_read(0x08); y2 = cmos_read(0x09);
        c2 = cmos_read(0x32);
    } while (s1 != s2 || mi1 != mi2 || h1 != h2 || d1 != d2 || mo1 != mo2 || y1 != y2 || c1 != c2);

    has_century = (c1 != 0 && c1 != 0xFF);

    *sec = s1; *min = mi1; *hour = h1; *day = d1; *month = mo1; *year = y1;
    *century = has_century ? c1 : 0;
}

void rtc_read(rtc_datetime_t* out) {
    uint8_t sec, min, hour, day, month, year, century;
    rtc_read_raw(&sec, &min, &hour, &day, &month, &year, &century);

    uint8_t status_b = cmos_read(0x0B);
    bool is_binary = (status_b & 0x04) != 0;
    bool is_24h    = (status_b & 0x02) != 0;

    if (!is_binary) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        bool pm = hour & 0x80;
        hour = bcd_to_bin(hour & 0x7F);
        if (!is_24h && pm) hour = (uint8_t)((hour + 12) % 24);
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = bcd_to_bin(year);
        if (century) century = bcd_to_bin(century);
    } else if (!is_24h && (hour & 0x80)) {
        hour = (uint8_t)(((hour & 0x7F) + 12) % 24);
    }

    out->second = sec;
    out->minute = min;
    out->hour = hour;
    out->day = day;
    out->month = month;
    out->year = (uint16_t)(century ? (century * 100 + year) : (year < 70 ? 2000 + year : 1900 + year));
}
