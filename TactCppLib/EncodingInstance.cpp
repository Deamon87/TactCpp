#include "EncodingInstance.h"
#include <stdexcept>
#include <cassert>
#include <chrono>
#include <thread>

#include "utils/DataReader.h"

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
    // Must have at least the 22-byte fixed header
    if (_fileSize < 22)
        throw std::runtime_error("File too small");

    // Wrap the view in a bounds-checked reader
    DataReader reader(const_cast<uint8_t*>(_view), _fileSize);

    // --- magic bytes ---
    if (reader.ReadUInt8() != 0x45 || reader.ReadUInt8() != 0x4E)
        throw std::runtime_error("Invalid magic");

    // --- fixed fields ---
    version       = reader.ReadUInt8();              // h[2]
    uint8_t hashC = reader.ReadUInt8();              // h[3]
    uint8_t hashE = reader.ReadUInt8();              // h[4]
    uint16_t cPgSzK = reader.ReadUInt16BE() * 1024;  // h[5–6]
    uint16_t ePgSzK = reader.ReadUInt16BE() * 1024;  // h[7–8]
    int32_t cPgCnt  = reader.ReadInt32BE();          // h[9–12]
    int32_t ePgCnt  = reader.ReadInt32BE();          // h[13–16]

    // Skip the one reserved byte at h[17]
    reader.ReadUInt8();

    // Read the size of the spec section (h[18–21])
    int32_t eSpecSz = reader.ReadInt32BE();

    // --- compute ranges ---
    std::size_t off = reader.GetOffset();  // should be 22 now

    Range rSpec{ off, off + std::size_t(eSpecSz) };

    off = rSpec.end;
    Range cHdr{ off, off + std::size_t(cPgCnt) * (std::size_t(hashC) + 0x10) };

    off = cHdr.end;
    Range cPages{ off, off + std::size_t(cPgSzK) * std::size_t(cPgCnt) };

    off = cPages.end;
    Range eHdr{ off, off + std::size_t(ePgCnt) * (std::size_t(hashE) + 0x10) };

    off = eHdr.end;
    Range ePages{ off, off + std::size_t(ePgSzK) * std::size_t(ePgCnt) };

    // --- fill out the schema ---
    schema = EncodingSchema{
        hashC, hashE, rSpec,
        TableSchema{ cHdr,    cPages, std::size_t(hashC) + 0x10, std::size_t(cPgSzK) },
        TableSchema{ eHdr,    ePages, std::size_t(hashE) + 0x10, std::size_t(ePgSzK) }
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

    DataReader reader(const_cast<uint8_t*>(ptr), sz);

    while (reader.GetOffset() < sz) {
        const std::size_t recordStart = reader.GetOffset();

        // 1-byte count
        uint8_t cnt = reader.ReadUInt8();

        // 5-byte decrypted size
        uint64_t decSize = reader.ReadUInt40BE();

        // Compare cKey
        std::size_t cKeyOff = reader.GetOffset();
        if (SequenceEqual(ptr + cKeyOff, keyPtr, _schema.cKeySize)) {
            // Read eKeys
            std::size_t eKeysOff = cKeyOff + _schema.cKeySize;
            std::size_t eKeysLen = std::size_t(cnt) * _schema.eKeySize;
            assert(eKeysOff + eKeysLen <= sz);

            std::vector<uint8_t> keys(ptr + eKeysOff,
                                      ptr + eKeysOff + eKeysLen);
            return EncodingResult{ cnt, std::move(keys), decSize };
        }

        // Skip entire record
        std::size_t recordLen =
            1 /*cnt*/ + 5 /*decSize*/ +
            _schema.cKeySize +
            std::size_t(cnt) * _schema.eKeySize;
        reader.SetOffset(recordStart + recordLen);
    }

    return EncodingInstance::Zero;
}

std::pair<std::string, uint64_t>
EncodingInstance::GetESpec(const std::vector<uint8_t>& target) {
    // Lazy-load the spec strings (thread-safe)
    {
        std::lock_guard<std::mutex> lk(_specsMutex);
        if (_encodingSpecs.empty()) {
            const std::size_t start  = _schema.encodingSpec.start;
            const std::size_t end    = _schema.encodingSpec.end;
            const std::size_t length = end - start;

            // Wrap the spec region in a DataReader
            DataReader specReader(const_cast<uint8_t*>(_view + start), length);

            // Read all NUL-terminated strings until we exhaust the region
            while (specReader.GetOffset() < length) {
                _encodingSpecs.push_back(specReader.ReadNullTermString());
            }
        }
    }

    // Resolve the page containing eKey specifications
    auto [ptr, sz] = _schema.eKeySpec.ResolvePage(
        _view, _fileSize, target.data(), target.size());

    DataReader reader(const_cast<uint8_t*>(ptr), sz);

    // Each record is: [eKey (_schema.eKeySize bytes)] [idx (4-byte BE)] [encSize (5-byte BE)]
    const std::size_t recordSize = _schema.eKeySize + 4 + 5;

    // Scan through records
    while (reader.GetOffset() + recordSize <= sz) {
        const std::size_t recOff = reader.GetOffset();

        // Compare the eKey prefix
        if (SequenceEqual(ptr + recOff, target.data(), _schema.eKeySize)) {
            // Advance past the eKey
            reader.SetOffset(recOff + _schema.eKeySize);

            // Read the index and size
            uint32_t idx     = reader.ReadInt32BE();
            uint64_t encSize = reader.ReadUInt40BE();

            // Return the matching spec string and size
            return { _encodingSpecs.at(idx), encSize };
        }

        // No match: skip the entire record
        reader.SetOffset(recOff + recordSize);
    }

    // Not found
    return { "", 0 };
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
