#pragma once
#include <algorithm>

// The calendar (docs/STAGES.md): dates for the day clock's days, and the regional cup's rally weekends.
// No engine dependencies.
namespace Rally::Calendar
{
    // Day 1 is Saturday 6 April 1996, the cup's first rally weekend.
    constexpr int StartYear = 1996;
    constexpr int StartDayOfYear = 97;
    inline bool IsLeap(int Year) { return (Year % 4 == 0 && Year % 100 != 0) || Year % 400 == 0; }
    struct FDate
    {
        int Year;
        int Month;   // 1..12
        int Date;    // 1..31
        int Weekday; // 0 Monday .. 6 Sunday
    };
    inline FDate DateOf(int Day)
    {
        Day = std::max(Day, 1);
        int Year = StartYear, Of = StartDayOfYear + Day - 1;
        while (Of > (IsLeap(Year) ? 366 : 365)) { Of -= IsLeap(Year) ? 366 : 365; ++Year; }
        static constexpr int Lengths[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        int Month = 0;
        for (;; ++Month)
        {
            const int Length = Lengths[Month] + (Month == 1 && IsLeap(Year) ? 1 : 0);
            if (Of <= Length) break;
            Of -= Length;
        }
        return { Year, Month + 1, Of, (Day - 1 + 5) % 7 };
    }
    inline const char* WeekdayName(int Weekday)
    {
        static const char* Names[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
        return Names[((Weekday % 7) + 7) % 7];
    }
    inline const char* MonthName(int Month)
    {
        static const char* Names[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        return Names[std::clamp(Month, 1, 12) - 1];
    }
    // A rally weekend every second weekend: the Saturday and Sunday of weeks 1, 3, 5 ... The weekdays
    // between are for work, money and the car.
    constexpr int WeekendEvery = 14;
    inline bool IsRallyDay(int Day) { return Day >= 1 && (Day - 1) % WeekendEvery < 2; }
    // How many rally weekends are over by this day (their Sunday has passed).
    inline int WeekendsOver(int Day) { return Day >= 3 ? (Day - 3) / WeekendEvery + 1 : 0; }
    // The first day of the next rally weekend (today if it is one).
    inline int NextRallyDay(int Day)
    {
        if (IsRallyDay(Day)) return Day;
        return ((std::max(Day, 1) - 1) / WeekendEvery + 1) * WeekendEvery + 1;
    }
    // Sleeping from this day and hour wakes on this day: the next morning, or, sleeping till the rally
    // weekend, the next rally day's morning (this morning's, if it is still before waking time).
    inline int WakeDay(int Day, double Hour, double WakeHour, bool bTillRally)
    {
        const int Morning = Hour < WakeHour ? Day : Day + 1;
        return bTillRally ? NextRallyDay(Morning) : Morning;
    }
    // The regional cup: twelve events, April to early September.
    constexpr int CupEvents = 12;
}
