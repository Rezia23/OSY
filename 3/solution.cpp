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
            FAT[i].next = EOF;
        }
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
    size_t getFollowingFATEntryIndex(size_t prevSector, char * buffer);

    int getFATentryOffset(size_t index){
        return (DIR_ENTRIES_MAX * sizeof(FileMetaData)) + sizeof(size_t) + (sizeof(FATentry) *index);
    }
    int getFirstFreeBlockIndexOffset(){
        return (DIR_ENTRIES_MAX * sizeof(FileMetaData));
    }
    int getFileMetaDataOffset(int index){
        return index * sizeof(FileMetaData);
    }
    FileMetaData getFileMetaData(const char * fileName, char * buffer);
    size_t getFirstNeededSectorNum(size_t offset);
    size_t getNumNeededSectors(size_t offset,size_t numToRead );
    FATentry getFATEntryAtIndex(size_t index, char * buffer);
    size_t getLastUsedSector(size_t first, char * buffer);
    size_t useFreeSector(char * buffer);
    void changeFATentry(size_t sector, size_t nextSector, char * buffer);
    void incrementFileSize(const char * fileName, size_t newSize, char * buffer);
};

void CFileSystem::incrementFileSize(const char * fileName, size_t newSize, char * buffer){
    for(int i = 0; i<DIR_ENTRIES_MAX;i++){
        FileMetaData fmd;
        memcpy(&fmd, buffer + i* sizeof(FileMetaData),sizeof(FileMetaData));
        if(fmd.valid && strcmp(fmd.name, fileName) == 0){
            fmd.size = newSize;
            memcpy(buffer + i* sizeof(FileMetaData), &fmd, sizeof(FileMetaData));
            break;
        }
    }
}


void CFileSystem::changeFATentry(size_t sector, size_t nextSector, char * buffer){
    FATentry originalFe;
    memcpy(&originalFe, buffer + getFATentryOffset(sector), sizeof(FATentry));

    FATentry newFe(nextSector, originalFe.free);        //might be problem with types
    memcpy(buffer + getFATentryOffset(sector),&newFe, sizeof(FATentry));
}

size_t CFileSystem::useFreeSector(char * buffer){
    size_t firstFree;
    memcpy(&firstFree, buffer + getFirstFreeBlockIndexOffset(), sizeof(size_t));
    //find next free
    size_t nextFreeBlock = firstFree+1;
    while(nextFreeBlock != firstFree){     //might be possibly broken if no blocks are free
        FATentry nextEntry = getFATEntryAtIndex(nextFreeBlock, buffer);
        if(nextEntry.free){
            memcpy(buffer + getFirstFreeBlockIndexOffset(), &nextFreeBlock, sizeof(size_t));
            break;
        } else{
            nextFreeBlock++;
            nextFreeBlock%maxSectors;
        }
    }
    return firstFree;
}

size_t CFileSystem::getLastUsedSector(size_t first, char * buffer){
    FATentry fe = getFATEntryAtIndex(first, buffer);
    size_t prev = first;
    while(fe.next != EOF){
        prev = fe.next;
        fe = getFATEntryAtIndex(fe.next, buffer);
    }
    return prev;
}

FATentry CFileSystem::getFATEntryAtIndex(size_t index, char * buffer){
    FATentry fe;
    memcpy(&fe, buffer + getFATentryOffset(index), sizeof(FATentry));
    return fe;
}

size_t CFileSystem::getNumNeededSectors(size_t offset,size_t numToRead ){
    size_t numNeededSectors = (offset%SECTOR_SIZE + numToRead)/SECTOR_SIZE;
    if((offset%SECTOR_SIZE + numToRead)%SECTOR_SIZE!= 0){
        numNeededSectors++;
    }
    return numNeededSectors;
}

size_t CFileSystem::getFirstNeededSectorNum(size_t offset){
    if(offset == 0){
        return 0;
    } else{
        size_t firstNeededSectorNum = offset/SECTOR_SIZE;
        if(offset % SECTOR_SIZE != 0){
            firstNeededSectorNum++;
        }
        return firstNeededSectorNum-1;
    }
}


FileMetaData  CFileSystem::getFileMetaData(const char * fileName, char * buffer){
    for(int i = 0; i<DIR_ENTRIES_MAX;i++){
        FileMetaData fmd;
        memcpy(&fmd, buffer + i* sizeof(FileMetaData),sizeof(FileMetaData));
        if(fmd.valid && strcmp(fmd.name, fileName) == 0){
            return fmd;
        }
    }
    return {};  //should not happen
}

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

    //find free sector
    size_t firstFreeBlock = useFreeSector(buffer);

    //write EOF to used sector in FAT
    FATentry fe(EOF, false);
    memcpy(buffer+ getFATentryOffset(firstFreeBlock), &fe, sizeof(fe) );

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
        fe = getFATEntryAtIndex(next, buffer);
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
size_t CFileSystem::getFollowingFATEntryIndex(size_t prevSector, char * buffer){
    FATentry fe = getFATEntryAtIndex(prevSector, buffer);
    return fe.next;
}

size_t CFileSystem::WriteFile(int fd, const void *data, size_t len){
    if(!openFiles[fd].writeMode){
        return 0;
    }
    char * buffer = new char [numSectorsForMetadata * SECTOR_SIZE];
    dev.m_Read(0, buffer, numSectorsForMetadata);
    FileMetaData fmd = getFileMetaData(openFiles[fd].name, buffer);
    size_t firstNeededSectorNum = getFirstNeededSectorNum(openFiles[fd].offset);
    size_t numNeededSectors = getNumNeededSectors(openFiles[fd].offset, len);

    size_t * neededSectors = new size_t [numNeededSectors];
    int startIndex = 0;
    if(openFiles[fd].offset%SECTOR_SIZE != 0){
        neededSectors[0] = getLastUsedSector(fmd.start, buffer);
        startIndex++;
    }
    for(int i = startIndex; i<numNeededSectors;i++){
        //find free sector
        neededSectors[i] = useFreeSector(buffer);
    }
    char * dataC = (char *) data;
    char sector[SECTOR_SIZE];
    size_t writePointer = 0;
    for(int i = 0; i<numNeededSectors;i++){
        if(numNeededSectors == 1 && openFiles[fd].offset%SECTOR_SIZE != 0){
            dev.m_Read(neededSectors[i], sector, 1);
            memcpy(sector + openFiles[fd].offset%SECTOR_SIZE, dataC, len);
            dev.m_Write(neededSectors[i], sector, 1);
            //break
        } else if(i == 0 && openFiles[fd].offset%SECTOR_SIZE != 0){
            //read sector
            dev.m_Read(neededSectors[i], sector, 1);
            size_t numToWrite = SECTOR_SIZE - openFiles[fd].offset%SECTOR_SIZE;
            memcpy(sector + openFiles[fd].offset%SECTOR_SIZE, dataC, numToWrite);
            writePointer = numToWrite;
            dev.m_Write(neededSectors[i], sector, 1);
        } else if(i == numNeededSectors-1){
            memcpy(sector, dataC + writePointer, len - writePointer);
            dev.m_Write(neededSectors[i], sector, 1);
            //break
        } else{
            memcpy(sector, dataC + writePointer, SECTOR_SIZE);
            dev.m_Write(neededSectors[i], sector, 1);
        }
    }

    //change entries in FAT
    for(int i = 0; i<numNeededSectors;i++){
        if(i == numNeededSectors-1){
            changeFATentry(neededSectors[i], EOF, buffer);
        } else{
            changeFATentry(neededSectors[i], neededSectors[i+1], buffer);
        }
    }
    //change size
    incrementFileSize(openFiles[fd].name, len+openFiles[fd].offset, buffer); //for Write files is offset same as size

    //change offset
    openFiles[fd].offset += len;
    return len;
}

size_t CFileSystem::ReadFile(int fd, void *data, size_t len) {
    if(openFiles[fd].writeMode){
        return 0;
    }
    char * buffer = new char [numSectorsForMetadata * SECTOR_SIZE];
    dev.m_Read(0, buffer, numSectorsForMetadata);
    FileMetaData fmd = getFileMetaData(openFiles[fd].name, buffer);

    size_t numToActuallyRead = len;
    if(len+openFiles[fd].offset > fmd.size){
        numToActuallyRead = fmd.size-openFiles[fd].offset;
    }

    size_t firstNeededSectorNum = getFirstNeededSectorNum(openFiles[fd].offset);

    size_t numNeededSectors = getNumNeededSectors(openFiles[fd].offset, numToActuallyRead);

    size_t * neededSectors = new size_t [numNeededSectors];
    size_t nextSector = fmd.start;

    for(int i = 0; i<firstNeededSectorNum+numNeededSectors;i++){
        if(i>=firstNeededSectorNum){
            neededSectors[i-firstNeededSectorNum] = nextSector;
        }
        nextSector = getFollowingFATEntryIndex(nextSector, buffer);
    }

    char * output  = new char [numToActuallyRead];
    char sector [SECTOR_SIZE];
    size_t outputPointer = 0;
    for(int i = 0; i<numNeededSectors;i++){
        dev.m_Read(neededSectors[i],sector ,1);
        if(numNeededSectors == 1){
            memcpy(output, sector + openFiles[fd].offset%SECTOR_SIZE, numToActuallyRead);
            //break
        } else if(i == 0){
            memcpy(output, sector + openFiles[fd].offset%SECTOR_SIZE, SECTOR_SIZE - (openFiles[fd].offset%SECTOR_SIZE));
            outputPointer = SECTOR_SIZE - (openFiles[fd].offset%SECTOR_SIZE);
        } else if(i == numNeededSectors-1){
            memcpy(output + outputPointer, sector, numToActuallyRead-outputPointer);
            //break
        } else{
            memcpy(output + outputPointer, sector, SECTOR_SIZE);
            outputPointer += SECTOR_SIZE;
        }
    }
    memcpy(data, output, numToActuallyRead);
    openFiles[fd].offset+= numToActuallyRead;
    return numToActuallyRead;
}


#ifndef __PROGTEST__

#include "simple_test.inc"

#endif /* __PROGTEST__ */
