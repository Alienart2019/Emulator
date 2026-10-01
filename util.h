// util.h - small string / printing helpers shared by every module
#pragma once

#include <cstdint>
#include <string>
#include <vector>

constexpr int LINE_WIDTH = 127;

// Prints a dashed banner:  ----- / title / -----
void printBanner(const std::string &title);

std::string trim(const std::string &s);
std::string upper(std::string s);

// "0x00ff" style string, `digits` hex digits wide.
std::string hexStr(std::uint64_t value, int digits = 16);

// Splits "X0, [SP, 0x08]" into {"X0", "[SP, 0x08]"}: commas inside [] are ignored.
std::vector<std::string> splitOperands(const std::string &s);

// Parses "#0x10", "0x10", "16", "-5", "#-5". Negative values wrap to 64 bits.
// Returns false if `text` is not a valid immediate.
bool parseImmediate(const std::string &text, std::uint64_t &out);
