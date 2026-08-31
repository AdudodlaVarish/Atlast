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
};

int run(int argc, char* argv[]);
int index_directory(const std::filesystem::path& root,
                    std::string_view database_path);
int list_sources(std::string_view database_path);
int refresh_sources(std::string_view database_path);
int forget_directory(const std::filesystem::path& root,
                     std::string_view database_path);
int search(const SearchRequest& request, std::string_view database_path,
           int limit);

}  // namespace atlast
