// No-op stubs for libslic3r symbols that bambu-cli does not need but the
// libslic3r static lib references (Http, BBL_Encrypt). Avoids dragging
// curl/openssl-ssl/crypt32 into the bambu-cli dependency surface.
// Do NOT compile src/slic3r/Utils/Http.cpp or src/slic3r/Utils/BBLUtil.cpp
// into bambu-cli — these stubs replace them.
//
// Important: the stubs must match the EXACT signatures from Http.hpp and
// BBLUtil.hpp. We include the real Http.hpp to match the class layout.
// For BBL_Encrypt, we provide direct symbol stubs without the header to
// avoid pulling in nlohmann/json.hpp through BBLUtil.hpp.

#include "slic3r/Utils/Http.hpp"

#include <functional>
#include <map>
#include <string>
#include <memory>

namespace Slic3r {

// --- Http stubs (matching Http.hpp signatures) --------------------------
// Stub bodies for all Http member functions referenced at link time.
// These are no-ops — bambu-cli never makes network requests.

struct Http::priv {};  // minimal pimpl definition

Http::Http(Http &&) {}
Http::~Http() {}
Http Http::get(std::string) { return Http(""); }
Http Http::post(std::string) { return Http(""); }
Http Http::put(std::string) { return Http(""); }
Http Http::del(std::string) { return Http(""); }
Http Http::put2(std::string) { return Http(""); }
Http Http::patch(std::string) { return Http(""); }

void Http::set_extra_headers(std::map<std::string, std::string>) {}
std::map<std::string, std::string> Http::get_extra_headers() { return {}; }

Http& Http::timeout_connect(long) { return *this; }
Http& Http::timeout_max(long)     { return *this; }
Http& Http::size_limit(size_t)    { return *this; }
Http& Http::header(std::string, const std::string&) { return *this; }
Http& Http::remove_header(std::string) { return *this; }
Http& Http::auth_digest(const std::string&, const std::string&) { return *this; }
Http& Http::auth_basic(const std::string&, const std::string&)  { return *this; }
Http& Http::ca_file(const std::string&) { return *this; }
Http& Http::form_add(const std::string&, const std::string&) { return *this; }
Http& Http::form_add_file(const std::string&, const boost::filesystem::path&) { return *this; }
Http& Http::mime_form_add_text(std::string&, std::string&) { return *this; }
Http& Http::mime_form_add_file(std::string&, const char*) { return *this; }
Http& Http::form_add_file(const std::wstring&, const boost::filesystem::path&) { return *this; }
Http& Http::form_add_file(const std::string&, const boost::filesystem::path&, const std::string&) { return *this; }
Http& Http::set_post_body(const boost::filesystem::path&) { return *this; }
Http& Http::set_post_body(const std::string&) { return *this; }
Http& Http::set_put_body(const boost::filesystem::path&) { return *this; }
Http& Http::set_del_body(const std::string&) { return *this; }
Http& Http::on_complete(CompleteFn)           { return *this; }
Http& Http::on_error(ErrorFn)                 { return *this; }
Http& Http::on_progress(ProgressFn)           { return *this; }
Http& Http::on_ip_resolve(IPResolveFn)        { return *this; }
Http& Http::on_header_callback(HeaderCallbackFn) { return *this; }

#ifdef WIN32
Http& Http::ssl_revoke_best_effort(bool) { return *this; }
#endif

Http::Ptr Http::perform()     { return nullptr; }
void      Http::perform_sync() {}
void      Http::cancel()       {}

bool        Http::ca_file_supported() { return false; }
std::string Http::tls_global_init()   { return {}; }
std::string Http::tls_system_cert_store() { return {}; }
std::string Http::url_encode(const std::string& s) { return s; }
std::string Http::url_decode(const std::string& s) { return s; }
std::string Http::get_filename_from_url(const std::string&) { return {}; }

// Private constructor (declaration in Http.hpp)
Http::Http(const std::string&) {}

// --- BBL_Encrypt stubs --------------------------------------------------
// Provided without including BBLUtil.hpp to avoid nlohmann/json dependency.
// Signatures match BBLUtil.hpp exactly.

class BBL_Encrypt {
public:
    static bool AESEncrypt(unsigned char*, unsigned, unsigned char*, unsigned&, const std::string&);
    static bool AESDecrypt(unsigned char*, unsigned, unsigned char*, unsigned&, const std::string&);
    static bool AES256CBC_Encrypt(unsigned char*, unsigned, unsigned char*, unsigned&, const std::string&, const std::string&);
    static bool AES256CBC_Decrypt(unsigned char*, unsigned, unsigned char*, unsigned&, const std::string&, const std::string&);
};

bool BBL_Encrypt::AESEncrypt(unsigned char*, unsigned, unsigned char*, unsigned& out_len, const std::string&) {
    out_len = 0; return false;
}
bool BBL_Encrypt::AESDecrypt(unsigned char*, unsigned, unsigned char*, unsigned& out_len, const std::string&) {
    out_len = 0; return false;
}
bool BBL_Encrypt::AES256CBC_Encrypt(unsigned char*, unsigned, unsigned char*, unsigned& out_len,
                                     const std::string&, const std::string&) {
    out_len = 0; return false;
}
bool BBL_Encrypt::AES256CBC_Decrypt(unsigned char*, unsigned, unsigned char*, unsigned& out_len,
                                     const std::string&, const std::string&) {
    out_len = 0; return false;
}

} // namespace Slic3r
