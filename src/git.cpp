#include "atlast.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace atlast {
namespace {

#ifndef _WIN32
std::string shell_quote(std::string_view value) {
    std::string quoted{"'"};
    for (const char character : value) {
        quoted += character == '\'' ? "'\\''" : std::string(1, character);
    }
    quoted += '\'';
    return quoted;
}

std::string literal_regex(std::string_view value) {
    constexpr std::string_view special = R"(\.^$|()[]{}*+?)";
    std::string escaped;
    for (const char character : value) {
        if (special.contains(character)) {
            escaped += '\\';
        }
        escaped += character;
    }
    return escaped;
}
#endif

}  // namespace

int search_git_history(const fs::path& repository, std::string_view query,
                       int limit) {
#ifdef _WIN32
    static_cast<void>(repository);
    static_cast<void>(query);
    static_cast<void>(limit);
    std::cerr << "Git history search currently requires Linux, macOS, or WSL.\n";
    return 1;
#else
    std::error_code error;
    if (!fs::is_directory(repository, error) || error) {
        std::cerr << "Not a readable repository: " << repository << '\n';
        return 1;
    }

    const std::string format =
        "commit %H%nDate: %aI%nAuthor: %an <%ae>%nSubject: %s%nFiles:";
    const std::string command =
        "git -C " + shell_quote(repository.string()) +
        " log --all --date=iso-strict --format=" + shell_quote(format) +
        " --name-only -G " + shell_quote(literal_regex(query)) + " -n " +
        std::to_string(limit) + " --";

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "Could not start Git.\n";
        return 1;
    }

    std::array<char, 4096> buffer{};
    bool found = false;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        found = true;
        std::cout << buffer.data();
    }

    if (pclose(pipe) != 0) {
        return 1;
    }
    if (!found) {
        std::cout << "No history results.\n";
    }
    return 0;
#endif
}

}  // namespace atlast
