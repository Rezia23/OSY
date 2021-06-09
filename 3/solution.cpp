#ifndef __PROGTEST__

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <functional>

using namespace std;


/* Filesystem size: min 8MiB, max 1GiB
 * Filename length: min 1B, max 28B
 * Sector size: 512B
 * Max open files: 8 at a time
 * At most one filesystem mounted at a time.
 * Max file size: < 1GiB
 * Max files in the filesystem: 128
 */

#define FILENAME_LEN_MAX    28
#define DIR_ENTRIES_MAX     128
#define OPEN_FILES_MAX      8
#define SECTOR_SIZE         512
#define DEVICE_SIZE_MAX     ( 1024 * 1024 * 1024 )
#define DEVICE_SIZE_MIN     ( 8 * 1024 * 1024 )

struct TFile {
    char m_FileName[FILENAME_LEN_MAX + 1];
    size_t m_FileSize;
};

struct TBlkDev {
    size_t m_Sectors;
    function<size_t(size_t, void *, size_t)> m_Read;
    function<size_t(size_t, const void *, size_t)> m_Write;
};
#endif /* __PROGTEST__ */

class CFileSystem {
public:
    CFileSystem(TBlkDev & oldDev) : dev(move(oldDev)){}

    static bool CreateFs(const TBlkDev &dev);

    static CFileSystem *Mount(const TBlkDev &dev);

    bool Umount(void);

    size_t FileSize(const char *fileName);

    int OpenFile(const char *fileName, bool writeMode);

    bool CloseFile(int fd);

    size_t ReadFile(int fd, void *data, size_t len);

    size_t WriteFile(int fd, const void *data, size_t len);

    bool DeleteFile(const char *fileName);

    bool FindFirst(TFile &file);

    bool FindNext(TFile &file);

private:
    TBlkDev dev;
    static size_t maxSectors;
    static size_t numSectorsForMetadata;
    static void writeInfoData(const void * data, size_t len, const TBlkDev & dev, size_t numSectorsUsed){
        char * mem = new char [numSectorsUsed*SECTOR_SIZE];
        memcpy(mem, (char * )data, len);
        dev.m_Write(0, mem, numSectorsUsed);
    }
};

struct FileMetaData{
    char name [FILENAME_LEN_MAX+1];
    size_t size;
    size_t start;
    FileMetaData(){
        memset(name, 0, sizeof name);
        size = 0;
        start = 0;
    }
};

struct FATentry{
    int next;
    bool free = true;
};


struct FileSystemInfo{
    FileMetaData fileMetaData [DIR_ENTRIES_MAX];
    size_t firstFreeBlock;
    FATentry * FAT;
    FileSystemInfo(size_t numSectors){
        FAT = new FATentry [numSectors];
        for(int i = 0; i<numSectors;i++){
            FAT[i].next = i+1;
        }
        FAT[numSectors-1].next = EOF;
        firstFreeBlock = 0;
    }
    void useFirstNBlocks(size_t n){
        for(int i = 0; i<n;i++){
            FAT[i].next= EOF;
            FAT[i].free = false;
        }
        firstFreeBlock = n;
    }
};

bool CFileSystem::CreateFs(const TBlkDev &dev) {
    maxSectors = dev.m_Sectors;
    FileSystemInfo fsInfo(dev.m_Sectors);
    size_t len = sizeof(fsInfo);
    numSectorsForMetadata = len/SECTOR_SIZE;
    if(len%SECTOR_SIZE != 0){
        numSectorsForMetadata++;
    }
    fsInfo.useFirstNBlocks(numSectorsForMetadata);
    writeInfoData(&fsInfo, sizeof(fsInfo), dev, numSectorsForMetadata);
    return true;
}

CFileSystem *CFileSystem::Mount(const TBlkDev &dev) {
    TBlkDev copy = dev;
    return new CFileSystem(copy);
}


#ifndef __PROGTEST__

#include "simple_test.inc"

#endif /* __PROGTEST__ */
