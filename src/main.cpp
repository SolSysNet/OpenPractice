#include "openpractice/cli.hpp"

#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace {

std::string narrow(const wchar_t* s) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 1 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
    return out;
}

// Windows hands narrow argv over in the ANSI code page; OpenPractice works in UTF-8.
std::vector<std::string> utf8Arguments() {
    std::vector<std::string> args;
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; wide && i < count; ++i) args.push_back(narrow(wide[i]));
    LocalFree(wide);
    return args;
}

}  // namespace
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    SetConsoleOutputCP(CP_UTF8);
    const std::vector<std::string> args = utf8Arguments();
#else
    const std::vector<std::string> args(argv + 1, argv + argc);
#endif
    return op::runCli(args, std::cout, std::cerr);
}
