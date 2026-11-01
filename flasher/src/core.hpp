#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <functional>
#include <atomic>
#include <memory>
#include "../third_party/json.hpp"
namespace drl
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Bytes = std::vector<unsigned char>;
inline void require(bool b, const std::string &s)
{
    if (!b)
        throw std::runtime_error(s);
}
inline Bytes read(const fs::path &p)
{
    auto close = [](void *handle) { CloseHandle(handle); };
    HANDLE handle = CreateFileW(p.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(handle != INVALID_HANDLE_VALUE, "Cannot read " + p.u8string());
    std::unique_ptr<void, decltype(close)> file(handle, close);
    LARGE_INTEGER size{};
    require(GetFileSizeEx(handle, &size) && size.QuadPart >= 0 &&
                size.QuadPart <= 32 * 1024 * 1024,
            "Invalid file size: " + p.u8string());
    Bytes b(size.QuadPart);
    DWORD received = 0;
    require(ReadFile(handle, b.data(), DWORD(b.size()), &received, nullptr) &&
                received == b.size(),
            "Incomplete read: " + p.u8string());
    return b;
}
inline void write(const fs::path &p, const Bytes &b)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    require(bool(f), "Cannot write " + p.u8string());
    f.write(reinterpret_cast<const char *>(b.data()), b.size());
    f.flush();
    require(bool(f), "Incomplete write: " + p.u8string());
}
inline Json jsonRead(const fs::path &p)
{
    auto b = read(p);
    return Json::parse(b);
}
inline void jsonWrite(const fs::path &p, const Json &j, unsigned retries = 20)
{
    static std::atomic<unsigned> sequence{0};
    auto s = j.dump(2);
    auto tmp = p;
    tmp += L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
           std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(sequence++) + L".tmp";
    DWORD error = ERROR_SUCCESS;
    HANDLE file = CreateFileW(tmp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        error = GetLastError();
    else
    {
        DWORD written = 0;
        if (!WriteFile(file, s.data(), DWORD(s.size()), &written, nullptr))
            error = GetLastError();
        else if (written != s.size())
            error = ERROR_WRITE_FAULT;
        else if (!FlushFileBuffers(file))
            error = GetLastError();
        CloseHandle(file);
        if (error == ERROR_SUCCESS)
        {
            for (unsigned attempt = 0;; ++attempt)
            {
                if (MoveFileExW(tmp.c_str(), p.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    return;
                error = GetLastError();
                if (attempt == retries || (error != ERROR_SHARING_VIOLATION &&
                                           error != ERROR_LOCK_VIOLATION &&
                                           error != ERROR_ACCESS_DENIED))
                    break;
                Sleep(25);
            }
        }
        DeleteFileW(tmp.c_str());
    }
    throw std::runtime_error("Cannot save " + p.filename().u8string() +
                             " (Windows error " + std::to_string(error) + ").");
}
inline bool tryJsonWrite(const fs::path &p, const Json &j) noexcept
{
    try
    {
        jsonWrite(p, j, 0);
        return true;
    }
    catch (const std::exception &error)
    {
        OutputDebugStringA(error.what());
        return false;
    }
    catch (...)
    {
        return false;
    }
}
inline std::string hash(const Bytes &b)
{
    BCRYPT_ALG_HANDLE a = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    DWORD size = 0, got = 0;
    unsigned char result[32];
    require(BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0,
            "SHA-256 unavailable.");
    BCryptGetProperty(a, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &got,
                      0);
    Bytes obj(size);
    auto status = BCryptCreateHash(a, &h, obj.data(), size, nullptr, 0, 0);
    if (status == 0)
        status = BCryptHashData(h, const_cast<PUCHAR>(b.data()), ULONG(b.size()), 0);
    if (status == 0)
        status = BCryptFinishHash(h, result, 32, 0);
    if (h)
        BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(a, 0);
    require(status == 0, "SHA-256 failed.");
    std::ostringstream s;
    for (auto v : result)
        s << std::hex << std::setw(2) << std::setfill('0') << int(v);
    return s.str();
}
inline uint32_t u32(const Bytes &b, size_t p)
{
    require(p <= b.size() && b.size() - p >= 4, "Truncated word.");
    return b[p] | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 | uint32_t(b[p + 3]) << 24;
}
inline void put(Bytes &b, size_t p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        b.at(p + i) = uint8_t(v >> (i * 8));
}
inline Bytes decodeUF2(const Bytes &raw)
{
    require(raw.size() == 2048 * 512, "Expected a complete 512 KiB CURRENT.UF2.");
    Bytes b(0x80000);
    std::set<uint32_t> seen, blocks;
    for (size_t p = 0; p < raw.size(); p += 512)
    {
        require(u32(raw, p) == 0x0a324655 && u32(raw, p + 4) == 0x9e5d5157 &&
                    u32(raw, p + 508) == 0x0ab16f30,
                "Invalid UF2 signature.");
        auto flags = u32(raw, p + 8), a = u32(raw, p + 12), n = u32(raw, p + 20);
        require((flags == 0 || flags == 0x2000) && u32(raw, p + 16) == 256 &&
                    u32(raw, p + 24) == 2048,
                "Unsupported UF2 format.");
        require(flags == 0 || u32(raw, p + 28) == 0x55114460, "Wrong UF2 processor.");
        require(a % 256 == 0 && a <= 0x7ff00 && n < 2048 && seen.insert(a).second &&
                    blocks.insert(n).second,
                "Duplicate or invalid UF2 block.");
        std::copy_n(raw.begin() + p + 32, 256, b.begin() + a);
    }
    return b;
}
inline Bytes encodeUF2(const Bytes &b)
{
    require(b.size() == 0x80000, "Wrong SAMD51 image size.");
    size_t count = (b.size() - 0x4000) / 256;
    Bytes out(count * 512);
    for (size_t i = 0; i < count; i++)
    {
        size_t p = i * 512, a = 0x4000 + i * 256;
        put(out, p, 0x0a324655);
        put(out, p + 4, 0x9e5d5157);
        put(out, p + 8, 0x2000);
        put(out, p + 12, a);
        put(out, p + 16, 256);
        put(out, p + 20, i);
        put(out, p + 24, count);
        put(out, p + 28, 0x55114460);
        put(out, p + 508, 0x0ab16f30);
        std::copy_n(b.begin() + a, 256, out.begin() + p + 32);
    }
    return out;
}
inline std::map<uint32_t, uint8_t> parseHEX(const Bytes &raw, size_t flashSize)
{
    std::istringstream in(std::string(raw.begin(), raw.end()));
    std::string line;
    uint32_t base = 0;
    bool end = false;
    std::map<uint32_t, uint8_t> out;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        require(!end && line.front() == ':' && line.size() % 2 == 1, "Invalid Intel HEX record.");
        Bytes r;
        for (size_t i = 1; i < line.size(); i += 2)
        {
            auto h = line.substr(i, 2);
            require(h.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos,
                    "Invalid HEX digit.");
            r.push_back(uint8_t(std::stoul(h, nullptr, 16)));
        }
        require(r.size() >= 5 && r.size() == size_t(r[0]) + 5, "Invalid HEX length.");
        unsigned sum = 0;
        for (auto v : r)
            sum += v;
        require((sum & 255) == 0, "HEX checksum mismatch.");
        auto n = r[0], type = r[3];
        uint32_t offset = (r[1] << 8) | r[2];
        if (type == 0)
        {
            for (unsigned i = 0; i < n; i++)
            {
                uint64_t a = uint64_t(base) + offset + i;
                require(a >= 0x08000000 && a < 0x08000000 + flashSize,
                        "HEX address outside selected processor.");
                require(a < 0x08004000 || a >= 0x08008000, "FC image overwrites settings.");
                require(out.emplace(uint32_t(a), r[4 + i]).second, "Overlapping HEX data.");
            }
        }
        else if (type == 4)
        {
            require(n == 2 && offset == 0, "Invalid HEX base.");
            base = ((uint32_t(r[4]) << 8) | r[5]) << 16;
        }
        else if (type == 1)
        {
            require(n == 0 && offset == 0, "Invalid HEX end.");
            end = true;
        }
        else if (type == 5)
            require(n == 4 && offset == 0, "Invalid HEX entry point.");
        else
            throw std::runtime_error("Unsupported HEX record.");
    }
    require(end && out.count(0x08000000), "HEX is incomplete.");
    return out;
}
inline unsigned long fcDeviceID(const std::string &info)
{
    auto start = info.find("Device ID");
    require(start != std::string::npos, "Cannot read the FC chip ID.");
    require(info.find("Device ID", start + 1) == std::string::npos,
            "Connect exactly one flight controller.");
    auto end = info.find_first_of("\r\n", start);
    start = info.find(':', start);
    require(start != std::string::npos && start < end, "Cannot read the FC chip ID.");
    auto text = info.substr(start + 1, end - start - 1);
    try
    {
        size_t consumed = 0;
        auto value = std::stoul(text, &consumed, 16);
        require(text.find_first_not_of(" \t", consumed) == std::string::npos,
                "Invalid FC chip ID.");
        return value;
    }
    catch (const std::exception &)
    {
        throw std::runtime_error("Invalid FC chip ID.");
    }
}
inline std::string detectFC(const std::string &info, const Json &catalog)
{
    auto actual = fcDeviceID(info);
    std::vector<std::string> matches;
    for (auto &entry : catalog.at("profiles").items())
        if (entry.value().value("kind", "") == "fc" &&
            std::stoul(entry.value().at("device_id").get<std::string>(), nullptr, 16) == actual)
            matches.push_back(entry.key());
    require(matches.size() == 1,
            matches.empty() ? "This FC chip is not supported by the selected build."
                            : "The build contains more than one match for this FC.");
    return matches.front();
}
inline bool matchesPendingProfile(const std::string &selection, const Json &report)
{
    auto profile = report.value("profile", "");
    if (selection == "FC_AUTO")
        return profile == "FC_F4" || profile == "FC_F7";
    if (selection == "FC_F4" || selection == "FC_F7")
        return profile == selection;
    if (selection == "EC_AUTO")
        return profile == "EC_F4" || profile == "EC_F7";
    if (selection == "LED_AUTO")
        return profile == "LED_AB" || profile == "LED_C";
    if (selection == "LED_AB" || selection == "LED_C")
        return profile == selection;
    return report.value("requested_profile", profile) == selection;
}
inline void validateFC(const std::string &info, const Json &profile, size_t size)
{
    auto actual = fcDeviceID(info);
    auto selected = std::stoul(profile.at("device_id").get<std::string>(), nullptr, 16);
    require(actual == selected, "Connected FC does not match the selected F4/F7 hardware.");
    require(size == profile.at("flash_size").get<size_t>(), "FC backup has the wrong size.");
}
inline Bytes fcExpected(const Bytes &before, const Bytes &hex, bool defaults)
{
    require(before.size() == 0x80000 || before.size() == 0x100000, "Wrong FC flash size.");
    auto data = parseHEX(hex, before.size());
    Bytes out = before;
    std::vector<uint32_t> edges = {0, 0x4000, 0x8000, 0xc000, 0x10000, 0x20000};
    for (uint32_t i = 0x40000; i <= before.size(); i += 0x20000)
        edges.push_back(i);
    for (size_t i = 0; i + 1 < edges.size(); i++)
    {
        auto it = data.lower_bound(0x08000000 + edges[i]);
        if (it != data.end() && it->first < 0x08000000 + edges[i + 1])
            std::fill(out.begin() + edges[i], out.begin() + edges[i + 1], 255);
    }
    for (auto [a, v] : data)
        out.at(a - 0x08000000) = v;
    if (defaults)
        std::fill(out.begin() + 0x4000, out.begin() + 0x8000, 255);
    return out;
}
inline std::string maskedHash(Bytes b, const Json &ranges)
{
    for (auto &r : ranges)
    {
        size_t a = r.at(0), n = r.at(1);
        require(a <= b.size() && n <= b.size() - a, "Invalid identity mask.");
        std::fill_n(b.begin() + a, n, 0);
    }
    return hash(b);
}
inline Json identify(const Bytes &before, const Json &profile)
{
    for (auto &known : profile.at("known"))
        if (before.size() == known.at("size").get<size_t>() &&
            maskedHash(before, known.at("mask")) == known.at("hash"))
            return known;
    throw std::runtime_error(
        "This firmware is not in the build's compatibility list. Backup saved; nothing written.");
}
inline std::string detectLED(const Bytes &before, const Json &catalog, bool allowUnknown = false)
{
    std::set<std::string> matches;
    for (auto &p : catalog.at("profiles").items())
    {
        if (p.value().value("kind", "") != "led")
            continue;
        for (auto &k : p.value().at("known"))
            if (before.size() == k.at("size").get<size_t>() &&
                maskedHash(before, k.at("mask")) == k.at("hash"))
                matches.insert(p.key());
    }
    if (matches.empty() && allowUnknown)
        return {};
    require(matches.size() == 1,
            matches.empty() ? "LEDC firmware not recognized. Backup saved; nothing written."
                            : "LEDC detection is ambiguous. Nothing written.");
    return *matches.begin();
}
inline Bytes mergeSAMD(const Bytes &before, Bytes candidate, const Json &known, bool defaults)
{
    require(before.size() == 0x80000 && candidate.size() == before.size(), "Wrong image size.");
    require(!defaults, "EC and LED settings must be migrated, not reset by this installer.");
    require(known.value("preserve_supported", true),
            "This older settings layout needs a migration before flashing. Nothing written.");
    require(known.at("migration").is_array() && !known.at("migration").empty(),
            "No settings migration was supplied. Nothing written.");
    std::copy_n(before.begin(), 0x4000, candidate.begin());
    for (auto &r : known.value("clear", Json::array()))
    {
        size_t a = r.at(0), n = r.at(1);
        require(a >= 0x4000 && a <= candidate.size() && n <= candidate.size() - a,
                "Invalid migration clear range.");
        std::fill_n(candidate.begin() + a, n, 255);
    }
    for (auto &m : known.at("migration"))
    {
        size_t s = m.at(0), d = m.at(1), n = m.at(2);
        require(s >= 0x4000 && d >= 0x4000 && s <= before.size() && d <= candidate.size() &&
                    n <= before.size() - s && n <= candidate.size() - d,
                "Invalid migration range.");
        std::copy_n(before.begin() + s, n, candidate.begin() + d);
    }
    return candidate;
}
inline fs::path safeFile(const fs::path &root, const std::string &file)
{
    fs::path p = fs::u8path(file);
    require(!p.has_root_path() && !p.empty() && file.find(':') == std::string::npos,
            "Invalid package path.");
    for (auto &part : p)
        require(part != L"..", "Package path leaves build folder.");
    auto full = fs::weakly_canonical(root / p), base = fs::weakly_canonical(root);
    auto relative = full.lexically_relative(base);
    require(!relative.empty() && *relative.begin() != L"..", "Package file leaves build folder.");
    return full;
}
inline Bytes verifiedFile(const fs::path &root, const Json &entry)
{
    auto b = read(safeFile(root, entry.at("file")));
    require(b.size() == entry.at("size").get<size_t>() && hash(b) == entry.at("sha256"),
            "Build file hash mismatch. Reload a complete build package.");
    return b;
}
inline std::string detectEC(const Bytes &before, const Json &catalog, const fs::path &package)
{
    std::string selected;
    Bytes expected;
    for (auto &entry : catalog.at("profiles").items())
    {
        auto &profile = entry.value();
        if (profile.value("kind", "") != "ec")
            continue;
        Bytes image;
        for (auto &known : profile.at("known"))
        {
            if (before.size() != known.at("size").get<size_t>() ||
                maskedHash(before, known.at("mask")) != known.at("hash"))
                continue;
            if (image.empty())
                image = verifiedFile(package, profile.at("image"));
            auto merged = mergeSAMD(before, image, known, false);
            // Both EC packages share the application. Their migrated images must agree.
            require(selected.empty() || merged == expected,
                    "EC detection found conflicting updates. Nothing written.");
            if (selected.empty())
            {
                selected = entry.key();
                expected = std::move(merged);
            }
        }
    }
    require(!selected.empty(), "EC firmware not recognized. Backup saved; nothing written.");
    return selected;
}
inline std::wstring wide(const std::string &s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring o(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), o.data(), n);
    return o;
}
inline std::string narrow(const std::wstring &s)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string o(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), o.data(), n, nullptr, nullptr);
    return o;
}
inline std::string stamp()
{
    SYSTEMTIME t;
    GetSystemTime(&t);
    char s[48];
    snprintf(s, sizeof(s), "%04u%02u%02uT%02u%02u%02u_%03uZ", t.wYear, t.wMonth, t.wDay, t.wHour,
             t.wMinute, t.wSecond, t.wMilliseconds);
    return s;
}
} // namespace drl
