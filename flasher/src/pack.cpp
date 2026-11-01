#include "recovery.hpp"
#include "release.h"
#include <iostream>
using namespace drl;
int main(int argc, char **argv)
{
    try
    {
        require(argc == 3, "Usage: pack <firmware folder> <output bundle folder>");
        fs::path r = fs::u8path(argv[1]), out = fs::u8path(argv[2]);
        fs::create_directories(out);
        auto releases = r / "build/releases";
        auto source = jsonRead(releases / "manifest.json");
        auto date = stamp();
        Json cat = {{"schema", 1},
                    {"name", std::string("DRL Racer4 ") + DRL_VERSION +
                                 (DRL_SOURCE_MODIFIED ? " (modified)" : "")},
                    {"version", DRL_VERSION},
                    {"source_commit", DRL_SOURCE_COMMIT},
                    {"source_modified", bool(DRL_SOURCE_MODIFIED)},
                    {"date", date.substr(0, 4) + "-" + date.substr(4, 2) + "-" + date.substr(6, 2)},
                    {"profiles", Json::object()}};
        auto image = [&](std::string name)
        {
            auto b = read(releases / name);
            require(hash(b) == source["files"][name]["sha256"], "Release hash mismatch.");
            write(out / name, b);
            return Json{{"file", name}, {"size", b.size()}, {"sha256", hash(b)}};
        };
        auto migration = [&](std::string name)
        {
            Json result = Json::array();
            auto document = jsonRead(releases / name);
            for (auto &m : document.at("migration"))
                result.push_back({std::stoul(m["source"].get<std::string>(), nullptr, 16),
                                  std::stoul(m["destination"].get<std::string>(), nullptr, 16),
                                  m["size"]});
            return result;
        };
        fs::create_directories(out / "fonts");
        for (auto layout : {"original", "betaflight"})
        {
            auto path = std::string(layout) == "original" ? r / "baselines/osd.mcm"
                : r.parent_path() / "configurator/files/resources/osd/1/drl.mcm";
            auto raw = read(path);
            parseFont(raw);
            auto name = std::string("fonts/") + layout + ".mcm";
            write(out / name, raw);
            cat["fonts"][layout] = {{"file", name}, {"size", raw.size()}, {"sha256", hash(raw)}};
        }
        auto fontSignature = [&](const Bytes &data, const Json &mask, const char *layout)
        {
            return Json{{"hash", maskedHash(data, mask)}, {"mask", mask}, {"font", layout}};
        };
        auto same = migration("EC_v25_F7_migration.json"),
             old = migration("EC_v25_F4_migration.json");
        Json ecMask = {{0, 0x4000}, {0x20000, 0x30000}},
             oldMask = {{0, 0x4000}, {0x24000, 0x38000}},
             ledMask = {{0, 0x4000}, {0x1c000, 0x32000}};
        auto known = [&](const Bytes &b, std::string name, const Json &mask, const Json &m,
                         bool reset = false)
        {
            return Json{{"name", name},   {"size", b.size()},
                        {"mask", mask},   {"hash", maskedHash(b, mask)},
                        {"migration", m}, {"requires_defaults", reset}};
        };
        for (std::string target : {"F7", "F4"})
        {
            auto ecImage = image("EC_v25_" + target + "_candidate.bin");
            Json list = Json::array();
            for (auto name : {"ec_v23.bin", "ec_v25.bin"})
                list.push_back(known(read(r / "baselines" / name), name, ecMask, same));
            for (auto &h : jsonRead(r.parent_path() / "flasher/catalog/ec_history.json"))
                if (h.at("profile") == "EC_" + target)
                    list.push_back(h.at("known"));
            list.push_back(
                known(read(r / "baselines/ec_v21.bin"), "Original EC v21", oldMask, old));
            list.push_back(known(read(out / ecImage["file"].get<std::string>()), "DRL " DRL_VERSION " EC",
                                 ecMask, same));
            cat["profiles"]["EC_" + target] = {{"kind", "ec"}, {"image", ecImage}, {"known", list}};
            Json fonts = Json::array();
            for (auto name : {"ec_v21.bin", "ec_v23.bin", "ec_v25.bin"})
                fonts.push_back(fontSignature(read(r / "baselines" / name),
                    std::string(name) == "ec_v21.bin" ? oldMask : ecMask, "original"));
            for (auto &item : list)
            {
                bool stock = false;
                for (auto &entry : fonts)
                    stock |= entry.at("hash") == item.at("hash");
                if (!stock)
                    fonts.push_back({{"hash", item.at("hash")}, {"mask", item.at("mask")},
                                     {"font", item.value("font", "betaflight")}});
            }
            cat["profiles"]["EC_" + target]["font_signatures"] = fonts;
            std::string file = target == "F7" ? "FC_RACER4S.hex" : "FC_RACER4F4.hex";
            auto fcImage = image(file);
            auto base = read(r / "baselines" / (target == "F7" ? "fc_f7.bin" : "fc_f4.bin"));
            cat["profiles"]["FC_" + target] = {{"kind", "fc"},
                                               {"flash_size", base.size()},
                                               {"device_id", target == "F7" ? "0x452" : "0x413"},
                                               {"image", fcImage}};
        }
        Json oldLED = Json::array(), sameLED = Json::array(), oldLEDMask = {{0, 0x4000}},
             sameLEDMask = {{0, 0x4000}};
        auto fieldMap = jsonRead(r.parent_path() / "flasher/catalog/led_settings.json");
        for (auto &f : fieldMap)
        {
            auto a = std::stoul(f["source"].get<std::string>(), nullptr, 16),
                 d = std::stoul(f["destination"].get<std::string>(), nullptr, 16);
            oldLED.push_back({a, d, f["size"]});
            sameLED.push_back({d, d, f["size"]});
            oldLEDMask.push_back({a, 0x2000});
            sameLEDMask.push_back({d, 0x2000});
        }
        Json journalLED = sameLED;
        journalLED.push_back({0x2a001, 0x2a001, 0x1fff});
        auto historyPath = r.parent_path() / "flasher/catalog/led_history.json";
        Json history = fs::exists(historyPath) ? jsonRead(historyPath) : Json::array();
        for (std::string variant : {"AB", "C"})
        {
            auto entry = image("LEDC_v22_" + variant + "_candidate.bin");
            Json list = Json::array();
            auto names = variant == "AB" ? std::vector<std::string>{"ledc_v22.bin", "ledc_v20.bin"}
                                         : std::vector<std::string>{"ledc_v21.bin"};
            for (auto &name : names)
            {
                bool v20 = name == "ledc_v20.bin";
                auto k = known(read(r / "baselines" / name), name, v20 ? oldLEDMask : sameLEDMask,
                               v20 ? oldLED : sameLED);
                k["clear"] = {{0x2a001, 0x1fff}};
                list.push_back(k);
            }
            auto current = known(read(out / entry["file"].get<std::string>()),
                                 "DRL " DRL_VERSION " LEDC " + variant, sameLEDMask, journalLED);
            list.push_back(current);
            for (auto &h : history)
                if (h.at("profile") == "LED_" + variant)
                {
                    bool found = false;
                    for (auto &k : list)
                        if (k["hash"] == h["known"]["hash"])
                            found = true;
                    if (!found)
                        list.push_back(h["known"]);
                }
            cat["profiles"]["LED_" + variant] = {
                {"kind", "led"}, {"image", entry}, {"known", list}};
        }
        jsonWrite(out / "bundle.json", cat);
        std::cout << "Bundle prepared: " << out << "\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
