#pragma once
#include "reports.hpp"

namespace drl
{
inline Bytes parseFont(const Bytes &raw)
{
    std::istringstream input(std::string(raw.begin(), raw.end()));
    std::string line;
    require(bool(std::getline(input, line)), "Empty font file.");
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    require(line == "MAX7456", "Invalid font header.");
    Bytes packed;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        require(line.size() == 8 && line.find_first_not_of("01") == std::string::npos,
                "Invalid font byte.");
        packed.push_back(uint8_t(std::stoul(line, nullptr, 2)));
    }
    require(packed.size() == 256 * 64, "Expected 256 font characters.");
    Bytes font;
    for (size_t i = 0; i < 256; ++i)
        font.insert(font.end(), packed.begin() + i * 64, packed.begin() + i * 64 + 54);
    return font;
}

inline std::string fontLayout(const Bytes &image, const Json &profile)
{
    for (auto &entry : profile.at("font_signatures"))
        if (maskedHash(image, entry.at("mask")) == entry.at("hash"))
            return entry.at("font");
    throw std::runtime_error(
        "The backup's OSD font is unknown. Its firmware has not been written.");
}

struct Backup
{
    fs::path folder, file;
    Json report, catalog, profile;
    Bytes image, fontData;
    std::string key, font, fontHash;
};

inline Backup loadBackup(const fs::path &file, const Json &catalog)
{
    Backup backup;
    backup.folder = file.parent_path();
    backup.file = file;
    if (!fs::exists(backup.folder / L"report.json") && fs::is_directory(backup.folder / L"Data"))
    {
        std::vector<fs::path> matches;
        for (auto &entry : fs::directory_iterator(backup.folder / L"Data"))
            if (entry.is_directory() && !fs::is_symlink(entry.symlink_status()) &&
                fs::exists(entry.path() / L"report.json"))
            {
                auto report = jsonRead(entry.path() / L"report.json");
                if (report.contains("exports") &&
                    report.at("exports").value("firmware", "") == file.filename().u8string())
                    matches.push_back(entry.path());
            }
        require(matches.size() == 1, "Backup report not found. Keep the Data folder beside the backups.");
        backup.folder = matches.front();
    }
    backup.report = jsonRead(backup.folder / L"report.json");
    bool exported = backup.folder.parent_path() == file.parent_path() / L"Data" &&
                    backup.report.contains("exports") &&
                    backup.report.at("exports").value("firmware", "") == file.filename().u8string();
    require(exported || file.filename() == reportFile(backup.folder, backup.report, "before.bin").filename(),
            "Select the firmware backup .bin file from a flasher folder.");
    backup.catalog = jsonRead(backup.folder / L"catalog.json");
    backup.key = backup.report.at("profile");
    backup.profile = catalog.at("profiles").at(backup.key);
    auto recorded = backup.catalog.at("profiles").at(backup.key);
    require(recorded.at("kind") == backup.profile.at("kind"), "Backup board type differs.");
    backup.image = read(file);
    require(hash(backup.image) == backup.report.at("before_sha256"), "Backup checksum differs.");
    if (backup.profile.at("kind") == "fc")
    {
        require(backup.image.size() == backup.profile.at("flash_size").get<size_t>() &&
                    recorded.at("device_id") == backup.profile.at("device_id"),
                "Backup processor differs.");
        auto stack = u32(backup.image, 0), entry = u32(backup.image, 4);
        bool ram = stack >= 0x20000000 && stack <= 0x20080000;
        if (backup.key == "FC_F4")
            ram |= stack >= 0x10000000 && stack <= 0x10010000;
        require(ram && (entry & 1) && entry >= 0x08000000 &&
                    entry < 0x08000000 + backup.image.size(),
                "Invalid FC backup vector table.");
    }
    else
    {
        require(backup.image.size() == 0x80000, "Wrong backup size.");
        if (backup.profile.at("kind") == "led")
        {
            auto wiring = detectLED(backup.image, catalog, true);
            require(wiring.empty() || wiring == backup.key, "Backup wiring differs.");
        }
    }
    if (backup.profile.at("kind") == "ec")
    {
        try
        {
            backup.font = fontLayout(backup.image, backup.profile);
        }
        catch (const std::exception &)
        {
            bool savedBefore = backup.report.contains("font_before_sha256");
            bool unchanged = backup.report.value("font_unchanged", false) &&
                             backup.report.value("font_verified", false);
            require(savedBefore || (unchanged && backup.report.contains("font_readback_sha256")),
                    "The backup's font is unknown and no font backup was saved.");
            backup.fontData = read(reportFile(backup.folder, backup.report,
                                             savedBefore ? "font_before.bin" : "font_after.bin"));
            backup.fontHash = backup.report.at(savedBefore ? "font_before_sha256"
                                                           : "font_readback_sha256");
            require(backup.fontData.size() == 13824 &&
                        hash(backup.fontData) == backup.fontHash,
                    "Font backup checksum differs.");
            backup.font = "backup";
        }
    }
    return backup;
}

inline Bytes restoreImage(const Bytes &current, const Backup &backup, const Json &catalog,
                          const std::string &connectedProfile)
{
    require(current.size() == backup.image.size(), "Backup flash size differs from the board.");
    require(connectedProfile == backup.key ||
                (connectedProfile.rfind("EC_", 0) == 0 && backup.key.rfind("EC_", 0) == 0),
            "Backup is for a different board.");
    auto restored = backup.image;
    if (backup.profile.at("kind") != "fc")
    {
        if (backup.profile.at("kind") == "led")
        {
            auto wiring = detectLED(current, catalog, true);
            require(wiring.empty() || wiring == backup.key,
                    "This firmware is not in the build's compatibility list. Backup saved; nothing written.");
        }
        // UF2 cannot replace the bootloader. Keep the one already installed.
        std::copy_n(current.begin(), 0x4000, restored.begin());
    }
    return restored;
}
} // namespace drl
