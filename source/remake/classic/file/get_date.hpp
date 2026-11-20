#pragma once
// Port of GET_DATE.HPP/.CPP: today's date for the reports, and the date
// prompts and helpers that went with it.
//
// v3.51 kept the last year it had seen in DATE.DAT and asked about a system
// year more than one away from it; here that year is the system's own, so
// the question does not come up.

#include "classic/screen.hpp"
#include "classic/task.hpp"

namespace classic {

class get_date {
    // (v3.51 also declared an int max_m_flag, never used.)
    Task<int> change_date(screen& scr, int& d, int& m, int& y);
    long get_factor(int d, int m, int y);
    int check_date(int& d, int& m, int& y);
    int YEAR;
    Task<void> print_error(const char* s);

public:
    get_date();
    Task<int> get_current_date(int xloc, int yloc, int& d, int& m, int& y, int ScrCol, int display);
    const char* get_month(int m);
    const char* get_day(int d);
    int get_any_day_of_week(int d, int m, int y);
    long days_between_dates(int d1, int m1, int y1, int d2, int m2, int y2);
    int get_max_m(int m, int y);
    Task<int> get_new_date(int xloc, int yloc, const char* prompt, int& d, int& m, int& y, screen& scr, int flag,
                           int PromptCol, int StrCol);
    void date_to_sstring(char* s, int d, int m, int y);  // s: 11 characters
    void date_to_lstring(char* s, int d, int m, int y);  // s: 80 characters
    void sstring_to_date(const char* s, int& d, int& m, int& y);
    Task<void> check_day_month(int& d, int& m, int& y);
};

}  // namespace classic
