#include "../src/samba.hpp"
#include <cassert>
#include <iostream>

using namespace drl;
template <class F> void rejects(F action)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    assert(rejected);
}

int main(int argc, char **argv)
{
    fs::path temporary;
    try
    {
        assert(argc == 3);
        fs::path source = fs::u8path(argv[1]), package = fs::u8path(argv[2]);
        auto catalog = jsonRead(package / "bundle.json");
        auto font = parseFont(verifiedFile(package, catalog.at("fonts").at("betaflight")));
        auto original = parseFont(verifiedFile(package, catalog.at("fonts").at("original")));
        assert(hash(original) ==
               "bd453a77de8583b8663c868abf0ad8e2119a5df5491b221ad586b14e6812191c");
        assert(font.size() == 13824 && font != original);
        auto raw = verifiedFile(package, catalog.at("fonts").at("original"));
        raw.resize(raw.size() - 4);
        rejects([&] { parseFont(raw); });
        rejects([] { parseFont(Bytes{'b', 'a', 'd'}); });
        auto memory = original;
        unsigned writes = 0, attempts = 0;
        auto readGlyph = [&](uint8_t index) {
            return Bytes(memory.begin() + index * 54, memory.begin() + (index + 1) * 54);
        };
        auto writeGlyph = [&](uint8_t index, const Bytes &glyph) {
            ++writes;
            if (++attempts == 1)
                return;
            std::copy(glyph.begin(), glyph.end(), memory.begin() + index * 54);
        };
        assert(programFont(font, readGlyph, writeGlyph, [](unsigned) {}) == font);
        assert(writes > 1);
        writes = 0;
        programFont(font, readGlyph, writeGlyph, [](unsigned) {});
        assert(writes == 0);
        unsigned reads = 0;
        auto countedRead = [&](uint8_t index) {
            ++reads;
            return readGlyph(index);
        };
        memory = font;
        memory[17 * 54] ^= 1;
        writes = 0;
        auto installed = memory;
        assert(programFont(font, countedRead, writeGlyph, [](unsigned) {}, installed) == font);
        assert(writes == 1 && reads == 257);
        unsigned verifiedCharacters = 0;
        programFont(
            font, readGlyph, writeGlyph, [](unsigned) {}, memory,
            [&](unsigned count) { assert(count == ++verifiedCharacters); });
        assert(verifiedCharacters == 256);
        memory = original;
        writes = 0;
        rejects([&] { programFont(font, readGlyph, writeGlyph, [](unsigned) {}, font); });
        assert(writes == 0);
        rejects([&] { programFont(font, readGlyph, writeGlyph, [](unsigned) {}, Bytes{1}); });
        memory = original;
        rejects(
            [&] { programFont(font, readGlyph, [](uint8_t, const Bytes &) {}, [](unsigned) {}); });

        assert(fontBootloader(read(source / "baselines/ec_v21.bin")) == "2.0.0");
        for (auto file : {"ec_v23.bin", "ec_v25.bin"})
            assert(fontBootloader(read(source / "baselines" / file)) == "3.7.0");
        rejects([] { fontBootloader(Bytes(0x4000)); });
        rejects([] { fontBootloader(Bytes(7)); });
        temporary = fs::temp_directory_path() /
                    ("drl_restore_test_" + std::to_string(GetCurrentProcessId()));
        assert(fs::create_directory(temporary));
        auto testBackup = [&](const std::string &key, const std::string &baseline) {
            auto before = read(source / "baselines" / baseline);
            write(temporary / "before.bin", before);
            jsonWrite(temporary / "catalog.json", catalog);
            jsonWrite(temporary / "report.json",
                      {{"profile", key}, {"before_sha256", hash(before)}});
            auto backup = loadBackup(temporary / "before.bin", catalog);
            auto report = backup.report;
            report["schema"] = 2;
            auto named = reportFile(temporary, report, "before.bin");
            write(named, before);
            jsonWrite(temporary / "report.json", report);
            auto namedBackup = loadBackup(named, catalog);
            assert(namedBackup.image == backup.image && namedBackup.file == named &&
                   namedBackup.font == backup.font);
            rejects([&] { loadBackup(temporary / "before.bin", catalog); });
            write(reportFile(temporary, report, "after.bin"), before);
            rejects([&] { loadBackup(reportFile(temporary, report, "after.bin"), catalog); });
            jsonWrite(temporary / "report.json", backup.report);
            auto current = before;
            auto &profile = catalog.at("profiles").at(key);
            if (profile.at("kind") == "fc")
            {
                current = fcExpected(before, verifiedFile(package, profile.at("image")), true);
                assert(backup.font.empty() && backup.fontData.empty());
                assert(restoreImage(current, backup, catalog, key) == before);
                rejects([&] {
                    restoreImage(current, backup, catalog, key == "FC_F7" ? "FC_F4" : "FC_F7");
                });
            }
            else
            {
                current = mergeSAMD(before, verifiedFile(package, profile.at("image")),
                                    identify(before, profile), false);
                if (profile.at("kind") == "ec")
                {
                    assert(backup.font == "original");
                    assert(fontLayout(current, profile) == "betaflight");
                }
                current[17] ^= 1;
                auto restored = restoreImage(current, backup, catalog, key);
                assert(std::equal(restored.begin(), restored.begin() + 0x4000, current.begin()));
                assert(
                    std::equal(restored.begin() + 0x4000, restored.end(), before.begin() + 0x4000));
                if (profile.at("kind") == "led")
                    rejects([&] {
                        restoreImage(current, backup, catalog,
                                     key == "LED_AB" ? "LED_C" : "LED_AB");
                    });
            }
            if (profile.at("kind") != "fc")
            {
                current[0x9000] ^= 1;
                auto restored = restoreImage(current, backup, catalog, key);
                assert(std::equal(restored.begin() + 0x4000, restored.end(),
                                  before.begin() + 0x4000));
            }
            before[0x9000] ^= 1;
            write(temporary / "before.bin", before);
            rejects([&] { loadBackup(temporary / "before.bin", catalog); });
        };
        testBackup("FC_F7", "fc_f7.bin");
        testBackup("FC_F4", "fc_f4.bin");
        testBackup("EC_F4", "ec_v21.bin");
        testBackup("EC_F7", "ec_v23.bin");
        testBackup("EC_F7", "ec_v25.bin");
        auto titledEC = read(source / "baselines/ec_v21.bin");
        const std::string title = "OpenDRLProject_";
        std::copy(title.begin(), title.end(), titledEC.begin() + 0x22409);
        titledEC[0x22409 + title.size()] = 0;
        for (auto key : {"EC_F4", "EC_F7"})
        {
            assert(identify(titledEC, catalog.at("profiles").at(key)).at("name") ==
                   "EC v21 OpenDRL");
            assert(fontLayout(titledEC, catalog.at("profiles").at(key)) == "original");
        }
        testBackup("LED_AB", "ledc_v20.bin");
        testBackup("LED_AB", "ledc_v22.bin");
        testBackup("LED_C", "ledc_v21.bin");
        auto unknownLED = read(source / "baselines/ledc_v22.bin");
        unknownLED[0x9000] ^= 1;
        write(temporary / "before.bin", unknownLED);
        jsonWrite(temporary / "report.json",
                  {{"profile", "LED_AB"}, {"before_sha256", hash(unknownLED)}});
        auto unknownBackup = loadBackup(temporary / "before.bin", catalog);
        assert(unknownBackup.image == unknownLED);
        rejects([&] { detectLED(unknownLED, catalog); });
        rejects([&] {
            restoreImage(read(source / "baselines/ledc_v21.bin"), unknownBackup,
                         catalog, "LED_AB");
        });
        auto otherFC = read(source / "baselines/fc_f4.bin");
        otherFC[0x9000] ^= 1;
        write(temporary / "before.bin", otherFC);
        jsonWrite(temporary / "report.json",
                  {{"profile", "FC_F4"}, {"before_sha256", hash(otherFC)}});
        auto other = loadBackup(temporary / "before.bin", catalog);
        assert(other.image == otherFC && other.font.empty());
        auto ecImage = read(source / "baselines/ec_v25.bin");
        auto withoutFontSignatures = catalog;
        withoutFontSignatures["profiles"]["EC_F7"]["font_signatures"] = Json::array();
        write(temporary / "before.bin", ecImage);
        write(temporary / "font_before.bin", original);
        jsonWrite(temporary / "report.json", {{"profile", "EC_F7"},
                                              {"before_sha256", hash(ecImage)},
                                              {"font_before_sha256", hash(original)}});
        other = loadBackup(temporary / "before.bin", withoutFontSignatures);
        assert(other.font == "backup" && other.fontData == original);
        auto report = other.report;
        report["schema"] = 2;
        write(reportFile(temporary, report, "before.bin"), ecImage);
        write(reportFile(temporary, report, "font_before.bin"), original);
        jsonWrite(temporary / "report.json", report);
        auto named = loadBackup(reportFile(temporary, report, "before.bin"), withoutFontSignatures);
        assert(named.font == "backup" && named.fontData == original);
        report.erase("font_before_sha256");
        report["font_unchanged"] = true;
        report["font_verified"] = true;
        report["font_readback_sha256"] = hash(original);
        write(reportFile(temporary, report, "font_after.bin"), original);
        jsonWrite(temporary / "report.json", report);
        auto unchanged =
            loadBackup(reportFile(temporary, report, "before.bin"), withoutFontSignatures);
        assert(unchanged.fontData == original && unchanged.fontHash == hash(original));
        report["font_unchanged"] = false;
        jsonWrite(temporary / "report.json", report);
        rejects([&] {
            loadBackup(reportFile(temporary, report, "before.bin"), withoutFontSignatures);
        });
        report["font_unchanged"] = true;
        report["font_verified"] = false;
        jsonWrite(temporary / "report.json", report);
        rejects([&] {
            loadBackup(reportFile(temporary, report, "before.bin"), withoutFontSignatures);
        });

        auto portable = temporary / "portable";
        auto operation = portable / "Data/01 EC";
        fs::create_directories(operation);
        report = {{"schema", 3},
                  {"profile", "EC_F4"},
                  {"before_sha256", hash(titledEC)},
                  {"font_before_sha256", hash(original)}};
        write(reportFile(operation, report, "before.bin"), titledEC);
        write(reportFile(operation, report, "font_before.bin"), original);
        jsonWrite(operation / "catalog.json", catalog);
        exportBackups(operation, report);
        jsonWrite(operation / "report.json", report);
        assert(read(portable / "01 EC backup.bin") == titledEC &&
               parseFont(read(portable / "01 OSD font backup.mcm")) == original);
        auto restored = loadBackup(portable / "01 EC backup.bin", catalog);
        assert(restored.image == titledEC && restored.font == "original");
        auto relocated = temporary / "relocated";
        fs::rename(portable, relocated);
        assert(loadBackup(relocated / "01 EC backup.bin", catalog).image == titledEC);
        auto corrupt = titledEC;
        corrupt[0x9000] ^= 1;
        write(relocated / "01 EC backup.bin", corrupt);
        rejects([&] { loadBackup(relocated / "01 EC backup.bin", catalog); });
        rejects([&] { exportBackups(relocated / "Data/01 EC", report); });
        assert(read(relocated / "Data/01 EC/EC backup.bin") == titledEC);
        assert(fs::canonical(temporary).parent_path() == fs::canonical(fs::temp_directory_path()) &&
               temporary.filename() == "drl_restore_test_" + std::to_string(GetCurrentProcessId()));
        fs::remove_all(temporary);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
