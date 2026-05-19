// Standalone STL generator for bambu-cli test fixtures.
// Build manually: cl /EHsc gen_fixtures.cpp /Fe:gen_fixtures.exe
// Then run: gen_fixtures.exe <out_dir>
// Commits the generated cube.stl, cylinder.stl, cone.stl alongside this source.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

struct Vec3 { float x, y, z; };
static Vec3 sub(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
static Vec3 normalize(Vec3 v) {
    float n = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    if (n < 1e-9f) return {1.0f, 0.0f, 0.0f};   // G7: never zero
    return {v.x/n, v.y/n, v.z/n};
}

struct Tri { Vec3 a, b, c; };

static void write_stl(const std::string& path, const std::vector<Tri>& tris) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "open %s failed\n", path.c_str()); std::exit(1); }
    char header[80] = "bambu-cli fixture";
    std::fwrite(header, 1, 80, f);
    uint32_t count = static_cast<uint32_t>(tris.size());
    std::fwrite(&count, 4, 1, f);
    for (const auto& t : tris) {
        Vec3 n = normalize(cross(sub(t.b, t.a), sub(t.c, t.a)));
        std::fwrite(&n, 12, 1, f);
        std::fwrite(&t.a, 12, 1, f);
        std::fwrite(&t.b, 12, 1, f);
        std::fwrite(&t.c, 12, 1, f);
        uint16_t attr = 0;
        std::fwrite(&attr, 2, 1, f);
    }
    std::fclose(f);
}

static std::vector<Tri> make_cube(float s) {
    // 10mm cube centered at origin, 12 triangles
    float h = s * 0.5f;
    Vec3 p[8] = {
        {-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h},
        {-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h}};
    auto q = [&](int a, int b, int c, int d) -> std::vector<Tri> {
        return {{p[a], p[b], p[c]}, {p[a], p[c], p[d]}};
    };
    std::vector<Tri> out;
    for (auto& v : q(0,1,2,3)) out.push_back(v);   // bottom
    for (auto& v : q(4,7,6,5)) out.push_back(v);   // top
    for (auto& v : q(0,4,5,1)) out.push_back(v);   // front
    for (auto& v : q(2,6,7,3)) out.push_back(v);   // back
    for (auto& v : q(0,3,7,4)) out.push_back(v);   // left
    for (auto& v : q(1,5,6,2)) out.push_back(v);   // right
    return out;
}

static std::vector<Tri> make_cylinder(float r, float h, int seg) {
    std::vector<Tri> out;
    for (int i = 0; i < seg; ++i) {
        float a0 = 2.0f * 3.14159265f * i / seg;
        float a1 = 2.0f * 3.14159265f * (i+1) / seg;
        Vec3 b0 = {r*std::cos(a0), r*std::sin(a0), -h*0.5f};
        Vec3 b1 = {r*std::cos(a1), r*std::sin(a1), -h*0.5f};
        Vec3 t0 = {b0.x, b0.y,  h*0.5f};
        Vec3 t1 = {b1.x, b1.y,  h*0.5f};
        Vec3 cb = {0,0,-h*0.5f}, ct = {0,0,h*0.5f};
        out.push_back({cb, b1, b0});            // bottom cap
        out.push_back({ct, t0, t1});            // top cap
        out.push_back({b0, b1, t1});            // side
        out.push_back({b0, t1, t0});            // side
    }
    return out;
}

static std::vector<Tri> make_cone(float r, float h, int seg) {
    std::vector<Tri> out;
    Vec3 apex = {0, 0, h*0.5f};
    Vec3 cb   = {0, 0, -h*0.5f};
    for (int i = 0; i < seg; ++i) {
        float a0 = 2.0f * 3.14159265f * i / seg;
        float a1 = 2.0f * 3.14159265f * (i+1) / seg;
        Vec3 b0 = {r*std::cos(a0), r*std::sin(a0), -h*0.5f};
        Vec3 b1 = {r*std::cos(a1), r*std::sin(a1), -h*0.5f};
        out.push_back({cb, b1, b0});
        out.push_back({b0, b1, apex});
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: %s <out_dir>\n", argv[0]); return 1; }
    std::string dir = argv[1];
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir.push_back('\\');
    write_stl(dir + "cube.stl",     make_cube(10.0f));
    write_stl(dir + "cylinder.stl", make_cylinder(5.0f, 10.0f, 32));
    write_stl(dir + "cone.stl",     make_cone(5.0f, 10.0f, 32));
    std::printf("Wrote 3 STLs to %s\n", dir.c_str());
    return 0;
}
