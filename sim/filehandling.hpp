#ifndef FILE_H
#define FILE_H

#include <iostream>
#include <filesystem>
#include <optional>
#include <string_view>
class FileHandling
{
public:
    std::optional<std::string> getData(const std::filesystem::path &fileName);
    bool setData(std::string_view data, const std::filesystem::path &fileName);
    FileHandling(/* args */);
    ~FileHandling();
};
#endif