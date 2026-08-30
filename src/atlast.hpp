#pragma once

#include <filesystem>
#include <string_view>

namespace atlast {

int run(int argc, char* argv[]);
int index_directory(const std::filesystem::path& root,
                    std::string_view database_path);
int search(std::string_view query, std::string_view database_path, int limit);

}  // namespace atlast
