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
        memset(name, 0, sizeof (name));
        size = 0;
        start = 0;
    }
    FileMetaData(const char * fn, size_t _size, size_t _start, bool _valid){
        strncpy(name, fn, FILENAME_LEN_MAX);
        name[FILENAME_LEN_MAX] = 0;
        size = _size;
        start = _start;
        valid = _valid;
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
        for(size_t i = 0; i<numSectors;i++){
            FAT[i].next = EOF;
        }
        firstFreeBlock = 0;
    }
    ~FileSystemInfo(){
        delete [] FAT;
    }
    void useFirstNBlocks(size_t n){
        for(size_t i = 0; i<n;i++){
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
    int FreeSectorsCount(){
        return freeSectors;
    }
    CFileSystem(TBlkDev oldDev) : dev((oldDev)){
        maxSectors = dev.m_Sectors;
        sizeOfMetaData = sizeof(FileMetaData)*DIR_ENTRIES_MAX + sizeof(size_t) + sizeof(FATentry)*maxSectors;
        numSectorsForMetadata = sizeOfMetaData/SECTOR_SIZE;
        if(sizeOfMetaData%SECTOR_SIZE != 0){
            numSectorsForMetadata++;
        }
        freeSectors = maxSectors - numSectorsForMetadata;
        char * mem = new char [numSectorsForMetadata*SECTOR_SIZE];
        memset(mem, 0, numSectorsForMetadata*SECTOR_SIZE);
        dev.m_Read(0, mem, numSectorsForMetadata);

        //copy file entries
        memcpy(&fileMetaData, mem, sizeof(FileMetaData)*DIR_ENTRIES_MAX);
        memcpy(&firstFreeSector, mem + sizeof(FileMetaData)*DIR_ENTRIES_MAX, sizeof(size_t));
        FAT = new FATentry[maxSectors];
        memcpy(FAT, mem + sizeof(FileMetaData)*DIR_ENTRIES_MAX + sizeof(size_t), maxSectors*sizeof(FATentry));
        delete [] mem;
    }
    ~CFileSystem(){
        delete [] FAT;
    }

    static bool CreateFs(const TBlkDev &dev);

    static CFileSystem *Mount(const TBlkDev &dev);

    bool Umount(void){
        for(int i = 0; i<OPEN_FILES_MAX;i++){
            if(openFiles[i].isValid){
                CloseFile(i);
            }
        }

        char * mem = new char [numSectorsForMetadata * SECTOR_SIZE];
        memcpy(mem, &fileMetaData, DIR_ENTRIES_MAX * sizeof(FileMetaData));
        memcpy(mem + DIR_ENTRIES_MAX * sizeof(FileMetaData), &firstFreeSector, sizeof(size_t));
        memcpy(mem + DIR_ENTRIES_MAX * sizeof(FileMetaData) + sizeof(size_t), FAT, maxSectors * sizeof(FATentry));

        dev.m_Write(0, mem, numSectorsForMetadata);

        delete [] mem;
        return true;
    }

    size_t FileSize(const char *fileName);

    int OpenFile(const char *fileName, bool writeMode);

    bool CloseFile(int fd) {
        if(!openFiles[fd].isValid){
            return false;
        }
        openFiles[fd].isValid = false;
        filesOpened--;
        return true;
    }

    size_t ReadFile(int fd, void *data, size_t len);

    size_t WriteFile(int fd, const void *data, size_t len);

    bool DeleteFile(const char *fileName);

    bool FindFirst(TFile &file);

    bool FindNext(TFile &file);

private:
    int freeSectors;
    int iterator = 0;
    TBlkDev dev;
    int sizeOfMetaData;
    size_t maxSectors;
    size_t numSectorsForMetadata;
    int fileCount = 0;
    openFileEntry openFiles[OPEN_FILES_MAX];
    size_t filesOpened = 0;

    FileMetaData fileMetaData[DIR_ENTRIES_MAX];
    size_t firstFreeSector = 0;
    FATentry * FAT;


    static void writeInfoData(FileSystemInfo & data, size_t len, const TBlkDev & dev, size_t numSectorsUsed, size_t maxSectors){
        char * mem = new char [numSectorsUsed*SECTOR_SIZE];
        memset(mem, 0, numSectorsUsed*SECTOR_SIZE);
        //copy static stuff
        memcpy(mem, data.fileMetaData, sizeof(FileMetaData)*DIR_ENTRIES_MAX);
        memcpy(mem + (sizeof(FileMetaData)*DIR_ENTRIES_MAX), &data.firstFreeBlock, sizeof(size_t));

        //copy FAT entries
        memcpy(mem+(sizeof(FileMetaData)*DIR_ENTRIES_MAX) + sizeof(size_t), data.FAT, sizeof(FATentry)*maxSectors);
        dev.m_Write(0, mem, numSectorsUsed);

        dev.m_Read(0,mem, numSectorsUsed);
        FATentry fe;
        memcpy(&fe, mem + sizeof(FileMetaData)*DIR_ENTRIES_MAX + sizeof(size_t) + sizeof(FATentry)*(maxSectors-1), sizeof(FATentry));
        memcpy(&fe, mem + sizeof(FileMetaData)*DIR_ENTRIES_MAX + sizeof(size_t) + sizeof(FATentry)*1, sizeof(FATentry));
        memcpy(&fe, mem + sizeof(FileMetaData)*DIR_ENTRIES_MAX + sizeof(size_t) + sizeof(FATentry)*10, sizeof(FATentry));
        memcpy(&fe, mem + sizeof(FileMetaData)*DIR_ENTRIES_MAX + sizeof(size_t) + sizeof(FATentry)*(maxSectors-2), sizeof(FATentry));

        delete [] mem;
    }
    int openExisting(const char * fileName, bool writeMode);
    int findFile(const char *fileName) const;
    void createFile(const char *fileName);
    void truncateFile(const char *fileName, int fileIndex);
    void pointFATStoEOF(size_t first);
    size_t getFollowingFATEntryIndex(size_t prevSector);

    int getFATentryOffset(size_t index){
        return (DIR_ENTRIES_MAX * sizeof(FileMetaData)) + sizeof(size_t) + (sizeof(FATentry) *index);
    }
    int getFirstFreeBlockIndexOffset(){
        return (DIR_ENTRIES_MAX * sizeof(FileMetaData));
    }
    int getFileMetaDataOffset(int index){
        return index * sizeof(FileMetaData);
    }
    FileMetaData getFileMetaData(const char * fileName);
    size_t getFirstNeededSectorNum(size_t offset);
    size_t getNumNeededSectors(size_t offset,size_t numToRead );
    FATentry getFATEntryAtIndex(size_t index);
    size_t getLastUsedSector(size_t first);
    size_t useFreeSector();
    void changeFATentry(size_t sector, size_t nextSector);
    void incrementFileSize(const char * fileName, size_t newSize);
    FileMetaData getFileMetaDataAtIndex(int it);
};


bool CFileSystem::DeleteFile(const char *fileName){
    int index = findFile(fileName);
    if(index == -1){
        return false;
    }
//    for(size_t i = 0; i<OPEN_FILES_MAX;i++){
//        if(openFiles[i].isValid && strncmp(openFiles[i].name, fileName, FILENAME_LEN_MAX)==0){
//            CloseFile(i);
//        }
//    }
    fileCount--;
    size_t FATindex = fileMetaData[index].start;

    //write fmd as invalid
    fileMetaData[index].valid = false;

    //delete FATs

    pointFATStoEOF(FATindex);
    FAT[FATindex].next = EOF;
    FAT[FATindex].free = true;
    freeSectors++;

    return true;
}

FileMetaData CFileSystem::getFileMetaDataAtIndex(int it){
    return fileMetaData[it];
}

size_t CFileSystem:: FileSize(const char *fileName){
    char correctName[FILENAME_LEN_MAX+1];
    strncpy(correctName, fileName, FILENAME_LEN_MAX);
    correctName[FILENAME_LEN_MAX] = 0;
    int file = findFile(correctName);
    if(file == -1){
        return SIZE_MAX;
    }
    FileMetaData fmd = getFileMetaData(correctName);
    return fmd.size;
}
bool CFileSystem::FindNext(TFile &file){
    if(iterator >=DIR_ENTRIES_MAX){
        return false;
    }
    FileMetaData fmd = getFileMetaDataAtIndex(iterator);
    while(!fmd.valid){

        iterator++;
        if(iterator >=DIR_ENTRIES_MAX){
            return false;
        }
        fmd = getFileMetaDataAtIndex(iterator);
    }
    iterator++;
    file.m_FileSize = fmd.size;
    strncpy(file.m_FileName, fmd.name, FILENAME_LEN_MAX);
    file.m_FileName[FILENAME_LEN_MAX] = 0;
//    printf("Name: %s, size: %zu, firstSector: %zu", fmd.name, fmd.size,fmd.start);
    return true;

}

bool CFileSystem::FindFirst(TFile &file){

    for(int i = 0; i<DIR_ENTRIES_MAX;i++){
        FileMetaData fmd = getFileMetaDataAtIndex(i);
        if(fmd.valid){
//            printf("%s\n",file.m_FileName);

            strncpy(file.m_FileName, fmd.name, FILENAME_LEN_MAX);
            file.m_FileName[FILENAME_LEN_MAX] = 0;
            file.m_FileSize = fmd.size;
            iterator = i+1;
//            printf("Name: %s, size: %zu, firstSector: %zu", fmd.name, fmd.size,fmd.start);
            return true;
        }
    }
    return false;
}

void CFileSystem::incrementFileSize(const char * fileName, size_t newSize){
    char correctFileName [FILENAME_LEN_MAX+1];
    strncpy(correctFileName, fileName, FILENAME_LEN_MAX);
    correctFileName[FILENAME_LEN_MAX] = 0;

    for(int i = 0; i<DIR_ENTRIES_MAX;i++){
        if(fileMetaData[i].valid && strcmp(fileMetaData[i].name, correctFileName) == 0){
            fileMetaData[i].size = newSize;
            break;
        }
    }
}


void CFileSystem::changeFATentry(size_t sector, size_t nextSector){
    FAT[sector].next = nextSector;
}


size_t CFileSystem::useFreeSector(){
    size_t firstFree = firstFreeSector;
    FAT[firstFree].free = false;
    //find next free
    size_t nextFreeBlock = firstFree+1;
    while(nextFreeBlock != firstFree){     //might be possibly broken if no blocks are free
        if(FAT[nextFreeBlock].free){
            firstFreeSector = nextFreeBlock;
            freeSectors--;
            return firstFree;
        } else{
            nextFreeBlock++;
            nextFreeBlock = nextFreeBlock%maxSectors;
        }
    }
   return 0;
}

size_t CFileSystem::getLastUsedSector(size_t first){
    FATentry fe = getFATEntryAtIndex(first);
    size_t prev = first;
    while(fe.next != EOF){
        prev = fe.next;
        fe = getFATEntryAtIndex(fe.next);
    }
    return prev;
}

FATentry CFileSystem::getFATEntryAtIndex(size_t index){
    return FAT[index];
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


FileMetaData  CFileSystem::getFileMetaData(const char * fileName){
    char correctFileName [FILENAME_LEN_MAX+1];
    strncpy(correctFileName, fileName, FILENAME_LEN_MAX);
    correctFileName[FILENAME_LEN_MAX] = 0;
    for(int i = 0; i<DIR_ENTRIES_MAX;i++){
        FileMetaData fmd = getFileMetaDataAtIndex(i);
        if(fmd.valid && strcmp(fmd.name, correctFileName) == 0){
            return fmd;
        }
    }
    return {};  //should not happen
}

int CFileSystem::findFile(const char *fileName) const {

    for(int i = 0; i<DIR_ENTRIES_MAX;i++){
        if(fileMetaData[i].valid && strcmp(fileMetaData[i].name, fileName) == 0){
            return i;
        }
    }
    return -1;
}

void CFileSystem::createFile(const char *fileName) {

    //find free sector
    size_t firstFreeBlock = useFreeSector();

    //write EOF to used sector in FAT
    FAT[firstFreeBlock].free = false;
    FAT[firstFreeBlock].next = EOF;


    //put entry to FileEntry array
    for(int i = 0; i<DIR_ENTRIES_MAX;i++){
        if(!fileMetaData[i].valid){
            fileMetaData[i].valid = true;
            strcpy(fileMetaData[i].name, fileName);
            fileMetaData[i].size = 0;
            fileMetaData[i].start = firstFreeBlock;
            break;
        }
    }
}

void CFileSystem::truncateFile(const char *fileName, int fileIndex) {
    pointFATStoEOF(fileMetaData[fileIndex].start);
    fileMetaData[fileIndex].size = 0;
}

void CFileSystem::pointFATStoEOF(size_t first) {
    int prev;
    int next = (int)first;

    if(FAT[first].next == EOF){
        return;
    }
    //set first to EOF
    prev = (int) first;
    next = FAT[first].next;

    FAT[first].next = EOF;
    FAT[first].free = false;

    while(FAT[next].next!= EOF){
        prev = next;
        next = FAT[prev].next;
        FAT[prev].next = EOF;
        FAT[prev].free = true;
        freeSectors++;
    }
    //set the last entry as invalid
    FAT[next].free = true;
    freeSectors++;
}




bool CFileSystem::CreateFs(const TBlkDev &dev) {
    FileSystemInfo fsInfo(dev.m_Sectors);
    size_t maxSectors = dev.m_Sectors;
    size_t sizeOfMetaData = sizeof(FileMetaData)*DIR_ENTRIES_MAX + sizeof(size_t) + sizeof(FATentry)*maxSectors;
    size_t numSectorsForMetadata = sizeOfMetaData/SECTOR_SIZE;
    if(sizeOfMetaData%SECTOR_SIZE != 0){
        numSectorsForMetadata++;
    }
    fsInfo.useFirstNBlocks(numSectorsForMetadata);
    writeInfoData(fsInfo, sizeof(fsInfo), dev, numSectorsForMetadata, maxSectors);
    //printf("whole info %d\n filemetadata %d\n size_t %d\n FATentry %d\n", sizeof(fsInfo), sizeof(FileMetaData), sizeof(size_t), sizeof(FATentry));
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
            openFiles[i].offset = 0;
            openFiles[i].writeMode = writeMode;
            strncpy(openFiles[i].name, fileName, FILENAME_LEN_MAX);
            openFiles[i].name[FILENAME_LEN_MAX] = 0;
            filesOpened++;
            return i;
        }
    }
    return -1;
}

int CFileSystem::OpenFile(const char *fileName, bool writeMode) {
    char correctFileName [FILENAME_LEN_MAX+1];
    strncpy(correctFileName, fileName, FILENAME_LEN_MAX);
    correctFileName[FILENAME_LEN_MAX] = 0;

    if(filesOpened >= OPEN_FILES_MAX || freeSectors <= 0 || fileCount >= DIR_ENTRIES_MAX){
        return -1;
    }
    int fileIndex = findFile(correctFileName);
    if(fileIndex == -1){    //file does not exist
        if(!writeMode){
            return -1;
        } else{
            createFile(fileName);
            return openExisting(correctFileName, writeMode);
        }
    } else{                 //file exists
        if(!writeMode){
           return openExisting(correctFileName, writeMode);
        } else{             //truncate
            truncateFile(correctFileName, fileIndex);
            return openExisting(correctFileName, writeMode);
        }
    }
}
size_t CFileSystem::getFollowingFATEntryIndex(size_t prevSector){
    FATentry fe = getFATEntryAtIndex(prevSector);
    return fe.next;
}

size_t CFileSystem::WriteFile(int fd, const void *data, size_t len){
    if(fd == -1){
        return 0;
    }
    if(!openFiles[fd].writeMode || len == 0 || !openFiles[fd].isValid){
        return 0;
    }
    FileMetaData fmd = getFileMetaData(openFiles[fd].name);
    size_t numNeededSectors = getNumNeededSectors(openFiles[fd].offset, len);


    size_t * neededSectors = new size_t [numNeededSectors];
    int startIndex = 0;
    size_t endIndex = numNeededSectors;

    size_t lastUsedSector = getLastUsedSector(fmd.start);
    if(openFiles[fd].offset%SECTOR_SIZE != 0 || fmd.size == 0 ){
        neededSectors[0] = lastUsedSector;
        startIndex++;
    }
    for(size_t i = startIndex; i<numNeededSectors;i++){
        if(freeSectors <=0){
            endIndex = i;
            break;
        }
        //find free sector
        neededSectors[i] = useFreeSector();
    }

    char * dataC = (char *) data;
    char sector[SECTOR_SIZE];
    size_t writePointer = 0;
    for(size_t i = 0; i<endIndex;i++){
        //printf("Writing to sector %zu \n", neededSectors[i]);
        if(numNeededSectors == 1 && openFiles[fd].offset%SECTOR_SIZE != 0){
            dev.m_Read(neededSectors[i], sector, 1);
            memcpy(sector + openFiles[fd].offset%SECTOR_SIZE, dataC, len);
            dev.m_Write(neededSectors[i], sector, 1);
            writePointer += len;
            //break
        } else if(i == 0 && openFiles[fd].offset%SECTOR_SIZE != 0){
            //read sector
            dev.m_Read(neededSectors[i], sector, 1);
            size_t numToWrite = SECTOR_SIZE - openFiles[fd].offset%SECTOR_SIZE;
            memcpy(sector + openFiles[fd].offset%SECTOR_SIZE, dataC, numToWrite);
            writePointer = numToWrite;
            dev.m_Write(neededSectors[i], sector, 1);
        } else if(i == numNeededSectors-1){
            memset(sector,0,SECTOR_SIZE);
            memcpy(sector, dataC + writePointer, len - writePointer);
            dev.m_Write(neededSectors[i], sector, 1);
            writePointer += len-writePointer;
            //break
        } else{
            memcpy(sector, dataC + writePointer, SECTOR_SIZE);
            dev.m_Write(neededSectors[i], sector, 1);
            writePointer+=SECTOR_SIZE;
        }
    }

    //change entries in FAT
    for(size_t i = 0; i<endIndex;i++){
        if(i == endIndex-1){
            changeFATentry(neededSectors[i], EOF);
        } else{
            changeFATentry(neededSectors[i], neededSectors[i+1]);
        }
    }
    if(lastUsedSector != neededSectors[0] && endIndex >=1){
        changeFATentry(lastUsedSector, neededSectors[0]);
    }
    //change size
    incrementFileSize(openFiles[fd].name, writePointer+openFiles[fd].offset); //for Write files is offset same as size

    //change offset
    openFiles[fd].offset += writePointer;
    delete [] neededSectors;
    return writePointer;
}

size_t CFileSystem::ReadFile(int fd, void *data, size_t len) {
    if(fd==-1){
        return 0;
    }
    if(openFiles[fd].writeMode || len == 0){
        return 0;
    }
    FileMetaData fmd = getFileMetaData(openFiles[fd].name);

    size_t numToActuallyRead = len;
    if(len+openFiles[fd].offset > fmd.size){
        numToActuallyRead = fmd.size-openFiles[fd].offset;
    }

    size_t firstNeededSectorNum = getFirstNeededSectorNum(openFiles[fd].offset);
    if(openFiles[fd].offset%SECTOR_SIZE==0 && openFiles[fd].offset!=0){
        firstNeededSectorNum++;
    }

    size_t numNeededSectors = getNumNeededSectors(openFiles[fd].offset, numToActuallyRead);

    size_t * neededSectors = new size_t [numNeededSectors];
    size_t nextSector = fmd.start;

    for(size_t i = 0; i<firstNeededSectorNum+numNeededSectors;i++){
        if(i>=firstNeededSectorNum){
            neededSectors[i-firstNeededSectorNum] = nextSector;
        }
        nextSector = getFollowingFATEntryIndex(nextSector);
    }

    char * output = (char *) data;
    char sector [SECTOR_SIZE];
    size_t outputPointer = 0;
    for(size_t i = 0; i<numNeededSectors;i++){
        dev.m_Read(neededSectors[i],sector ,1);
        if(numNeededSectors == 1){
            memcpy(output, sector + openFiles[fd].offset%SECTOR_SIZE, numToActuallyRead);
            outputPointer+=numToActuallyRead;
            //break
        } else if(i == 0){
            memcpy(output, sector + openFiles[fd].offset%SECTOR_SIZE, SECTOR_SIZE - (openFiles[fd].offset%SECTOR_SIZE));
            outputPointer = SECTOR_SIZE - (openFiles[fd].offset%SECTOR_SIZE);
        } else if(i == numNeededSectors-1){
            memcpy(output + outputPointer, sector, numToActuallyRead-outputPointer);
            outputPointer += numToActuallyRead-outputPointer;
            //break
        } else{
            memcpy(output + outputPointer, sector, SECTOR_SIZE);
            outputPointer += SECTOR_SIZE;
        }
    }
    memcpy(data, output, numToActuallyRead);
    openFiles[fd].offset+= numToActuallyRead;
    delete [] neededSectors;
//    delete [] output;
    return outputPointer;
}


#ifndef __PROGTEST__

//#include "simple_test.inc"
#include "cool_test.inc"
//#include "simple_test_mega.inc"
//#include "another_test.inc"
#endif /* __PROGTEST__ */
