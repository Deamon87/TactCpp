#ifndef ENCODINGINSTANCE_H
#define ENCODINGINSTANCE_H

#include "utils/BinaryUtils.h"
#include <string>
#include <vector>
#include <mutex>
#include <stdexcept>
#include <windows.h>

namespace TACTSharp {

struct Range { size_t start, end; };

struct TableSchema {
    Range header;      // byte range of the header entries
    Range pages;       // byte range of the pages
    size_t headerEntrySize;
    size_t pageSize;

    // fileData: pointer to entire file bytes
    // fileSize: total file size
    // xKey: pointer to the key bytes to lookup
    // keyLen: length of xKey
    // returns a pointer+length pair for the resolved page
    std::pair<const uint8_t*, size_t>
    ResolvePage(const uint8_t* fileData, size_t fileSize,
                const uint8_t* xKey, size_t keyLen) const;
};

struct EncodingSchema {
    uint8_t cKeySize, eKeySize;
    Range encodingSpec;
    TableSchema cEKey, eKeySpec;
};

struct EncodingResult {
    uint8_t keyCount;
    std::vector<uint8_t> keys;
    uint64_t decodedFileSize;

    // default empty
    EncodingResult(): keyCount(0), decodedFileSize(0) {}
    EncodingResult(uint8_t kc, std::vector<uint8_t>&& k, uint64_t sz)
      : keyCount(kc), keys(std::move(k)), decodedFileSize(sz) {}

    bool empty() const { return keyCount == 0; }
    // access i-th key slice
    std::vector<uint8_t> key(size_t i) const {
        if (i >= keyCount) throw std::out_of_range("Key index");
        size_t len = keys.size() / keyCount;
        return std::vector<uint8_t>(
            keys.begin() + i * len,
            keys.begin() + (i+1) * len);
    }
};

class EncodingInstance {
public:
    static const EncodingResult Zero;

    EncodingInstance(const std::wstring& filePath);
    ~EncodingInstance();

    // lookup cKey -> (count, encKeys, decodedSize)
    EncodingResult FindContentKey(const std::vector<uint8_t>& cKeyTarget) const;

    // lookup eKey -> (eSpec string, encodedFileSize)
    std::pair<std::string, uint64_t>
    GetESpec(const std::vector<uint8_t>& eKeyTarget);

private:
    void ReadHeader(uint8_t& version, EncodingSchema& schema);

    std::wstring          _filePath;
    size_t                _fileSize;
    HANDLE                _hFile        = INVALID_HANDLE_VALUE;
    HANDLE                _hMapping     = nullptr;
    const uint8_t*        _view         = nullptr;

    EncodingSchema        _schema;
    mutable std::vector<std::string> _encodingSpecs;
    mutable std::mutex    _specsMutex;
};
}


#endif //ENCODINGINSTANCE_H
