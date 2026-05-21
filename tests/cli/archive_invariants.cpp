#include "archive_invariants.hpp"
#include "test_helpers.hpp"

#include <catch2/catch.hpp>

#include <algorithm>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace bambu_cli_test {

static std::string regex_escape(const std::string& s) {
    static const std::regex meta(R"([.^$|()\[\]{}*+?\\])");
    return std::regex_replace(s, meta, R"(\$&)");
}

static std::string read_entry_string(const std::string& zip_path,
                                     const std::string& entry) {
    auto bytes = read_zip_entry(zip_path, entry);
    return std::string(bytes.begin(), bytes.end());
}

void assert_relationships_resolve(const std::string& zip_path) {
    auto entries = list_zip_entries(zip_path);
    std::set<std::string> entry_set(entries.begin(), entries.end());

    std::vector<std::string> rels;
    for (const auto& e : entries) {
        if (e.size() >= 5 && e.compare(e.size() - 5, 5, ".rels") == 0)
            rels.push_back(e);
    }
    if (std::find(rels.begin(), rels.end(), "_rels/.rels") == rels.end())
        FAIL("archive missing _rels/.rels");

    static const std::regex re(R"re(Target\s*=\s*"([^"]+)")re");
    for (const auto& rf : rels) {
        const std::string body = read_entry_string(zip_path, rf);
        if (body.empty()) continue;
        auto it = std::sregex_iterator(body.begin(), body.end(), re);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) {
            std::string tgt = (*it)[1].str();
            if (!tgt.empty() && tgt[0] == '/') tgt.erase(0, 1);
            if (entry_set.find(tgt) == entry_set.end())
                FAIL("rels target '" << tgt << "' in " << rf << " not in archive");
        }
    }
}

// PNG IHDR: bytes 0-7 signature, 8-11 chunk length (=13), 12-15 "IHDR",
// 16-19 width (big-endian u32), 20-23 height (big-endian u32).
static bool decode_png_ihdr(const std::vector<uint8_t>& bytes,
                            uint32_t& width, uint32_t& height) {
    if (bytes.size() < 24) return false;
    static const uint8_t SIG[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    for (int i = 0; i < 8; ++i) if (bytes[i] != SIG[i]) return false;
    if (bytes[12] != 'I' || bytes[13] != 'H' ||
        bytes[14] != 'D' || bytes[15] != 'R') return false;
    width  = (uint32_t(bytes[16]) << 24) | (uint32_t(bytes[17]) << 16) |
             (uint32_t(bytes[18]) << 8)  |  uint32_t(bytes[19]);
    height = (uint32_t(bytes[20]) << 24) | (uint32_t(bytes[21]) << 16) |
             (uint32_t(bytes[22]) << 8)  |  uint32_t(bytes[23]);
    return true;
}

void assert_plate_thumbnails_128(const std::string& zip_path) {
    auto entries = list_zip_entries(zip_path);
    static const std::regex plate_re(R"re(^Metadata/plate_(\d+)\.png$)re");
    std::set<int> plate_indices;
    for (const auto& e : entries) {
        std::smatch m;
        if (std::regex_match(e, m, plate_re))
            plate_indices.insert(std::stoi(m[1].str()));
    }
    if (plate_indices.empty())
        FAIL("archive has no Metadata/plate_N.png entries");

    for (int idx : plate_indices) {
        const std::string big   = "Metadata/plate_" + std::to_string(idx) + ".png";
        const std::string small = "Metadata/plate_" + std::to_string(idx) + "_small.png";
        auto big_bytes   = read_zip_entry(zip_path, big);
        auto small_bytes = read_zip_entry(zip_path, small);
        if (big_bytes.empty())   FAIL("missing " << big);
        if (small_bytes.empty()) FAIL("missing " << small);
        uint32_t w = 0, h = 0;
        if (!decode_png_ihdr(big_bytes, w, h))
            FAIL(big << " is not a valid PNG (IHDR missing)");
        if (w != 128 || h != 128)
            FAIL(big << " is " << w << "x" << h << ", expected 128x128");
        if (!decode_png_ihdr(small_bytes, w, h))
            FAIL(small << " is not a valid PNG (IHDR missing)");
        if (w != 128 || h != 128)
            FAIL(small << " is " << w << "x" << h << ", expected 128x128");
    }
}

void assert_printable_area_4_points(const std::string& zip_path) {
    const std::string body =
        read_entry_string(zip_path, "Metadata/project_settings.config");
    if (body.empty())
        FAIL("Metadata/project_settings.config is missing or empty");

    // Find the printable_area JSON array. Format example:
    //   "printable_area": [
    //       "0x0", "256x0", "256x256", "0x256"
    //   ]
    static const std::regex outer(
        R"re("printable_area"\s*:\s*\[([^\]]*)\])re");
    std::smatch m;
    if (!std::regex_search(body, m, outer))
        FAIL("printable_area key not found in project_settings.config");

    std::string list = m[1].str();
    static const std::regex point_re(R"re("[-0-9\.]+x[-0-9\.]+")re");
    int n = 0;
    auto it = std::sregex_iterator(list.begin(), list.end(), point_re);
    auto end = std::sregex_iterator();
    for (; it != end; ++it) ++n;
    if (n != 4)
        FAIL("printable_area has " << n << " entries; expected exactly 4");
}

void assert_parts_have_source_file(const std::string& zip_path) {
    const std::string body =
        read_entry_string(zip_path, "Metadata/model_settings.config");
    if (body.empty())
        FAIL("Metadata/model_settings.config is missing or empty");

    // Locate every <part ...> ... </part> block; require a source_file metadata
    // entry inside it.
    static const std::regex part_re(R"re(<part\b[^>]*>([\s\S]*?)</part>)re");
    auto it = std::sregex_iterator(body.begin(), body.end(), part_re);
    auto end = std::sregex_iterator();
    int part_count = 0;
    for (; it != end; ++it) {
        ++part_count;
        const std::string inner = (*it)[1].str();
        if (inner.find("source_file") == std::string::npos)
            FAIL("<part> #" << part_count << " lacks source_file metadata");
    }
    // Note: zero <part> blocks is acceptable (template with no objects).
}

void assert_object_extruder(const std::string& zip_path,
                            const std::string& obj_name, int slot) {
    const std::string body =
        read_entry_string(zip_path, "Metadata/model_settings.config");
    if (body.empty())
        FAIL("Metadata/model_settings.config is missing or empty");

    // Find the <object ...> block whose <metadata key="name" value="<obj>"/>
    // matches, then look for extruder=<slot>.
    static const std::regex obj_re(
        R"re(<object\b[^>]*>([\s\S]*?)</object>)re");
    auto it = std::sregex_iterator(body.begin(), body.end(), obj_re);
    auto end = std::sregex_iterator();
    for (; it != end; ++it) {
        const std::string inner = (*it)[1].str();
        std::regex name_re(R"re(key\s*=\s*"name"[^/]*value\s*=\s*")re" +
                           regex_escape(obj_name) + "\"");
        if (std::regex_search(inner, name_re)) {
            std::regex extr_re(R"re(key\s*=\s*"extruder"[^/]*value\s*=\s*")re" +
                               regex_escape(std::to_string(slot)) + "\"");
            if (!std::regex_search(inner, extr_re))
                FAIL("<object name='" << obj_name <<
                     "'> lacks extruder=" << slot);
            return;
        }
    }
    FAIL("no <object> named '" << obj_name << "' in model_settings.config");
}

void run_all_basic(const std::string& zip_path) {
    assert_relationships_resolve(zip_path);
    assert_plate_thumbnails_128(zip_path);
    assert_printable_area_4_points(zip_path);
    SUCCEED("run_all_basic passed for " << zip_path);
}

} // namespace bambu_cli_test
