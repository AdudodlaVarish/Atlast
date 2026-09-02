#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace atlast {

struct SearchRequest {
    std::string text;
    std::string path;
    std::string extension;
    std::optional<int> modified_days;
    bool explain = false;
};

int run(int argc, char* argv[]);
int index_directory(const std::filesystem::path& root,
                    std::string_view database_path);
int list_sources(std::string_view database_path);
int refresh_sources(std::string_view database_path);
int watch_sources(std::string_view database_path);
int forget_directory(const std::filesystem::path& root,
                     std::string_view database_path);
int search(const SearchRequest& request, std::string_view database_path,
           int limit);
int search_git_history(const std::filesystem::path& repository,
                       std::string_view query, int limit);

}  // namespace atlast
