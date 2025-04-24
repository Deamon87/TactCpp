#include "InstallInstance.h"
#include <stdexcept>
#include <cstring>

#include "utils/BinaryUtils.h"

InstallInstance::InstallInstance(const std::string& path)
    : mmf_(path, /*write=*/false)
{
    if (!mmf_.isOpen())
        throw std::runtime_error("Cannot open memory-mapped file: " + path);

    const uint8_t* buf = reinterpret_cast<const uint8_t*>(mmf_.data());
    size_t bufLen      = mmf_.size();
    if (!buf || bufLen < 10)
        throw std::runtime_error("File too small or invalid mapping");

    // magic check: 'I','N'
    if (buf[0] != 0x49 || buf[1] != 0x4E)
        throw std::runtime_error("Invalid Install file magic");

    HashSize_   = buf[3];
    NumTags_    = ReadUInt16BE(buf + 4);
    NumEntries_ = ReadUInt32BE(buf + 6);

    size_t bytesPerTag = (NumEntries_ + 7) / 8;
    size_t offs = 10;

    // parse tag entries
    Tags_.reserve(NumTags_);
    for (uint16_t i = 0; i < NumTags_; ++i) {
        auto name = ReadNullTermString(buf, bufLen, offs);
        offs += name.size() + 1;

        uint16_t type = ReadUInt16BE(buf + offs);
        offs += 2;

        std::vector<uint8_t> raw(bytesPerTag);
        std::memcpy(raw.data(), buf + offs, bytesPerTag);
        offs += bytesPerTag;

        for (size_t j = 0; j < bytesPerTag; ++j) {
            uint64_t x = raw[j];
            raw[j] = static_cast<uint8_t>(
                (x * 0x0202020202ULL & 0x010884422010ULL) % 1023
            );
        }

        std::vector<bool> bits;
        bits.reserve(bytesPerTag * 8);
        for (uint8_t b : raw) {
            for (int bit = 0; bit < 8; ++bit)
                bits.push_back((b >> bit) & 1);
        }

        Tags_.push_back({ name, type, std::move(bits) });
    }

    // parse file entries
    Entries_.reserve(NumEntries_);
    for (uint32_t i = 0; i < NumEntries_; ++i) {
        auto name = ReadNullTermString(buf, bufLen, offs);
        for (auto& c : name) if (c == '/') c = '\\';
        offs += name.size() + 1;

        std::vector<uint8_t> contentHash(HashSize_);
        std::memcpy(contentHash.data(), buf + offs, HashSize_);
        offs += HashSize_;

        uint32_t sz = ReadUInt32BE(buf + offs);
        offs += 4;

        std::vector<std::string> entryTags;
        entryTags.reserve(NumTags_);
        for (uint16_t t = 0; t < NumTags_; ++t) {
            if (Tags_[t].files[i]) {
                entryTags.push_back(
                    std::to_string(Tags_[t].type) + "=" + Tags_[t].name
                );
            }
        }

        Entries_.push_back({ name, std::move(contentHash), sz, std::move(entryTags) });
    }
}

const std::vector<InstallTagEntry>& InstallInstance::getTags() const {
    return Tags_;
}

const std::vector<InstallFileEntry>& InstallInstance::getEntries() const {
    return Entries_;
}
