// No-op stubs for libslic3r symbols that bambu-cli does not need but the
// libslic3r static lib references (Http, BBL_Encrypt). Avoids dragging
// curl/openssl-ssl/crypt32 into the bambu-cli dependency surface.
// Do NOT compile src/slic3r/Utils/Http.cpp or src/slic3r/Utils/BBLUtil.cpp
// into bambu-cli — these stubs replace them.

#include <string>
#include <memory>

namespace Slic3r {

// --- Http ------------------------------------------------------------
class Http {
public:
    Http() = default;
    ~Http() = default;
    static Http get(const std::string&) { return Http(); }
    static Http post(const std::string&) { return Http(); }
    Http& size_limit(size_t)             { return *this; }
    Http& header(const std::string&, const std::string&) { return *this; }
    Http& form_add(const std::string&, const std::string&) { return *this; }
    Http& on_complete(...)               { return *this; }
    Http& on_error(...)                  { return *this; }
    Http& on_progress(...)               { return *this; }
    void perform()                       {}
    void perform_sync()                  {}
    bool tls_global_init()               { return true; }
};

// --- BBL_Encrypt -----------------------------------------------------
namespace BBL_Encrypt {
    std::string encrypt(const std::string& s) { return s; }
    std::string decrypt(const std::string& s) { return s; }
}

} // namespace Slic3r
