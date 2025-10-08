#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace reportgen
{
    struct Entry
    {
        std::string header;
        std::string description;
        std::filesystem::path imagePath;
    };

    // rootFolder = exe_dir();  templateDocx = rootFolder/"template.docx"
    bool generateDocx(const std::filesystem::path& templateDocx,
        const std::filesystem::path& outputDocx,
        const std::vector<Entry>& entries);
}
