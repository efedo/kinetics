// Port of GET_DATE.CPP. Comments marked "v3.51" note behaviour kept on
// purpose although it looks odd.

#include "classic/file/get_date.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace classic {
namespace {

std::tm local_now() {
    const std::time_t timer = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &timer);
#else
    localtime_r(&timer, &tm);
#endif
    return tm;
}

}  // namespace

get_date::get_date() {
    // v3.51 read the last year it had seen from DATE.DAT (and wrote it back
    // when done). The system's year is used instead.
    YEAR = local_now().tm_year + 1900;
}

int get_date::get_max_m(int m, int y) {
    if (m == 1 || m == 3 || m == 5 || m == 7 || m == 8 || m == 10 || m == 12) return 31;
    if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
    if (m == 2) {
        if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) return 29;
        return 28;
    }
    return 0;
}

Task<int> get_date::get_current_date(int xloc, int yloc, int& d, int& m, int& y, int Scrcol, int display) {
    char buf[10];
    int i1, i2, flag;
    double Year;

    int right = xloc + 63;
    if (right > 80) {
        right = 80;
        xloc = 27;  // v3.51: 27, so the box is too narrow for its text
    }
    int bottom = yloc + 3;
    if (bottom > 25) {
        bottom = 25;
        yloc = 22;
    }
    screen scr(xloc, yloc, right, bottom, Scrcol, SINGLE, YELLOW);

    const std::tm tblock = local_now();
    y = tblock.tm_year + 1900;
    m = tblock.tm_mon + 1;
    d = tblock.tm_mday;
    if (display) {
        i1 = scr.put_str(1, 1, "Current date: ", WHITE);
        i2 = scr.put_int(i1, 1, "%04i-", y, WHITE);
        i1 = scr.put_int(i2, 1, "%02i-", m, WHITE);
        scr.put_int(i1, 1, "%02i", d, WHITE);
        scr.put_str(1, 2, "Press d to change date, ESC to quit, any other key to continue", WHITE);
        scr.print_screen();
        const int a = std::tolower(co_await getch());
        i1 = 0;
        if (a != ESC) {
            i1 = 1;
            if (a == 'd') i1 = co_await change_date(scr, d, m, y);
        }
        scr.restore_previous_screen();
        co_return i1;
    }
    if (y < (YEAR - 1) || y > (YEAR + 1)) {
        // v3.51: this box is not taken off the screen afterwards.
        screen es(10, 5, 70, 11, RED, DOUBLE, YELLOW);
        es.put_str(2, 2, "ERROR condition exists in GET_DATE !!", WHITE);
        i1 = es.put_str(2, 3, "Suspecious YEAR value from system: ", WHITE);
        es.put_int(i1, 3, "%d", YEAR, YELLOW);
        es.put_str(2, 4, "Press 'e' to EXIT, 'y' to change system YEAR value", YELLOW);
        es.print_screen();
        int a = ' ';
        while (a != 'e' && a != 'y') a = std::tolower(co_await getch());
        if (a == 'e') co_return 0;
        flag = 1;
        std::snprintf(buf, sizeof buf, "%d", YEAR);
        Year = std::atof(buf);
        es.delete_line(1, 58, 4);
        co_await es.get_double(2, 4, "Enter new YEAR value: ", Year, 4, flag, YELLOW, WHITE);
        YEAR = static_cast<int>(Year);  // v3.51 also wrote it to DATE.DAT
        co_return 1;
    }
    if (y != YEAR) YEAR = y;  // v3.51 also wrote it to DATE.DAT
    co_return 1;
}

Task<int> get_date::change_date(screen& scr, int& d, int& m, int& y) {
    char s[11] = "", sday[3] = "";
    int flag;

    scr.blank_screen();
    for (;;) {
        flag = 0;
        const int c = co_await scr.get_string(1, 1, "Enter date in format (YYYY-MM-DD): ", s, 10, flag, YELLOW, WHITE);
        if (c == ESC) co_return 0;
        if (s[0] == 'd') {
            // v3.51: sd[1] = sd[2] (for s[2]), so only one digit is read.
            sday[0] = s[1];
            sday[1] = sday[2];
            sday[2] = '\0';
            d = std::atoi(sday);
        } else {
            sstring_to_date(s, d, m, y);
        }
        if (!check_date(d, m, y)) scr.put_str(1, 2, "Re-enter date in correct format!!", YELLOW);
        else break;
    }
    co_return 1;
}

void get_date::sstring_to_date(const char* str, int& d, int& m, int& y) {
    // v3.51 read the fixed positions of "YYYY-MM-DD" whatever the length.
    char s[11] = {};
    std::strncpy(s, str, 10);
    char s1[5];

    if (s[0] == 'd') {
        s1[0] = s[1];
        s1[1] = s[2];
        s1[2] = '\0';
        d = std::atoi(s1);
    } else {
        std::strncpy(s1, s, 4);
        s1[4] = '\0';
        y = std::atoi(s1);
        s1[0] = s[5];
        s1[1] = s[6];
        s1[2] = '\0';
        m = std::atoi(s1);
        s1[0] = s[8];
        s1[1] = s[9];
        d = std::atoi(s1);
    }
}

int get_date::check_date(int& d, int& m, int& y) {
    int a = YES;
    if (y <= 1900 || y >= 4000) a = NO;
    if (m < 1 || m > 12) {
        m = 0;
        a = NO;
    }
    if ((d < 1) || (d > get_max_m(m, y))) {
        d = 0;
        a = NO;
    }
    return a;
}

const char* get_date::get_month(int m) {
    static const char* const months[] = {"January", "February", "March",     "April",   "May",      "June",
                                         "July",    "August",   "September", "October", "November", "December"};
    if (m >= 1 && m <= 12) return months[m - 1];
    return "Month not defined !!";
}

const char* get_date::get_day(int d) {
    static const char* const days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    if (d >= 0 && d <= 6) return days[d];
    return "Day not defined !!";
}

int get_date::get_any_day_of_week(int d, int m, int y) {
    // 0=SUN, 1=MON, 2=TUE, 3=WED, 4=THUR, 5=FRI, 6=SAT
    const long factor = get_factor(d, m, y);
    const long a = factor - 7L * (factor / 7L);
    if (a == 0) return 6;
    return static_cast<int>(a - 1);
}

long get_date::days_between_dates(int d1, int m1, int y1, int d2, int m2, int y2) {
    const long factor1 = get_factor(d1, m1, y1);
    const long factor2 = get_factor(d2, m2, y2);
    return factor2 - factor1;
}

long get_date::get_factor(int d, int m, int y) {
    long factor;
    if (m == 1 || m == 2)
        factor = 365L * y + d + 31 * (m - 1) + (y - 1) / 4 - 3 * ((y - 1) / 100 + 1) / 4;
    else
        factor = 365L * y + d + 31 * (m - 1) - static_cast<int>(0.4f * static_cast<float>(m) + 2.3) + y / 4 -
                 3 * (y / 100 + 1) / 4;
    return factor;
}

Task<int> get_date::get_new_date(int xloc, int yloc, const char* prompt, int& d, int& m, int& y, screen& scr,
                                 int flag, int PromptCol, int StrCol) {
    char s[11] = "";
    int key, a;

    if (flag) date_to_sstring(s, d, m, y);
    for (;;) {
        key = co_await scr.get_string(xloc, yloc, prompt, s, 10, flag, PromptCol, StrCol);
        sstring_to_date(s, d, m, y);
        if (check_date(d, m, y) || key == ESC || key == DELETE_KEY || keystroke::is_alt(key)) break;
    }
    if (check_date(d, m, y)) {
        date_to_sstring(s, d, m, y);
        a = scr.put_str(xloc, yloc, prompt, PromptCol);
        scr.put_str(a, yloc, s, StrCol);
    } else if (key == DELETE_KEY) {
        d = m = y = 0;
        a = scr.put_str(xloc, yloc, prompt, PromptCol);
        scr.put_str(a, yloc, "", StrCol);
    }
    scr.print_screen();
    co_return key;
}

void get_date::date_to_sstring(char* s, int d, int m, int y) { std::snprintf(s, 11, "%4d-%02d-%02d", y, m, d); }

void get_date::date_to_lstring(char* s, int d, int m, int y) {
    const int z = get_any_day_of_week(d, m, y);
    std::snprintf(s, 80, "%s, %s %i, %i", get_day(z), get_month(m), d, y);
}

Task<void> get_date::check_day_month(int& d, int& m, int& y) {
    if (y <= 0) co_await print_error("NO year entered in 'check_day_month'");
    while (m < 1) {
        m += 12;
        y--;
    }
    while (m > 12) {
        m -= 12;
        y++;
    }
    for (;;) {
        if (d > get_max_m(m, y)) {
            d -= get_max_m(m, y);
            m++;
            if (m > 12) {
                m = 1;
                y++;
            }
        } else if (d < 1) {
            if (m == 1) {
                m = 12;
                y--;
            } else {
                m--;
            }
            d += get_max_m(m, y);
        } else {
            break;
        }
    }
}

Task<void> get_date::print_error(const char* s) {
    screen scr(10, 5, 70, 11, RED, DOUBLE, YELLOW);
    scr.put_str(2, 2, "ERROR in a GET_DATE class program", WHITE);
    scr.put_str(2, 3, s, WHITE);
    scr.put_str(2, 4, "Press any key to exit program", WHITE);
    scr.print_screen();
    co_await getch();
    g_fatal_error = s;  // v3.51: exit(1); the host shows this and starts again
}

}  // namespace classic
