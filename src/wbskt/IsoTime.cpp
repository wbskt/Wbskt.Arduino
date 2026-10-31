#include "IsoTime.h"

#include <ctype.h>
#include <stdio.h>

namespace wbskt_core {

namespace {

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's days_from_civil).
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int>(static_cast<int64_t>(yoe) + era * 400 + (m <= 2));
}

bool digits(const char*& p, int count, int& out) {
    out = 0;
    for (int i = 0; i < count; ++i, ++p) {
        if (!isdigit(static_cast<unsigned char>(*p))) {
            return false;
        }
        out = out * 10 + (*p - '0');
    }
    return true;
}

bool expect(const char*& p, char c) {
    if (*p != c) {
        return false;
    }
    ++p;
    return true;
}

}  // namespace

int64_t parseIsoTime(const char* text) {
    if (text == nullptr) {
        return -1;
    }
    const char* p = text;
    int year, month, day, hour, minute, second;
    if (!digits(p, 4, year) || !expect(p, '-') || !digits(p, 2, month) || !expect(p, '-') ||
        !digits(p, 2, day) || (*p != 'T' && *p != 't' && *p != ' ')) {
        return -1;
    }
    ++p;
    if (!digits(p, 2, hour) || !expect(p, ':') || !digits(p, 2, minute) || !expect(p, ':') ||
        !digits(p, 2, second)) {
        return -1;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) {
        return -1;
    }
    if (*p == '.') {
        ++p;
        while (isdigit(static_cast<unsigned char>(*p))) {
            ++p;
        }
    }

    int64_t offset = 0;
    if (*p == 'Z' || *p == 'z') {
        ++p;
    } else if (*p == '+' || *p == '-') {
        const int sign = *p == '-' ? -1 : 1;
        ++p;
        int oh, om;
        if (!digits(p, 2, oh) || !expect(p, ':') || !digits(p, 2, om)) {
            return -1;
        }
        offset = sign * (oh * 3600 + om * 60);
    }
    if (*p != '\0') {
        return -1;
    }

    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 + hour * 3600 +
           minute * 60 + second - offset;
}

std::string formatIsoTime(int64_t epochSeconds, uint16_t millis) {
    int64_t days = epochSeconds / 86400;
    int64_t rest = epochSeconds % 86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    int year;
    unsigned month, day;
    civilFromDays(days, year, month, day);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%04d-%02u-%02uT%02d:%02d:%02d.%03uZ", year, month, day,
             static_cast<int>(rest / 3600), static_cast<int>(rest % 3600 / 60), static_cast<int>(rest % 60),
             static_cast<unsigned>(millis % 1000));
    return buffer;
}

}  // namespace wbskt_core
