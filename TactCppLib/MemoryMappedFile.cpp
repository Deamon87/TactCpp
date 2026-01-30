#include "MemoryMappedFile.h"
#include <iostream>
MemoryMappedFile::MemoryMappedFile(const std::string& filename,
                                   bool write,
                                   size_t length)
  : filename_(filename)
  , size_(0)
  , data_(nullptr)
#ifdef _WIN32
  , fileHandle_(INVALID_HANDLE_VALUE)
  , mappingHandle_(NULL)
#else
  , fd_(-1)
#endif
{
#ifdef _WIN32
    DWORD access   = write ? (GENERIC_READ|GENERIC_WRITE) : GENERIC_READ;
    DWORD share    = FILE_SHARE_READ;
    DWORD creation = write ? OPEN_ALWAYS : OPEN_EXISTING;
    fileHandle_ = CreateFileA(
        filename.c_str(), access, share,
        nullptr, creation,
        FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (fileHandle_ == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Failed to open file: " + filename_);

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(fileHandle_, &fileSize))
        throw std::runtime_error("Failed to get file size: " + filename_);

    size_ = static_cast<size_t>(fileSize.QuadPart);
    if (write && length > 0 && size_ < length) {
        fileSize.QuadPart = length;
        SetFilePointerEx(fileHandle_, fileSize, nullptr, FILE_BEGIN);
        SetEndOfFile(fileHandle_);
        size_ = length;
    }

    DWORD protect    = write ? PAGE_READWRITE : PAGE_READONLY;
    mappingHandle_ = CreateFileMappingA(fileHandle_, nullptr, protect, 0, 0, nullptr);
    if (!mappingHandle_)
        throw std::runtime_error("Failed to create file mapping: " + filename_);

    DWORD viewAccess = write ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ;
    data_ = MapViewOfFile(mappingHandle_, viewAccess, 0, 0, 0);
    if (!data_)
        throw std::runtime_error("Failed to map view of file: " + filename_);

#else
    int flags = write ? (O_RDWR|O_CREAT) : O_RDONLY;
    mode_t mode = write ? (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) : 0;
    fd_ = ::open(filename.c_str(), flags, mode);
    if (fd_ < 0)
        throw std::runtime_error("open failed: " + std::string(std::strerror(errno)));

    struct stat st;
    if (fstat(fd_, &st) < 0)
        throw std::runtime_error("fstat failed: " + std::string(std::strerror(errno)));

    size_ = static_cast<size_t>(st.st_size);
    if (write && length > 0 && size_ < length) {
        if (ftruncate(fd_, length) < 0)
            throw std::runtime_error("ftruncate failed: " + std::string(std::strerror(errno)));
        size_ = length;
    }

    int prot     = PROT_READ | (write ? PROT_WRITE : 0);
    data_ = mmap(nullptr, size_, prot, MAP_SHARED, fd_, 0);
    if (data_ == MAP_FAILED)
        throw std::runtime_error("mmap failed: " + std::string(std::strerror(errno)));
#endif
}

MemoryMappedFile::~MemoryMappedFile() {
    close();
//    std::cout << "MemoryMappedFile destroyed" << std::endl;
}

void* MemoryMappedFile::data() const {
    return data_;
}

size_t MemoryMappedFile::size() const {
    return size_;
}

bool MemoryMappedFile::isOpen() const {
#ifdef _WIN32
    return data_ != nullptr && 
           mappingHandle_ != NULL && 
           fileHandle_ != INVALID_HANDLE_VALUE;
#else
    return data_ != MAP_FAILED && data_ != nullptr && fd_ >= 0;
#endif
}

void MemoryMappedFile::close() {
    // Early exit if already closed - check individual components since
    // isOpen() might return false even if some resources need cleanup
    bool anythingOpen = false;
#ifdef _WIN32
    anythingOpen = (data_ != nullptr) || 
                   (mappingHandle_ != NULL) || 
                   (fileHandle_ != INVALID_HANDLE_VALUE);
#else
    anythingOpen = (data_ != nullptr && data_ != MAP_FAILED) || (fd_ >= 0);
#endif
    if (!anythingOpen) return;

#ifdef _WIN32
    // Close in reverse order of creation: view -> mapping -> file
    if (data_) {
        BOOL result = UnmapViewOfFile(data_);
        // Always nullify pointer even if unmap fails to prevent use-after-free
        data_ = nullptr;
        if (!result) {
            // Log error but continue cleanup
            // In production, consider logging: GetLastError()
            std::cout << "UnmapViewOfFile returned error" << std::endl;
        }
    }
    if (mappingHandle_ && mappingHandle_ != NULL) {
        BOOL result = CloseHandle(mappingHandle_);
        mappingHandle_ = NULL;
        if (!result) {
            // Log error but continue cleanup
            std::cout << "CloseHandle returned error at " << __LINE__ << std::endl;
        }
    }
    if (fileHandle_ && fileHandle_ != INVALID_HANDLE_VALUE) {
        BOOL result = CloseHandle(fileHandle_);
        fileHandle_ = INVALID_HANDLE_VALUE;
        if (!result) {
            // Log error but continue cleanup

            std::cout << "CloseHandle returned error at " << __LINE__ << std::endl;
        }
    }
#else
    if (data_ && data_ != MAP_FAILED) {
        int result = munmap(data_, size_);
        data_ = nullptr;
        if (result != 0) {
            // Log error but continue cleanup
        }
    }
    if (fd_ >= 0) {
        int result = ::close(fd_);
        fd_ = -1;
        if (result != 0) {
            // Log error but continue cleanup
        }
    }
#endif
    size_ = 0;
}
