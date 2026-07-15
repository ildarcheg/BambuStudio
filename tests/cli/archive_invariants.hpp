#pragma once

#include <string>

namespace bambu_cli_test {

// Archive-level invariant assertions for produced .3mf files.
// Each function is a side-effecting Catch2 assertion: on failure it
// calls FAIL() with a contextual message. Callers do not need to wrap
// in REQUIRE.

// (a) Every <Relationship Target="..."/> in any *.rels file resolves to
//     an entry in the archive. Mirrors the runtime guard's check (a).
void assert_relationships_resolve(const std::string& zip_path);

// (b) Per-plate thumbnails Metadata/plate_N.png + plate_N_small.png exist
//     and decode as 128x128 PNGs (IHDR width/height fields).
void assert_plate_thumbnails_128(const std::string& zip_path);

// printable_area in Metadata/project_settings.config is a JSON array of
// exactly 4 string entries (rectangular bed corners). Closes a spec
// coverage gap: this is the Bug-A-class regression catch and was missing
// from the prior suite.
void assert_printable_area_4_points(const std::string& zip_path);

// Every <part> in Metadata/model_settings.config carries
// <metadata key="source_file" .../>. Bug-B-class regression catch.
void assert_parts_have_source_file(const std::string& zip_path);

// The <object> block named obj_name in Metadata/model_settings.config
// carries <metadata key="extruder" value="<slot>"/>.
void assert_object_extruder(const std::string& zip_path,
                            const std::string& obj_name, int slot);

// Composer: (a) + (b) + printable_area_4_points. Run on every e2e that
// produces a .3mf as a baseline regression net.
void run_all_basic(const std::string& zip_path);

} // namespace bambu_cli_test
