#include "Sector_Log.h"

namespace SSD_Components{
    
    class Sector_Map_Entry{
    private:
    public:
        PPA_type ppa;
        Sector_Log_WF_Entry* storedBlock;

        Sector_Map_Entry();
    };

    class Sector_Map{
    private:
        Sector_Log* sectorLog;
        std::unordered_map<LPA_type, std::vector<Sector_Map_Entry>*> entry;
    public:
        Sector_Map(Sector_Log* in_sectorLog)
            :sectorLog(in_sectorLog){};
        ~Sector_Map();
        std::vector<Sector_Map_Entry>* getAllRelatedPPAsInLPA(const LPA_type& lpa);
        void Insert(const LPA_type& lpa, const uint32_t& sector, const PPA_type& ppa, Sector_Log_WF_Entry* WFEntry);
        void Remove(const LPA_type& lpa);
    };
}