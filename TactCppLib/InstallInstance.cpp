#include "InstallInstance.h"
#include <stdexcept>
#include <cstring>

#include "utils/BinaryUtils.h"
#include "utils/DataReader.h"

InstallInstance::InstallInstance(const std::string& path)
    : mmf_(path, /*write=*/false)
{
    if (!mmf_.isOpen())
        throw std::runtime_error("Cannot open memory-mapped file: " + path);

    auto* base = static_cast<uint8_t*>(mmf_.data());
    const size_t bufLen = mmf_.size();
    if (!base || bufLen < 10)
        throw std::runtime_error("File too small or invalid mapping");

    DataReader dr(base, bufLen);

    // magic check: 'I','N'
    if (dr.ReadUInt8() != 0x49 || dr.ReadUInt8() != 0x4E)
        throw std::runtime_error("Invalid Install file magic");

    // skip version byte at offset 2
    dr.ReadUInt8();

    // header fields
    HashSize_   = dr.ReadUInt8();
    NumTags_    = dr.ReadUInt16BE();
    NumEntries_ = dr.ReadUInt32BE();

    const size_t bytesPerTag = (NumEntries_ + 7) / 8;

    // parse tag entries
    Tags_.reserve(NumTags_);
    for (uint16_t i = 0; i < NumTags_; ++i) {

        std::string name = dr.ReadNullTermString();
        uint16_t type = dr.ReadUInt16BE();

        // raw bitmap
        std::vector<uint8_t> raw = dr.ReadUint8Array(bytesPerTag);

        // unpack bits
        for (size_t j = 0; j < bytesPerTag; ++j) {
            uint64_t x = raw[j];
            raw[j] = static_cast<uint8_t>((x * 0x0202020202ULL & 0x010884422010ULL) % 1023);
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
        // filename
        std::string name = dr.ReadNullTermString();
        for (auto& c : name) if (c == '/') c = '\\';

        // content hash
        std::vector<uint8_t> contentHash = dr.ReadUint8Array(HashSize_);

        uint32_t sz = dr.ReadUInt32BE();

        // collect tags for this entry
        std::vector<std::string> entryTags;
        entryTags.reserve(NumTags_);
        for (uint16_t t = 0; t < NumTags_; ++t) {
            if (Tags_[t].files[i]) {
                entryTags.emplace_back(
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
