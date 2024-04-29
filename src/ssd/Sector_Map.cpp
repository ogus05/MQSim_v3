#include "Sector_Map.h"

namespace SSD_Components{


    std::vector<Sector_Map_Entry>* Sector_Map::getAllRelatedPPAsInLPA(const LPA_type& lpa)
    {
        if(entry.find(lpa) == entry.end()){
            return NULL;
        } else{
            return entry.at(lpa);
        }
    }

    void Sector_Map::Insert(const LPA_type &lpa, const uint32_t &sector, const PPA_type &ppa, Sector_Log_WF_Entry* WFEntry)
    {
        auto itr = entry.find(lpa);
        if(itr == entry.end()){
            auto insertedEntry = entry.insert({lpa, new std::vector<Sector_Map_Entry>()}).first->second;
            insertedEntry->resize(sectorLog->sectorsPerPage, Sector_Map_Entry());
            insertedEntry->at(sector).ppa = ppa;
            insertedEntry->at(sector).storedBlock = WFEntry;
            if(WFEntry->storeSectors.find(lpa) != WFEntry->storeSectors.end()){
                PRINT_ERROR("SECTOR MAP INSERT")
            }
            WFEntry->storeSectors.insert({lpa, 1});
        } else{
            if(itr->second->at(sector).ppa != NO_PPA){
                std::unordered_map<LPA_type, uint32_t>& removeBlock = itr->second->at(sector).storedBlock->storeSectors;
                removeBlock.at(lpa)--;
                if(removeBlock.at(lpa) == 0){
                    removeBlock.erase(lpa);
                }
            }
            itr->second->at(sector).ppa = ppa;
            itr->second->at(sector).storedBlock = WFEntry;
            if(WFEntry->storeSectors.find(lpa) != WFEntry->storeSectors.end()){
                WFEntry->storeSectors.at(lpa)++;
            } else{
                WFEntry->storeSectors.insert({lpa, 1});
            }
        }
    }

    Sector_Map::~Sector_Map()
    {
        for(auto& e : entry){
            delete e.second;
        }
    }

void Sector_Map::Remove(const LPA_type &lpa)
    {
        auto itr = entry.find(lpa);

        if(itr != entry.end()){
            for(auto& entry: *itr->second){
                if(entry.storedBlock != NULL && (entry.storedBlock->storeSectors.find(lpa) != entry.storedBlock->storeSectors.end())){
                    entry.storedBlock->storeSectors.erase(lpa);
                }
            }
            delete itr->second;
            entry.erase(itr);
        }
    }


    Sector_Map_Entry::Sector_Map_Entry()
    {
        ppa = NO_PPA;
        storedBlock = NULL;
    }

}