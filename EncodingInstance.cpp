#include "EncodingInstance.h"
#include <stdexcept>
#include <cassert>
#include <chrono>
#include <thread>

using namespace TACTSharp;

// static empty
const EncodingResult EncodingInstance::Zero = EncodingResult{};

//If fileSize is -1 - it's size is take defacto
EncodingInstance::EncodingInstance(const std::string &filePath, int fileSize) : _filePath(filePath) {
    // open file
    m_file = std::make_shared<MemoryMappedFile>(filePath);

    _fileSize = fileSize == -1 ? m_file->size() : fileSize;

    // map view
    _view = static_cast<const uint8_t *>(m_file->data());
    if (!_view)
        throw std::runtime_error("MapViewOfFile failed");

    // read header
    uint8_t version = 0;
    ReadHeader(version, _schema);
    if (version != 1)
        throw std::runtime_error("Unsupported encoding version");
}

EncodingInstance::~EncodingInstance() {

}

void EncodingInstance::ReadHeader(uint8_t& version, EncodingSchema& schema) {
    if (_fileSize < 22) throw std::runtime_error("File too small");
    const uint8_t* h = _view;
    if (h[0] != 0x45 || h[1] != 0x4E)
        throw std::runtime_error("Invalid magic");

    version       = h[2];
    uint8_t hashC = h[3];
    uint8_t hashE = h[4];
    uint16_t cPgSzK = ReadUInt16BE(h + 5) * 1024;
    uint16_t ePgSzK = ReadUInt16BE(h + 7) * 1024;
    int32_t cPgCnt  = ReadInt32BE(h + 9);
    int32_t ePgCnt  = ReadInt32BE(h + 13);
    int32_t eSpecSz = ReadInt32BE(h + 18);

    size_t off = 22;
    Range rSpec{ off, off + eSpecSz };
    off = rSpec.end;
    Range cHdr{ off, off + size_t(cPgCnt) * (hashC + 0x10) };
    off = cHdr.end;
    Range cPages{ off, off + size_t(cPgSzK) * cPgCnt };
    off = cPages.end;
    Range eHdr{ off, off + size_t(ePgCnt) * (hashE + 0x10) };
    off = eHdr.end;
    Range ePages{ off, off + size_t(ePgSzK) * ePgCnt };

    schema = EncodingSchema{
        hashC, hashE, rSpec,
        TableSchema{cHdr, cPages, size_t(hashC)+0x10, size_t(cPgSzK)},
        TableSchema{eHdr, ePages, size_t(hashE)+0x10, size_t(ePgSzK)}
    };
}
EncodingResult EncodingInstance::FindContentKey(const std::array<uint8_t, 16>& cKeyTarget) const {
    return FindContentKey(cKeyTarget.data(), cKeyTarget.size());
}

EncodingResult EncodingInstance::FindContentKey(const std::vector<uint8_t>& cKeyTarget) const {
    return FindContentKey(cKeyTarget.data(), cKeyTarget.size());
}

EncodingResult EncodingInstance::FindContentKey(const uint8_t *keyPtr, int keyLength) const {
    auto [ptr, sz] = _schema.cEKey.ResolvePage(_view, _fileSize,
                                               keyPtr, keyLength);

    while (sz > 0) {
        uint8_t cnt = ptr[0];
        size_t recordLen = 1 + 5 + _schema.cKeySize + cnt * _schema.eKeySize;
        // record payload starts at ptr+1
        const uint8_t* rec = ptr + 1;
        // read fileSize
        uint64_t decSize = ReadUInt40BE(rec);
        const uint8_t* cKey = rec + 5;
        if (SequenceEqual(cKey, keyPtr, _schema.cKeySize)) {
            // copy encoding keys
            const uint8_t* eKeys = cKey + _schema.cKeySize;
            std::vector<uint8_t> keys(eKeys, eKeys + cnt*_schema.eKeySize);
            return EncodingResult{cnt, std::move(keys), decSize};
        }
        // advance to next record
        ptr += recordLen;
        sz  -= recordLen;
    }
    return EncodingInstance::Zero;
}

std::pair<std::string,uint64_t>
EncodingInstance::GetESpec(const std::vector<uint8_t>& target) {
    // lazy load specs
    {
        std::lock_guard<std::mutex> lk(_specsMutex);
        if (_encodingSpecs.empty()) {
            size_t off = _schema.encodingSpec.start;
            size_t remain = _schema.encodingSpec.end - off;
            while (remain) {
                auto s = ReadNullTermString(_view, remain, off);
                _encodingSpecs.push_back(s);
                off    += s.size() + 1;
                remain -= s.size() + 1;
            }
        }
    }

    auto [ptr, sz] = _schema.eKeySpec.ResolvePage(_view, _fileSize,
                                                  target.data(), target.size());
    while (sz >= _schema.eKeySize + 4 + 5) {
        if (SequenceEqual(ptr, target.data(), _schema.eKeySize)) {
            uint32_t idx = ReadInt32BE(ptr + _schema.eKeySize);
            uint64_t encSize = ReadUInt40BE(ptr + _schema.eKeySize + 4);
            return {_encodingSpecs.at(idx), encSize};
        }
        ptr += _schema.eKeySize + 4 + 5;
        sz  -= _schema.eKeySize + 4 + 5;
    }
    return {"",0};
}

// TableSchema implementation
std::pair<const uint8_t*, int64_t>
TableSchema::ResolvePage(const uint8_t* fileData, size_t fileSize,
                         const uint8_t* xKey, size_t keyLen) const
{
    size_t count = (header.end - header.start) / headerEntrySize;

    EntryIterator beginIt(fileData + header.start, headerEntrySize);
    EntryIterator endIt(fileData + header.end, headerEntrySize);

    auto it = std::lower_bound(beginIt, endIt, xKey,
        [keyLen](const uint8_t* rec, const uint8_t* needle) {
            return std::memcmp(rec, needle, keyLen) < 0;
        }
    );
    // If at the beginning, no lower entry
    if (it == endIt)
        return { nullptr, 0 };

    size_t index = std::distance(beginIt, --it);
    size_t pageOff = pages.start + index * pageSize;

    if (pageOff + pageSize > fileSize)
        return { nullptr, 0 };

    return { fileData + pageOff, pageSize };
}
