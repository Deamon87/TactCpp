#include "GroupIndex.h"

#include <openssl/md5.h>
#include <algorithm>
#include <execution>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>

#include "IndexInstance.h"
#include "utils/stringUtils.h"

std::string md5(const uint8_t* data, uint32_t length) {
    static const uint32_t s[64] = {
         7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
         5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
         4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
         6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21
    };
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
        0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
        0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
        0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
    };
    auto LEFTROTATE = [](uint32_t x, uint32_t c) { return (x << c) | (x >> (32 - c)); };

    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xefcdab89;
    uint32_t h2 = 0x98badcfe;
    uint32_t h3 = 0x10325476;

    // Pre-processing
    size_t new_len = ((((length + 8) / 64) + 1) * 64) - 8;
    std::vector<uint8_t> msg(new_len + 8);
    std::memcpy(msg.data(), data, length);
    msg[length] = 0x80;
    uint64_t bits_len = uint64_t(length) * 8;
    std::memcpy(msg.data() + new_len, &bits_len, 8);

    for (size_t offset = 0; offset < new_len; offset += 64) {
        uint32_t w[16];
        for (int j = 0; j < 16; ++j) {
            std::memcpy(&w[j], msg.data() + offset + j*4, 4);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3;
        for (uint32_t i = 0; i < 64; ++i) {
            uint32_t f, g;
            if (i < 16) { f = (b & c) | ((~b) & d); g = i; }
            else if (i < 32) { f = (d & b) | ((~d) & c); g = (5*i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3*i + 5) % 16; }
            else { f = c ^ (b | (~d)); g = (7*i) % 16; }
            uint32_t temp = d;
            d = c;
            c = b;
            uint32_t sum = a + f + K[i] + w[g];
            b += LEFTROTATE(sum, s[i]);
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
    }

    char buf[33];
    std::snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x"
        "%02x%02x%02x%02x"
        "%02x%02x%02x%02x"
        "%02x%02x%02x%02x",
        h0 & 0xff, (h0 >> 8) & 0xff, (h0 >> 16) & 0xff, (h0 >> 24) & 0xff,
        h1 & 0xff, (h1 >> 8) & 0xff, (h1 >> 16) & 0xff, (h1 >> 24) & 0xff,
        h2 & 0xff, (h2 >> 8) & 0xff, (h2 >> 16) & 0xff, (h2 >> 24) & 0xff,
        h3 & 0xff, (h3 >> 8) & 0xff, (h3 >> 16) & 0xff, (h3 >> 24) & 0xff);
    return std::string(buf);
}


struct IndexFooter {
    uint8_t formatRevision;
    uint8_t flags0, flags1;
    uint8_t blockSizeKBytes;
    uint8_t offsetBytes;
    uint8_t sizeBytes;
    uint8_t keyBytes;
    uint8_t hashBytes;
    uint32_t numElements;
};

std::string GroupIndex::Generate(
    const std::shared_ptr<CDN> &cdn,
    const Settings& settings,
    const std::string& inputHash,
    const std::vector<std::string>& archives)
{
    std::string hash = inputHash;
    if (hash.empty()) {
        std::cout << "Generating group index for unknown group-index\n";
    } else {
        std::cout << "Generating group index for " << hash << "\n";
    }

    std::cout << "Loading " << archives.size() << " index files\n";

    // load each archive in parallel
    std::vector<std::future<void>> futures;
    for (size_t archiveIndex = 0; archiveIndex < archives.size(); ++archiveIndex) {
        futures.emplace_back(std::async(std::launch::async, [&, archiveIndex]() {
            const auto& name = archives[archiveIndex];
            std::string indexPath;

            // check local BaseDir
            if (settings.BaseDir.has_value()) {
                std::filesystem::path p = std::filesystem::path(settings.BaseDir.value()) /
                             "Data" / "indices" / (name + ".index");
                if (std::filesystem::exists(p)) {
                    indexPath = p.string();
                }
            }
            if (indexPath.empty()) {
                cdn->GetFile("data", name + ".index");
                indexPath = (std::filesystem::path(settings.CacheDir) / cdn->ProductDirectory() /
                    "data" / (name + ".index")).string();
            }

            IndexInstance idx(indexPath);
            auto all = idx.GetAllEntries();
            for (auto& tup : all) {
                auto& [eKey, offset, size, _unused] = tup;
                std::lock_guard lock(entryMutex);
                Entries.push_back({
                    .EKey = std::move(eKey),
                    .Size = static_cast<uint32_t>(size),
                    .ArchiveIndex = static_cast<uint16_t>(archiveIndex),
                    .Offset = static_cast<uint32_t>(offset)
                });
            }
        }));
    }
    for (auto& f : futures) f.get();

    std::cout << "Done loading index files, got " << Entries.size() << " entries\n";

    // sort by EKey
    std::cout << "Sorting entries by EKey\n";
    std::sort(std::execution::par, Entries.begin(), Entries.end(),
        [](auto const& a, auto const& b){
            return std::memcmp(a.EKey.data(), b.EKey.data(), a.EKey.size()) < 0;
        });
    std::cout << "Done sorting entries\n";

    // build footer metadata
    IndexFooter footer{
        .formatRevision = 1,
        .flags0         = 0,
        .flags1         = 0,
        .blockSizeKBytes= 4,
        .offsetBytes    = 6,
        .sizeBytes      = 4,
        .keyBytes       = 16,
        .hashBytes      = 8,
        .numElements    = static_cast<uint32_t>(Entries.size())
    };

    const size_t blockSizeBytes      = footer.blockSizeKBytes * 1024;
    const size_t entrySize           = footer.keyBytes + footer.sizeBytes + footer.offsetBytes;
    const size_t entriesPerBlock     = blockSizeBytes / entrySize;
    const size_t numBlocks           = (footer.numElements + entriesPerBlock - 1) / entriesPerBlock;
    const size_t totalSize =
        numBlocks * blockSizeBytes +
        numBlocks * (footer.keyBytes + footer.hashBytes) +
        /* footer overhead */ 28;

    // main buffer
    std::vector<uint8_t> buf(totalSize, 0);

    // helper to write little-endian
    auto write32 = [&](size_t pos, uint32_t v){
        v = __builtin_bswap32(v);
        std::memcpy(buf.data()+pos, &v, 4);
    };
    auto write16 = [&](size_t pos, uint16_t v){
        v = __builtin_bswap16(v);
        std::memcpy(buf.data()+pos, &v, 2);
    };

    size_t tocEkeysOff   = numBlocks * blockSizeBytes;
    size_t tocHashesOff  = tocEkeysOff + footer.keyBytes * numBlocks;

    // write blocks & per-block TOC entries
    for (size_t i = 0; i < numBlocks; ++i) {
        size_t blockStart = i * blockSizeBytes;
        size_t sliceStart = i * entriesPerBlock;
        size_t count      = std::min(entriesPerBlock, footer.numElements - sliceStart);

        // write each entry in block
        for (size_t j = 0; j < count; ++j) {
            auto const& e = Entries[sliceStart + j];
            size_t p = blockStart + j * entrySize;
            std::memcpy(buf.data()+p, e.EKey.data(), footer.keyBytes);
            write32(p + footer.keyBytes, e.Size);
            write16(p + footer.keyBytes + footer.sizeBytes, e.ArchiveIndex);
            write32(p + footer.keyBytes + footer.sizeBytes + 2, e.Offset);
        }

        // last EKey for this block
        auto const& lastKey = Entries[sliceStart + count - 1].EKey;
        std::memcpy(buf.data() + tocEkeysOff + i * footer.keyBytes,
                    lastKey.data(), footer.keyBytes);
        // leave the per-block hash zero for now
    }

    // write footer metadata (all but its own hash)
    size_t footerStart = totalSize - 28;
    uint8_t* F = buf.data() + footerStart;
    std::memset(F, 0, footer.hashBytes);                // toc_hash placeholder
    F[8]  = footer.formatRevision;
    F[9]  = footer.flags0;
    F[10] = footer.flags1;
    F[11] = footer.blockSizeKBytes;
    F[12] = footer.offsetBytes;
    F[13] = footer.sizeBytes;
    F[14] = footer.keyBytes;
    F[15] = footer.hashBytes;
    // numElements (big-endian)
    *(uint32_t*)(F+16) = footer.numElements;
    // note: it’s little-endian writing, adjust if needed
    // final 8 bytes of footer reserved for footerHash

    // compute each block’s MD5 and fill in tocHashes
    for (size_t i = 0; i < numBlocks; ++i) {
        size_t blockStart = i * blockSizeBytes;

        auto md = hexToBytes(md5(buf.data()+blockStart, blockSizeBytes));

        // copy first hashBytes
        std::memcpy(buf.data() + tocHashesOff + i * footer.hashBytes, md.data(), footer.hashBytes);
    }

    // compute TOC-hash (over ekeys+block-hashes)
    {
        size_t tocLen = totalSize - tocEkeysOff - 28;

        auto md = hexToBytes(md5(buf.data()+tocEkeysOff, tocLen));
        std::memcpy(buf.data() + footerStart, md.data(), footer.hashBytes);
    }

    // compute footer-hash (next 8 bytes)
    {
        auto md = hexToBytes(md5(buf.data()+footerStart, 20));

        std::memcpy(buf.data() + footerStart + 20, md.data(), footer.hashBytes);
    }

    // compute full-footer (filename) MD5
    std::string fullFooterHash = md5(buf.data()+footerStart, 28);

    // write out file
    std::filesystem::path outDir = std::filesystem::path(settings.CacheDir) /
                      cdn->ProductDirectory() / "data";
    std::filesystem::create_directories(outDir);
    const std::string fname = (hash.empty() ? fullFooterHash : hash) + ".index";
    if (!hash.empty() && fullFooterHash != hash) {
        throw std::runtime_error("Footer MD5 mismatch: expected " + hash +
                                 ", got " + fullFooterHash);
    }
    std::ofstream out(outDir / fname, std::ios::binary);
    out.write(reinterpret_cast<char*>(buf.data()), buf.size());
    out.close();

    return hash.empty() ? fullFooterHash : hash;
}
