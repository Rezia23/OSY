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
        strcpy(name, fn);
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
    CFileSystem(TBlkDev oldDev) : dev((oldDev)){
        maxSectors = dev.m_Sectors;
        sizeOfMetaData = sizeof(FileMetaData)*DIR_ENTRIES_MAX + sizeof(size_t) + sizeof(FATentry)*maxSectors;
        numSectorsForMetadata = sizeOfMetaData/SECTOR_SIZE;
        if(sizeOfMetaData%SECTOR_SIZE != 0){
            numSectorsForMetadata++;
        }

        metadata = new char [numSectorsForMetadata*SECTOR_SIZE];
        memset(metadata, 0, numSectorsForMetadata*SECTOR_SIZE);
        dev.m_Read(0, metadata, numSectorsForMetadata);
        printf("rozbije se to uz tady\n");
        FileMetaData fmd = getFileMetaDataAtIndex(0);
        if(fmd.valid){
            printf("valid");
        }else{
            printf("invalid");
        }
    }

    static bool CreateFs(const TBlkDev &dev);

    static CFileSystem *Mount(const TBlkDev &dev);

    bool Umount(void){
        for(int i = 0; i<OPEN_FILES_MAX;i++){
            if(openFiles[i].isValid){
                CloseFile(i);
            }
        }

        FileMetaData fmdOrigin = getFileMetaDataAtIndex(0);

        dev.m_Write(0, metadata, numSectorsForMetadata);

        //debug
        dev.m_Read(0, metadata, numSectorsForMetadata);
        FileMetaData fmdNew = getFileMetaDataAtIndex(0);
//        printf("%s\n", fmdNew.name);

        delete [] metadata;
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
    char * metadata;
    int iterator = 0;
    TBlkDev dev;
    static int sizeOfMetaData;
    static size_t maxSectors;
    static size_t numSectorsForMetadata;

    openFileEntry openFiles[OPEN_FILES_MAX];
    size_t filesOpened = 0;
    static void writeInfoData(FileSystemInfo & data, size_t len, const TBlkDev & dev, size_t numSectorsUsed){
        char * mem = new char [numSectorsUsed*SECTOR_SIZE];
        memset(mem, 0, numSectorsUsed*SECTOR_SIZE);
        //copy static stuff
        memcpy(mem, data.fileMetaData, sizeof(FileMetaData)*DIR_ENTRIES_MAX);
        memcpy(mem + sizeof(FileMetaData)*DIR_ENTRIES_MAX, &data.firstFreeBlock, sizeof(size_t));

        //copy FAT entries
        memcpy(mem+sizeof(FileMetaData)*DIR_ENTRIES_MAX + sizeof(size_t), data.FAT, sizeof(FATentry)*maxSectors);
        dev.m_Write(0, mem, numSectorsUsed);

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

int CFileSystem::sizeOfMetaData = 0;
size_t CFileSystem:: maxSectors = 0;
size_t CFileSystem::numSectorsForMetadata = 0;



bool CFileSystem::DeleteFile(const char *fileName){
    int index = findFile(fileName);
    if(index == -1){
        return false;
    }
    FileMetaData fmd = getFileMetaDataAtIndex(index);
    size_t FATindex = fmd.start;

    //write fmd as invalid
    fmd.valid = false;
    memcpy(metadata + getFileMetaDataOffset(index), &fmd, sizeof(fmd));
    //delete FATs
    size_t current = FATindex;
    FATentry fe;
    size_t prev;

    while(true){
        fe = getFATEntryAtIndex(current);
        if(fe.next == EOF){
            break;
        }
        prev = current;
        current = fe.next;
        FATentry newFe(EOF, false);
        memcpy(&metadata + getFATentryOffset(prev),&newFe, sizeof(FATentry) );
    }
    return true;
}

FileMetaData CFileSystem::getFileMetaDataAtIndex(int it){
    FileMetaData fmd;
//    printf("index is %d\n", it);
//    printf("fmd offset is %d \n", getFileMetaDataOffset(0));
    memcpy(&fmd, metadata + getFileMetaDataOffset(it), sizeof(FileMetaData));
    return fmd;
}

size_t CFileSystem:: FileSize(const char *fileName){
    int file = findFile(fileName);
    if(file == -1){
        return SIZE_MAX;
    }
    FileMetaData fmd = getFileMetaData(fileName);
    return fmd.size;
}
bool CFileSystem::FindNext(TFile &file){
    int originIterator = iterator;
    FileMetaData fmd = getFileMetaDataAtIndex(iterator);
    while(!fmd.valid){

        iterator++;
        iterator = iterator %DIR_ENTRIES_MAX;
        if(iterator == originIterator){
            return false;
        }
        fmd = getFileMetaDataAtIndex(iterator);
    }
    iterator++;
    file.m_FileSize = fmd.size;
    strcpy(file.m_FileName , fmd.name);
    return true;

}

bool CFileSystem::FindFirst(TFile &file){

    for(int i = 0; i<DIR_ENTRIES_MAX;i++){
        FileMetaData fmd = getFileMetaDataAtIndex(i);
        if(fmd.valid){
            printf("%s\n",file.m_FileName);
            strcpy(file.m_FileName, fmd.name);
            file.m_FileSize = fmd.size;
            return true;
        }
    }
    return false;
}

void CFileSystem::incrementFileSize(const char * fileName, size_t newSize){
    for(int i = 0; i<DIR_ENTRIES_MAX;i++){
        FileMetaData fmd;
        memcpy(&fmd, metadata + i* sizeof(FileMetaData),sizeof(FileMetaData));
        if(fmd.valid && strcmp(fmd.name, fileName) == 0){
            fmd.size = newSize;
            memcpy(metadata + i* sizeof(FileMetaData), &fmd, sizeof(FileMetaData));
            break;
        }
    }
}


void CFileSystem::changeFATentry(size_t sector, size_t nextSector){
    FATentry originalFe;
    memcpy(&originalFe, metadata + getFATentryOffset(sector), sizeof(FATentry));

    FATentry newFe(nextSector, originalFe.free);        //might be problem with types
    memcpy(metadata + getFATentryOffset(sector),&newFe, sizeof(FATentry));
}


size_t CFileSystem::useFreeSector(){
    size_t firstFree;
    memcpy(&firstFree, metadata + getFirstFreeBlockIndexOffset(), sizeof(size_t));
    //find next free
    size_t nextFreeBlock = firstFree+1;
    while(nextFreeBlock != firstFree){     //might be possibly broken if no blocks are free
        FATentry nextEntry = getFATEntryAtIndex(nextFreeBlock);
        if(nextEntry.free){
            memcpy(metadata + getFirstFreeBlockIndexOffset(), &nextFreeBlock, sizeof(size_t));
            break;
        } else{
            nextFreeBlock++;
            nextFreeBlock%=maxSectors;
        }
    }
    return firstFree;
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
    FATentry fe;
    memcpy(&fe, metadata + getFATentryOffset(index), sizeof(FATentry));
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


FileMetaData  CFileSystem::getFileMetaData(const char * fileName){
    for(int i = 0; i<DIR_ENTRIES_MAX;i++){
        FileMetaData fmd = getFileMetaDataAtIndex(i);
        if(fmd.valid && strcmp(fmd.name, fileName) == 0){
            return fmd;
        }
    }
    return {};  //should not happen
}

int CFileSystem::findFile(const char *fileName) const {

    for(int i = 0; i<DIR_ENTRIES_MAX;i++){
        FileMetaData fmd;
        memcpy(&fmd, metadata + i* sizeof(FileMetaData),sizeof(FileMetaData));
        if(fmd.valid && strcmp(fmd.name, fileName) == 0){
            return i;
        }
    }
    return -1;
}

void CFileSystem::createFile(const char *fileName) {

    //find free sector
    size_t firstFreeBlock = useFreeSector();

    //write EOF to used sector in FAT
    FATentry fe(EOF, false);
    memcpy(metadata+ getFATentryOffset(firstFreeBlock), &fe, sizeof(fe) );


    //put entry to FileEntry array
    for(int i = 0; i<DIR_ENTRIES_MAX;i++){
        FileMetaData fmd;
        memcpy(&fmd, metadata + i* sizeof(FileMetaData),sizeof(FileMetaData));
        if(!fmd.valid){
            FileMetaData newFmd (fileName, 0, firstFreeBlock, true) ;
            memcpy(metadata + i* sizeof(FileMetaData), &newFmd, sizeof(newFmd));
            break;
        }
    }
}


void CFileSystem::truncateFile(const char *fileName, int fileIndex) {
    FileMetaData fmd;
    memcpy(&fmd, metadata + getFileMetaDataOffset(fileIndex), sizeof(fmd));
    pointFATStoEOF(fmd.start);
    fmd.size = 0;
    memcpy(metadata + getFileMetaDataOffset(fileIndex),&fmd,  sizeof(fmd));
}

void CFileSystem::pointFATStoEOF(size_t first) {
    int next = (int)first;
    FATentry fe;
    do{
        fe = getFATEntryAtIndex(next);
        next = fe.next;
        fe.next = EOF;
        fe.free = true;
        memcpy(metadata + getFATentryOffset(next),&fe, sizeof(fe));
    } while(next != EOF);
}




bool CFileSystem::CreateFs(const TBlkDev &dev) {
    maxSectors = dev.m_Sectors;
    FileSystemInfo fsInfo(dev.m_Sectors);
    sizeOfMetaData = sizeof(FileMetaData)*DIR_ENTRIES_MAX + sizeof(size_t) + sizeof(FATentry)*maxSectors;
    numSectorsForMetadata = sizeOfMetaData/SECTOR_SIZE;
    if(sizeOfMetaData%SECTOR_SIZE != 0){
        numSectorsForMetadata++;
    }
    fsInfo.useFirstNBlocks(numSectorsForMetadata);
    writeInfoData(fsInfo, sizeof(fsInfo), dev, numSectorsForMetadata);
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

            ///debug
//            for(int i = 0; i<DIR_ENTRIES_MAX;i++){
//                FileMetaData fmd;
//                fmd = getFileMetaDataAtIndex(i);
//                printf("reee");
//            }

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
size_t CFileSystem::getFollowingFATEntryIndex(size_t prevSector){
    FATentry fe = getFATEntryAtIndex(prevSector);
    return fe.next;
}

size_t CFileSystem::WriteFile(int fd, const void *data, size_t len){
    if(!openFiles[fd].writeMode){
        return 0;
    }
    FileMetaData fmd = getFileMetaData(openFiles[fd].name);
    size_t numNeededSectors = getNumNeededSectors(openFiles[fd].offset, len);


    size_t * neededSectors = new size_t [numNeededSectors];
    int startIndex = 0;
    if(openFiles[fd].offset%SECTOR_SIZE != 0 || fmd.size == 0 ){
        neededSectors[0] = getLastUsedSector(fmd.start);
        startIndex++;
    }
    for(size_t i = startIndex; i<numNeededSectors;i++){
        //find free sector
        neededSectors[i] = useFreeSector();
    }

    char * dataC = (char *) data;
    char sector[SECTOR_SIZE];
    size_t writePointer = 0;
    for(size_t i = 0; i<numNeededSectors;i++){
        //printf("Writing to sector %zu \n", neededSectors[i]);
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
            memset(sector,0,SECTOR_SIZE);
            memcpy(sector, dataC + writePointer, len - writePointer);
            dev.m_Write(neededSectors[i], sector, 1);
            //break
        } else{
            memcpy(sector, dataC + writePointer, SECTOR_SIZE);
            dev.m_Write(neededSectors[i], sector, 1);
            writePointer+=SECTOR_SIZE;
        }
    }

    //change entries in FAT
    for(size_t i = 0; i<numNeededSectors;i++){
        if(i == numNeededSectors-1){
            changeFATentry(neededSectors[i], EOF);
        } else{
            changeFATentry(neededSectors[i], neededSectors[i+1]);
        }
    }
    //change size
    incrementFileSize(openFiles[fd].name, len+openFiles[fd].offset); //for Write files is offset same as size

    //change offset
    openFiles[fd].offset += len;
    delete [] neededSectors;
    return len;
}

size_t CFileSystem::ReadFile(int fd, void *data, size_t len) {
    if(openFiles[fd].writeMode){
        return 0;
    }
    FileMetaData fmd = getFileMetaData(openFiles[fd].name);

    size_t numToActuallyRead = len;
    if(len+openFiles[fd].offset > fmd.size){
        numToActuallyRead = fmd.size-openFiles[fd].offset;
    }

    size_t firstNeededSectorNum = getFirstNeededSectorNum(openFiles[fd].offset);

    size_t numNeededSectors = getNumNeededSectors(openFiles[fd].offset, numToActuallyRead);

    size_t * neededSectors = new size_t [numNeededSectors];
    size_t nextSector = fmd.start;

    for(size_t i = 0; i<firstNeededSectorNum+numNeededSectors;i++){
        if(i>=firstNeededSectorNum){
            neededSectors[i-firstNeededSectorNum] = nextSector;
        }
        nextSector = getFollowingFATEntryIndex(nextSector);
    }

    char * output  = new char [numToActuallyRead];
    char sector [SECTOR_SIZE];
    size_t outputPointer = 0;
    for(size_t i = 0; i<numNeededSectors;i++){
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
    delete [] neededSectors;
    return numToActuallyRead;
}


#ifndef __PROGTEST__

//#include "simple_test.inc"
#include "cool_test.inc"

#endif /* __PROGTEST__ */
