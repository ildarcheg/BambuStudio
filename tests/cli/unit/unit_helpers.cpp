#include "unit_helpers.hpp"
#include "../test_helpers.hpp"
#include "io.hpp"

#include <catch2/catch.hpp>
#include <miniz.h>
#include <boost/filesystem.hpp>
#include <cstring>

namespace bambu_cli_unit {

void load_reference_into(bambu_cli::ProjectState& state) {
    auto r = bambu_cli::load_project(bambu_cli_test::canonical_committed_3mf(),
                                     state);
    INFO("load_project: " << r.error_message);
    REQUIRE(r.ok);
}

void make_minimal_state(bambu_cli::ProjectState& state, int n_plates) {
    for (int i = 0; i < n_plates; ++i) {
        auto* pd = new Slic3r::PlateData();
        pd->plate_index = i;
        pd->plate_name  = std::string("Plate-") + std::to_string(i + 1);
        state.plate_data.push_back(pd);
    }
}

std::string fixture_stl(const std::string& name) {
    return std::string(BAMBU_CLI_FIXTURE_STL_DIR) + "/" + name;
}

namespace fs_helper = boost::filesystem;

void mutate_archive_remove_entry(const std::string& archive_path,
                                 const std::string& entry_name) {
    mz_zip_archive src;
    std::memset(&src, 0, sizeof(src));
    REQUIRE(mz_zip_reader_init_file(&src, archive_path.c_str(), 0));

    const fs_helper::path tmp = fs_helper::path(archive_path).string() + ".mut.tmp";
    mz_zip_archive dst;
    std::memset(&dst, 0, sizeof(dst));
    REQUIRE(mz_zip_writer_init_file(&dst, tmp.string().c_str(), 0));

    bool found = false;
    const mz_uint n = mz_zip_reader_get_num_files(&src);
    for (mz_uint i = 0; i < n; ++i) {
        char name[512];
        mz_zip_reader_get_filename(&src, i, name, sizeof(name));
        if (entry_name == name) { found = true; continue; }
        REQUIRE(mz_zip_writer_add_from_zip_reader(&dst, &src, i));
    }
    mz_zip_writer_finalize_archive(&dst);
    mz_zip_writer_end(&dst);
    mz_zip_reader_end(&src);

    REQUIRE(found); // mutating a missing entry is almost always a test bug

    fs_helper::remove(archive_path);
    fs_helper::rename(tmp, archive_path);
}

void mutate_archive_add_extra(const std::string& archive_path,
                              const std::string& entry_name,
                              const std::string& content) {
    mz_zip_archive src;
    std::memset(&src, 0, sizeof(src));
    REQUIRE(mz_zip_reader_init_file(&src, archive_path.c_str(), 0));

    const fs_helper::path tmp = fs_helper::path(archive_path).string() + ".mut.tmp";
    mz_zip_archive dst;
    std::memset(&dst, 0, sizeof(dst));
    REQUIRE(mz_zip_writer_init_file(&dst, tmp.string().c_str(), 0));

    const mz_uint n = mz_zip_reader_get_num_files(&src);
    for (mz_uint i = 0; i < n; ++i)
        REQUIRE(mz_zip_writer_add_from_zip_reader(&dst, &src, i));
    REQUIRE(mz_zip_writer_add_mem(&dst, entry_name.c_str(),
                                  content.data(), content.size(),
                                  MZ_DEFAULT_COMPRESSION));
    mz_zip_writer_finalize_archive(&dst);
    mz_zip_writer_end(&dst);
    mz_zip_reader_end(&src);

    fs_helper::remove(archive_path);
    fs_helper::rename(tmp, archive_path);
}

} // namespace bambu_cli_unit
