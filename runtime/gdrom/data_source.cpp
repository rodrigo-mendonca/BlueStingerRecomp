#include "gdrom/data_source.h"

#include "runtime_data.h"

#include <cstdio>

namespace bs {
namespace {

constexpr uint32_t kFadOffset = 150;  // FAD = LBA + 150 (como em image_source.cpp)

class DataSource final : public DiscSource {
public:
    DataSource(const std::filesystem::path& dir, const std::filesystem::path& mods) : disc_(dir, mods) {
        for (const auto& n : disc_.overridden()) std::fprintf(stderr, "[mods] arquivo substituido: %s\n", n.c_str());
        for (const auto& t : disc_.manifest().tracks) {
            DiscTrack d;
            d.number    = t.number;
            d.session   = t.session;
            d.start_fad = t.start_lba + kFadOffset;
            d.length    = t.length;
            d.ctrl      = t.data ? 4 : 0;
            tracks_.push_back(d);
        }
    }

    const std::vector<DiscTrack>& tracks() const override { return tracks_; }

    bool read(uint32_t fad, uint8_t* out) override {
        if (fad < kFadOffset) return false;
        return disc_.readSector(fad - kFadOffset, out);
    }

private:
    disc::RuntimeDisc      disc_;
    std::vector<DiscTrack> tracks_;
};

} // namespace

std::unique_ptr<DiscSource> open_runtime_data(const std::filesystem::path& dir, const std::filesystem::path& modAssets) {
    return std::make_unique<DataSource>(dir, modAssets);
}

} // namespace bs
