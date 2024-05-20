#include "BitFilter.h"
#include "SSD_Defs.h"


namespace SSD_Components{
    SectorCluster::SectorCluster()
    {
    }

    void SectorCluster::addSectors(LPA_type lpa, page_status_type sectors)
    {
        clusteredSectors.push_back({lpa, sectors});
    }

    std::list<SectorCluster*> BitFilter::makeCluster()
    {
        std::list<SectorCluster*> sectorCluster;

        SectorCluster* newSectorCluster = new SectorCluster();
        sectorCluster.push_back(newSectorCluster);
        
        uint32_t remainSectorSize = sectorsPerPage;
        for(auto subFilter : filter){
            page_status_type sectorsToInsert = 0;
            for(int sectorLocation = 0; sectorLocation < sectorsPerPage; sectorLocation++){
                if(remainSectorSize == 0){
                    newSectorCluster->addSectors(subFilter.first, sectorsToInsert);
                    sectorsToInsert = 0;
                    remainSectorSize = sectorsPerPage;

                    newSectorCluster = new SectorCluster();
                    sectorCluster.push_back(newSectorCluster);
                }

                if((((page_status_type)1 << sectorLocation) & subFilter.second) == 1){
                    remainSectorSize--;
                    sectorsToInsert |= ((page_status_type)1 << sectorLocation);
                }

            }

            newSectorCluster->addSectors(subFilter.first, sectorsToInsert);
        }

        return sectorCluster;
    }

    BitFilter::BitFilter(sim_time_type T_executeThreshold, uint64_t numberOfLogicalSector, uint32_t sectorsPerPage)
    {
        this->sectorsPerPage = sectorsPerPage;

        this->T_executeThreshold = T_executeThreshold;
        this->T_lastRead = 0;
        filterSize = 0;

    }

    BitFilter::~BitFilter()
    {
    }

    void BitFilter::addBit(const LPA_type &lpa, const page_status_type &sectors)
    {
        auto subFilter = filter.find(lpa);
        if(subFilter == filter.end()){
            subFilter = filter.insert({lpa, 0}).first;
        }
        filterSize += count_sector_no_from_status_bitmap(subFilter->second & sectors);
        subFilter->second |= sectors;
        T_lastRead = CurrentTimeStamp;
    }

    void BitFilter::removeBit(const LPA_type &lpa, const page_status_type &sectors)
    {
        auto subFilter = filter.find(lpa);
        if(subFilter != filter.end()){
            filterSize -= count_sector_no_from_status_bitmap(subFilter->second & sectors);
            subFilter->second &= ~(sectors);
        }

    }

    void BitFilter::reset()
    {
        filter.clear();
        T_lastRead = CurrentTimeStamp;
    }

    void BitFilter::polling()
    {
        if(((CurrentTimeStamp - T_lastRead) > T_executeThreshold) && (filterSize > sectorsPerPage)){
            startClustering();
        }
    }

    void BitFilter::startClustering()
    {
        std::list<SectorCluster*> clusterList = makeCluster();
    }

}
