#include <iostream>
#include <fstream>
#include "filehandling.hpp"

#include "../common/observability.hpp"

FileHandling::FileHandling() {}
FileHandling::~FileHandling() {}

std::optional<std::string> FileHandling::getData(const std::filesystem::path &fileName)
{
    /* open the file as input stream */
    std::ifstream inputFile(fileName);

    /* is it opend well? */
    if (!inputFile.is_open())
    {
        obs::Logger::instance().log(
            obs::LogLevel::ERROR,
            "trgt",
            "file_open_failed",
            "Failed to open input file",
            {{"path", fileName.string()}});
        return std::nullopt;
    }

    /* read the data */
    std::string data;
    std::getline(inputFile, data);

    /* close the file */
    inputFile.close();
    return data;
}

bool FileHandling::setData(std::string_view data, const std::filesystem::path &fileName)
{
    /* open the file as output stream */
    std::ofstream outputFile(fileName, std::ios::app);

    /* is it opend well? */
    if (!outputFile.is_open())
    {
        obs::Logger::instance().log(
            obs::LogLevel::ERROR,
            "trgt",
            "file_open_failed",
            "Failed to open output file",
            {{"path", fileName.string()}});
        return false;
    }

    /* write the data */
    outputFile << data << "\n";

    /* close the file */
    outputFile.close();
    return true;
}