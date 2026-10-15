#include "probe.h"

#include <sstream>
#include <stdexcept>
#include <vector>

namespace bs::hwscan {

namespace {

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> f;
    std::string              cur;
    for (char ch : line) {
        if (ch == '\t') {
            f.push_back(cur);
            cur.clear();
        } else if (ch != '\r') {
            cur += ch;
        }
    }
    f.push_back(cur);
    return f;
}

uint32_t parseHex(const std::string& s) {
    size_t        n = 0;
    unsigned long v = std::stoul(s, &n, 16);
    if (n != s.size() || v > 0xFFFFFFFFul) throw std::runtime_error("sonda: numero invalido '" + s + "'");
    return uint32_t(v);
}

} // namespace

ProbeStats merge_probe(Scan& s, const analysis::Image& img, const std::string& tsv) {
    ProbeStats         st;
    std::istringstream in(tsv);
    std::string        line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("tipo\t", 0) == 0) continue;
        const auto f = split(line);
        if (f.size() != 6 || f[0].size() != 1) throw std::runtime_error("sonda: linha malformada: " + line);
        const char     kind  = f[0][0];
        const uint32_t fn    = parseHex(f[1]);
        const uint32_t addr  = parseHex(f[2]);
        const uint64_t count = std::stoull(f[5]);
        if (kind == 'i') {
            const uint32_t callee = img.normalize(addr);
            if (!fn) {  // despacho feito pelo runtime (interrupcao, inicio de thread)
                ++st.skipped;
                continue;
            }
            s.edges.push_back({fn, callee ? callee : addr, 0, EdgeKind::Dynamic});
            ++st.edges;
            continue;
        }
        Kind k;
        if (kind == 'r') k = Kind::Read;
        else if (kind == 'w') k = Kind::Write;
        else if (kind == 'q') k = Kind::Pref;
        else throw std::runtime_error("sonda: tipo desconhecido em: " + line);
        dcmap::Region r = dcmap::classify(addr);
        // envio por store queue para a RAM: o dispositivo usado e a propria SQ
        if (k == Kind::Pref && !dcmap::is_hardware(r)) r = dcmap::Region::StoreQueue;
        if (!dcmap::is_hardware(r)) {
            ++st.skipped;
            continue;
        }
        HwRef ref;
        ref.function = fn;
        ref.address  = addr;
        ref.size     = uint8_t(std::stoul(f[3]));
        ref.kind     = k;
        ref.region   = r;
        ref.count    = count;
        ref.dynamic  = true;
        s.refs.push_back(ref);
        ++st.accesses;
    }
    return st;
}

} // namespace bs::hwscan
