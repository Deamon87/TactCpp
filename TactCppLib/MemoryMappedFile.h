#ifndef MEMORYMAPPEDFILE_H
#define MEMORYMAPPEDFILE_H

#include <string>
#include <stdexcept>

#ifdef _WIN32
  #include <Windows.h>
#else
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #include <unistd.h>
  #include <cstring>
  #include <cerrno>
#endif

class MemoryMappedFile {
public:
    MemoryMappedFile(const std::string& filename,
                     bool write = false,
                     size_t length = 0);
    ~MemoryMappedFile();

    void*   data() const;
    size_t  size() const;
    bool    isOpen() const;
    void    close();

private:
    std::string filename_;
    size_t      size_;
    void*       data_;

#ifdef _WIN32
    HANDLE      fileHandle_;
    HANDLE      mappingHandle_;
#else
    int         fd_;
#endif
};


#endif //MEMORYMAPPEDFILE_H
