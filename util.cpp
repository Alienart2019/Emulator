#include "util.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>

using namespace std;

void printBanner(const string &title) {
    string bar(LINE_WIDTH, '-');
    printf("%s\n%s\n%s\n", bar.c_str(), title.c_str(), bar.c_str());
}

string trim(const string &s) {
    size_t b = 0, e = s.size();
    while (b < e && isspace((unsigned char)s[b])) b++;
    while (e > b && isspace((unsigned char)s[e - 1])) e--;
    return s.substr(b, e - b);
}

string upper(string s) {
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)toupper(c); });
    return s;
}

string hexStr(uint64_t value, int digits) {
    char buf[40];
    snprintf(buf, sizeof buf, "0x%0*llx", digits, (unsigned long long)value);
    return buf;
}

vector<string> splitOperands(const string &s) {
    vector<string> out;
    string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '[') depth++;
        if (c == ']') depth--;
        if (c == ',' && depth == 0) {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

bool parseImmediate(const string &text, uint64_t &out) {
    string s = trim(text);
    if (!s.empty() && s[0] == '#') s = trim(s.substr(1));

    bool negative = false;
    if (!s.empty() && (s[0] == '-' || s[0] == '+')) {
        negative = (s[0] == '-');
        s = s.substr(1);
    }
    if (s.empty()) return false;

    int base = 10;
    size_t start = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        start = 2;
    }
    for (size_t i = start; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(base == 16 ? isxdigit(c) : isdigit(c))) return false;
    }

    errno = 0;
    char *end = nullptr;
    unsigned long long v = strtoull(s.c_str() + start, &end, base);
    if (errno == ERANGE || *end != '\0') return false;

    out = negative ? (uint64_t)(0ULL - v) : (uint64_t)v;
    return true;
}
