#include <catch2/catch.hpp>
#include "io.hpp"
#include "invariant_guard.hpp"
#include "project_state.hpp"

#include "libslic3r/Model.hpp"

#include <boost/filesystem.hpp>
#include <miniz.h>

#include <cstring>
#include <string>

namespace fs = boost::filesystem;

static std::string read_entry(const std::string& archive,
                              const std::string& name) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    REQUIRE(mz_zip_reader_init_file(&zip, archive.c_str(), 0));
    const int idx = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
    REQUIRE(idx >= 0);
    mz_zip_archive_file_stat st;
    REQUIRE(mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(idx), &st));
    std::string buf(static_cast<size_t>(st.m_uncomp_size), '\0');
    if (st.m_uncomp_size > 0) {
        REQUIRE(mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(idx),
                                             &buf[0], buf.size(), 0));
    }
    mz_zip_reader_end(&zip);
    return buf;
}

static bool has_entry(const std::string& archive, const std::string& name) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, archive.c_str(), 0)) return false;
    const int idx = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
    mz_zip_reader_end(&zip);
    return idx >= 0;
}

TEST_CASE("test_reference.3mf round-trip preserves canonical aux layout",
          "[roundtrip][reference_passthrough]") {
    const std::string src = BAMBU_CLI_FIXTURE_TEST_REFERENCE_3MF;
    REQUIRE(fs::exists(src));

    bambu_cli::ProjectState state;
    auto lr = bambu_cli::load_project(src, state);
    REQUIRE(lr.ok);

    const fs::path out = fs::temp_directory_path() /
                         fs::unique_path("refmf-%%%%-%%%%.3mf");
    auto sr = bambu_cli::save_project(state, out.string());
    INFO("save error_code: " << sr.error_code);
    INFO("save error_message: " << sr.error_message);
    REQUIRE(sr.ok);

    // 1-3. Aux files preserved with canonical paths + identical content.
    const auto pre_jpg_a  = read_entry(src,           "Auxiliaries/Model Pictures/50calpellet.jpg");
    const auto post_jpg_a = read_entry(out.string(),  "Auxiliaries/Model Pictures/50calpellet.jpg");
    REQUIRE(pre_jpg_a == post_jpg_a);

    const auto pre_jpg_b  = read_entry(src,           "Auxiliaries/Profile Pictures/5.45x39mm.jpg");
    const auto post_jpg_b = read_entry(out.string(),  "Auxiliaries/Profile Pictures/5.45x39mm.jpg");
    REQUIRE(pre_jpg_b == post_jpg_b);

    const auto pre_pdf  = read_entry(src,           "Auxiliaries/Assembly Guide/D_02_40sw_PRINT_GUIDE.pdf");
    const auto post_pdf = read_entry(out.string(),  "Auxiliaries/Assembly Guide/D_02_40sw_PRINT_GUIDE.pdf");
    REQUIRE(pre_pdf == post_pdf);

    // 4. Thumbnails preserved.
    REQUIRE(has_entry(out.string(), "Auxiliaries/.thumbnails/thumbnail_3mf.png"));
    REQUIRE(has_entry(out.string(), "Auxiliaries/.thumbnails/thumbnail_middle.png"));
    REQUIRE(has_entry(out.string(), "Auxiliaries/.thumbnails/thumbnail_small.png"));

    // 5. Cover metadata: basename-only references.
    const auto model_xml = read_entry(out.string(), "3D/3dmodel.model");
    REQUIRE(model_xml.find("name=\"DesignerCover\">50calpellet.jpg<") != std::string::npos);
    REQUIRE(model_xml.find("name=\"ProfileCover\">5.45x39mm.jpg<")   != std::string::npos);

    // 6. Title / ProfileTitle preserved.
    REQUIRE(model_xml.find("name=\"Title\">Test Project for reference<")            != std::string::npos);
    REQUIRE(model_xml.find("name=\"ProfileTitle\">Test profile name for reference<") != std::string::npos);

    // Bonus: the cover_references_resolve guard passes on the saved archive
    // (auxiliary_passthrough is wired into run_guard and already enforced
    // by save_project's success above).
    std::string cov_err;
    REQUIRE(bambu_cli::check_cover_references_resolve(out.string(), &cov_err));
    REQUIRE(cov_err.empty());

    fs::remove(out);
}
