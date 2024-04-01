#ifndef NVM_SECTORMAP_H
#define NVM_SECTORMAP_H

#include "Physical_Page_Address.h"
#include "LSMBlockBuffer.h"
#include <unordered_set>
#include <memory>

namespace SSD_Components{

    class LSMSectorMapInfo{
    public:
        LSA_type lsa;
        PPA_type ppa;
        std::shared_ptr<std::unordered_set<LSMSectorMapInfo*>> infoInCurPage;
        LSMSectorMapInfo(const LSA_type& lsa, const PPA_type& ppa);
        ~LSMSectorMapInfo();
    };

    class LSMSectorMapReadingInfo{
    public:
        PPA_type ppa;
        std::unordered_map<LPA_type, page_status_type> dataInPage;
        LSMSectorMapReadingInfo(const PPA_type ppa, std::unordered_map<LPA_type, page_status_type>& dataInPage);
    };

    class LSMSectorMapEntry
    {
    friend class LSMSectorMap;
    private:
        NVM::FlashMemory::Physical_Page_Address* lastWrittenPage;
        LPA_type highLPA;
        LPA_type lowLPA;
        uint32_t invalidSectorsCount;
        std::unordered_map<LPA_type, std::list<LSMSectorMapInfo*>> info;


        bool insertingPage;
        bool victim;
    public:
        LSMSectorMapEntry(NVM::FlashMemory::Physical_Page_Address* pba);
        ~LSMSectorMapEntry();
        
        void Insert(const std::list<LSA_type>& sectorsInsertToOnePage, const PPA_type& ppa);
        std::list<LSMSectorMapReadingInfo*> LookUp(LSA_type& LSAToLookUp);
        page_status_type Invalidate(const LSA_type& lsa);

        void reArrange();
        bool isOverlapped(const LSMSectorMapEntry* in_entry);
        void setAsVictim();

        NVM::FlashMemory::Physical_Page_Address getNextPage();
        std::unordered_map<PPA_type, uint32_t> getValidSectorsCountInPage();
        std::list<LSA_type> getValidLSAList();
        uint32_t getInvalidSectorsCount();
        NVM::FlashMemory::Physical_Page_Address& getPBA();
    };

    class LSMSectorMap
    {
    private:
        LSMSectorMap* upperLevelSectorMap;
        std::unordered_map<LPA_type, std::unordered_set<LSMSectorMapEntry*>> mappingLPA;

        const uint32_t level;
        const uint32_t maxBlockCount;

        uint32_t entriesCount = 0;

        bool compaction;

    public:
        std::list<LSMSectorMapEntry*>* getVictimGroup();
        LSMSectorMap(const uint32_t maxBlockCount, const uint32_t level, LSMSectorMap* upperLevelSectorMap);
        ~LSMSectorMap();
        std::list<LSMSectorMapReadingInfo*> LookUp(LSA_type& LSAToLookUp);

        void InsertEntry(std::list<LSMSectorMapEntry*> entryList);
        bool EraseEntry(LSMSectorMapEntry* entry);
        void MoveEntryToUpperLevel(LSMSectorMapEntry* entry);

        page_status_type Invalidate(const LSA_type& lsa);

        bool isNoFreeCapacity();
        bool isCompactioning();
        void startCompaction();
        void endCompaction();
    };


}

#endif