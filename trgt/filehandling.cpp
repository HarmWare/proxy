#include <iostream>
#include <fstream>
#include "filehandling.hpp"

FileHandling::FileHandling() {}
FileHandling::~FileHandling() {}

std::string FileHandling::getData(const std::string fileName)
{
    /* open the file as input stream */
    std::ifstream inputFile(fileName);

    /* is it opend well? */
    if (!inputFile.is_open())
    {
        std::cerr << "Error opening the file." << std::endl;
        return {};
    }

    /* read the data */
    std::string data;
    std::getline(inputFile, data);

    /* close the file */
    inputFile.close();
    return data;
}

void FileHandling::setData(const std::string data, const std::string fileName)
{
    /* open the file as output stream */
    std::ofstream outputFile(fileName, std::ios::app);

    /* is it opend well? */
    if (!outputFile.is_open())
    {
        std::cerr << "Error opening the file." << std::endl;
    }

    /* write the data */
    outputFile << data << "\n";

    /* close the file */
    outputFile.close();
}