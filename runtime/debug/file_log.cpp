#include "debug/file_log.h"

#include "pvr/holly_intc.h"
#include "util/hash.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sstream>

namespace bs {

bool g_file_log = false;

namespace {

struct State {
    std::mutex         mu;
    std::ostringstream out;
    std::string        path;
    uint64_t           seq = 0;
    bool               atexitDone = false;
};

State& st() {
    static State s;
    return s;
}

void atExit() { file_log_flush(); }

const bool kInit = [] {
    if (const char* p = std::getenv("BS_FILE_LOG"); p && *p) file_log_init(p);
    return true;
}();

} // namespace

void file_log_init(const std::string& path) {
    State& s = st();
    {
        std::lock_guard lk(s.mu);
        s.out.str({});
        s.seq  = 0;
        s.path = path;
        if (!path.empty() && !s.atexitDone) {
            std::atexit(atExit);
            s.atexitDone = true;
        }
    }
    g_file_log = !path.empty();
}

void file_log_chunk(const char* source, const std::string& logical, uint64_t offset, const uint8_t* data, size_t n) {
    const std::string h = util::sha256Hex(data, n);
    State&            s = st();
    std::lock_guard   lk(s.mu);
    s.out << s.seq++ << '\t' << holly_vblank_count() << '\t' << source << "\tdado\t" << logical << '\t' << offset << '\t' << n << '\t' << h << '\n';
}

void file_log_other(const char* source, const char* what, uint32_t where) {
    State&          s = st();
    std::lock_guard lk(s.mu);
    s.out << s.seq++ << '\t' << holly_vblank_count() << '\t' << source << "\toutro\t" << what << '\t' << where << "\t0\t-\n";
}

std::string file_log_dump() {
    State&          s = st();
    std::lock_guard lk(s.mu);
    return "seq\tvblank\torigem\ttipo\tarquivo\tdeslocamento\tbytes\tsha256\n" + s.out.str();
}

bool file_log_flush() {
    std::string path;
    {
        State&          s = st();
        std::lock_guard lk(s.mu);
        path = s.path;
    }
    if (path.empty()) return false;
    const std::string text = file_log_dump();
    FILE*             f    = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace bs
