#include "gdrom/image_source.h"

#include "disc/cdi_image.h"

namespace bs {
namespace {

constexpr uint32_t kFadOffset = 150;  // FAD = LBA + 150 (2 s de pregap do lead-in)

class ImageSource final : public DiscSource {
public:
    explicit ImageSource(const std::filesystem::path& path) : img_(path) {
        for (const auto& t : img_.tracks()) {
            DiscTrack d;
            d.number    = uint32_t(t.number);
            d.session   = uint32_t(t.session);
            d.start_fad = t.start_lba + kFadOffset;
            d.length    = t.length;
            d.ctrl      = t.isData() ? 4 : 0;
            tracks_.push_back(d);
        }
    }

    const std::vector<DiscTrack>& tracks() const override { return tracks_; }

    bool read(uint32_t fad, uint8_t* out) override {
        if (fad < kFadOffset) return false;
        const uint32_t lba = fad - kFadOffset;
        const auto*    t   = img_.findTrack(lba);
        if (!t || !t->isData()) return false;
        img_.readUserData(lba, out);
        return true;
    }

private:
    disc::CdiImage         img_;
    std::vector<DiscTrack> tracks_;
};

} // namespace

std::unique_ptr<DiscSource> open_disc_image(const std::filesystem::path& path) {
    return std::make_unique<ImageSource>(path);
}

} // namespace bs
