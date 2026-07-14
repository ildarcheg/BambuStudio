#include <catch2/catch.hpp>
#include "io.hpp"
#include "project_state.hpp"

#include "libslic3r/Model.hpp"

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

// Non-ASCII characters that have triggered or could trigger XML/encoding bugs
// in the bbs_3mf writer/reader pair:
//   — em-dash, U+2014, UTF-8 E2 80 94  (the case that surfaced this bug)
//   测试 CJK,        U+6D4B U+8BD5
//   "x" smart quotes,U+201C / U+201D
//   é Latin-1 supp., U+00E9
//
// All metadata writer call sites in bbs_3mf.cpp:7004-7009 are already wrapped
// in xml_escape(). xml_escape (utils.cpp:1229) only escapes " ' & < > so
// non-ASCII UTF-8 bytes pass through unchanged — which is correct behavior.
// The XML declaration at bbs_3mf.cpp:6696 etc. correctly declares UTF-8. So
// this test failing means the bug is either (a) the writer mangles bytes on
// output, (b) the reader's expat config rejects valid UTF-8, or (c) something
// in load_project's deserialization path strips/replaces non-ASCII bytes.
TEST_CASE("non-ASCII description round-trips byte-identically",
          "[roundtrip][non_ascii_metadata]") {
    const std::string kDescription =
        "Resin print \xE2\x80\x94 \xE6\xB5\x8B\xE8\xAF\x95 "
        "\xE2\x80\x9Cx\xE2\x80\x9D caf\xC3\xA9";

    const std::string src = BAMBU_CLI_FIXTURE_TEST_REFERENCE_3MF;
    REQUIRE(fs::exists(src));

    bambu_cli::ProjectState s;
    auto lr = bambu_cli::load_project(src, s);
    REQUIRE(lr.ok);
    REQUIRE(s.model.model_info);
    s.model.model_info->description = kDescription;

    const fs::path out = fs::temp_directory_path() /
                         fs::unique_path("nonascii-%%%%-%%%%.3mf");
    auto sr = bambu_cli::save_project(s, out.string());
    INFO("save error_code:    " << sr.error_code);
    INFO("save error_message: " << sr.error_message);
    REQUIRE(sr.ok);

    bambu_cli::ProjectState r;
    auto lr2 = bambu_cli::load_project(out.string(), r);
    INFO("reload error_code:    " << lr2.error_code);
    INFO("reload error_message: " << lr2.error_message);
    REQUIRE(lr2.ok);

    REQUIRE(r.model.model_info);
    REQUIRE(r.model.model_info->description == kDescription);

    fs::remove(out);
}

TEST_CASE("non-ASCII title and copyright round-trip byte-identically",
          "[roundtrip][non_ascii_metadata]") {
    const std::string kTitle     = "\xE6\xB5\x8B\xE8\xAF\x95 \xE2\x80\x94 v1"; // 测试 — v1
    const std::string kCopyright = "\xC2\xA9 2026 caf\xC3\xA9 labs";            // © 2026 café labs

    const std::string src = BAMBU_CLI_FIXTURE_TEST_REFERENCE_3MF;
    bambu_cli::ProjectState s;
    REQUIRE(bambu_cli::load_project(src, s).ok);
    REQUIRE(s.model.model_info);
    s.model.model_info->model_name = kTitle;
    s.model.model_info->copyright  = kCopyright;

    const fs::path out = fs::temp_directory_path() /
                         fs::unique_path("nonascii-tc-%%%%-%%%%.3mf");
    REQUIRE(bambu_cli::save_project(s, out.string()).ok);

    bambu_cli::ProjectState r;
    REQUIRE(bambu_cli::load_project(out.string(), r).ok);
    REQUIRE(r.model.model_info);
    REQUIRE(r.model.model_info->model_name == kTitle);
    REQUIRE(r.model.model_info->copyright  == kCopyright);

    fs::remove(out);
}
