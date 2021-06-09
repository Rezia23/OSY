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



struct FileMetaData{
    char name [FILENAME_LEN_MAX+1];
    size_t size;
    size_t start;
    bool valid = false;
    FileMetaData(){
        memset(name, 0, sizeof name);
        size = 0;
        start = 0;
    }
    FileMetaData(const char * fn, size_t _size, size_t _start){
        strcpy(name, fn);
        size = _size;
        start = _start;
    }
};

struct FATentry{
    int next = EOF;
    bool free = true;
    FATentry() = default;
    FATentry(int n, bool f){
        next = n;
        free = f;
    }
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
struct openFileEntry{
    bool isValid = false;
    char name [FILENAME_LEN_MAX+1];
    bool writeMode;
    int offset = 0;
};
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
    static int sizeOfMetaData;
    TBlkDev dev;
    static size_t maxSectors;
    static size_t numSectorsForMetadata;
    openFileEntry openFiles[OPEN_FILES_MAX];
    size_t filesOpened = 0;
    static void writeInfoData(const void * data, size_t len, const TBlkDev & dev, size_t numSectorsUsed){
        char * mem = new char [numSectorsUsed*SECTOR_SIZE];
        memcpy(mem, (char * )data, len);
        dev.m_Write(0, mem, numSectorsUsed);
    }
    int openExisting(const char * fileName, bool writeMode);
    int findFile(const char *fileName) const;
    void createFile(const char *fileName);
    void truncateFile(const char *fileName, int fileIndex);
    void pointFATStoEOF(size_t first, char * buffer);

    int getFATentryOffset(size_t index){
        return (DIR_ENTRIES_MAX * sizeof(FileMetaData)) + sizeof(size_t) + (sizeof(FATentry) *index);
    }
    int getFirstFreeBlockIndexOffset(){
        return (DIR_ENTRIES_MAX * sizeof(FileMetaData));
    }
    int getFileMetaDataOffset(int index){
        return index * sizeof(FileMetaData);
    }
};



int CFileSystem::findFile(const char *fileName) const {
    char * buffer = new char [numSectorsForMetadata * SECTOR_SIZE];
    dev.m_Read(0, buffer, numSectorsForMetadata);
    for(int i = 0; i<DIR_ENTRIES_MAX;i++){
        FileMetaData fmd;
        memcpy(&fmd, buffer + i* sizeof(FileMetaData),sizeof(FileMetaData));
        if(fmd.valid && strcmp(fmd.name, fileName) == 0){
            return i;
        }
    }
    return -1;
}

void CFileSystem::createFile(const char *fileName) {
    char * buffer = new char [numSectorsForMetadata * SECTOR_SIZE];
    dev.m_Read(0, buffer, numSectorsForMetadata);
    //find free sector, put EOF there
    size_t firstFreeBlock;
    memcpy(&firstFreeBlock, buffer + getFirstFreeBlockIndexOffset(), sizeof(size_t) );

    //write EOF to used sector in FAT
    FATentry fe(EOF, false);
    memcpy(buffer+ getFATentryOffset(firstFreeBlock), &fe, sizeof(fe) );
    //get another free block and store it to var
    size_t nextFreeBlock = firstFreeBlock+1;
    while(nextFreeBlock != firstFreeBlock){     //might be possibly broken if no blocks are free
        FATentry nextEntry;
        memcpy(&nextEntry, buffer + getFATentryOffset(nextFreeBlock), sizeof(nextEntry));
        if(nextEntry.free){
            memcpy(buffer + getFirstFreeBlockIndexOffset(), &nextFreeBlock, sizeof(size_t));
            break;
        } else{
            nextFreeBlock++;
            nextFreeBlock%maxSectors;
        }
    }
    //put entry to FileEntry array
    for(int i = 0; i<DIR_ENTRIES_MAX;i++){
        FileMetaData fmd;
        memcpy(&fmd, buffer + i* sizeof(FileMetaData),sizeof(FileMetaData));
        if(!fmd.valid){
            FileMetaData newFmd (fileName, 0, firstFreeBlock) ;
            memcpy(buffer + i* sizeof(FileMetaData), &newFmd, sizeof(newFmd));
            break;
        }
    }

}


void CFileSystem::truncateFile(const char *fileName, int fileIndex) {
    char * buffer = new char [numSectorsForMetadata * SECTOR_SIZE];
    dev.m_Read(0, buffer, numSectorsForMetadata);
    FileMetaData fmd;
    memcpy(&fmd, buffer + getFileMetaDataOffset(fileIndex), sizeof(fmd));
    pointFATStoEOF(fmd.start, buffer);
    fmd.size = 0;
    memcpy(buffer + getFileMetaDataOffset(fileIndex),&fmd,  sizeof(fmd));
    dev.m_Write(0, buffer, numSectorsForMetadata);
}

void CFileSystem::pointFATStoEOF(size_t first, char * buffer) {
    size_t next = first;
    FATentry fe;
    do{
        memcpy(&fe, buffer + getFATentryOffset(next), sizeof(fe));
        next = fe.next;
        fe.next = EOF;
        fe.free = true;
        memcpy(buffer + getFATentryOffset(next),&fe, sizeof(fe));
    } while(next != EOF);
}



bool CFileSystem::CreateFs(const TBlkDev &dev) {
    maxSectors = dev.m_Sectors;
    FileSystemInfo fsInfo(dev.m_Sectors);
    sizeOfMetaData = sizeof(fsInfo);
    numSectorsForMetadata = sizeOfMetaData/SECTOR_SIZE;
    if(sizeOfMetaData%SECTOR_SIZE != 0){
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

int CFileSystem::openExisting(const char * fileName, bool writeMode){
    for(int i = 0; i<OPEN_FILES_MAX;i++){
        if(!openFiles[i].isValid){
            openFiles[i].isValid = true;
            openFiles[i].writeMode = writeMode;
            strcpy(openFiles[i].name, fileName);
            filesOpened++;
            return i;
        }
    }
    return -1;
}

int CFileSystem::OpenFile(const char *fileName, bool writeMode) {
    if(filesOpened >= OPEN_FILES_MAX){
        return -1;
    }
    if(writeMode){
        int fileIndex = findFile(fileName);
        if(fileIndex != -1){
            int fd = openExisting(fileName, writeMode);
            truncateFile(fileName, fileIndex);
            return fd;
        } else{
            //create
            createFile(fileName);
            return openExisting(fileName, writeMode);
        }
    } else{
        if(findFile(fileName) != -1){
            return openExisting(fileName, writeMode);
        } else{
            return -1;
        }
    }
}

size_t CFileSystem::ReadFile(int fd, void *data, size_t len) {
    size_t firstNeededSectorOrder = openFiles[fd].offset/SECTOR_SIZE;
    if(openFiles[fd].offset%SECTOR_SIZE!=0){
        firstNeededSectorOrder++;
    }
    size_t numNeededSectors = (openFiles[fd].offset%SECTOR_SIZE + len)/SECTOR_SIZE;
    if((openFiles[fd].offset%SECTOR_SIZE + len)%SECTOR_SIZE!= 0){
        numNeededSectors++;
    }
    //todo find needed sector nums in FAT

    //todo copy sectors to *data

    //increase offset
    return 0;
}


#ifndef __PROGTEST__

#include "simple_test.inc"

#endif /* __PROGTEST__ */
