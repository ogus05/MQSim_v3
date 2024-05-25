#include "BitFilter.h"
#include "SSD_Defs.h"


namespace SSD_Components{
    SectorCluster::SectorCluster()
    {
    }

    void SectorCluster::addSubPage(key_type key)
    {
        clusteredSectors.push_back(key);
    }

    std::list<SectorCluster*> BitFilter::makeClusterList()
    {
        std::list<SectorCluster*> sectorCluster;

        SectorCluster* newSectorCluster = nullptr;
        uint32_t remainSizeInSubPages = 0;
        
        for(auto subFilter : filter){
            for(int subFilterOffset = 0; subFilterOffset < 64; subFilterOffset++){
                if((((uint64_t)1 << subFilterOffset) & subFilter.second) != 0){
                    if(remainSizeInSubPages == 0){
                        newSectorCluster = new SectorCluster();
                        sectorCluster.push_back(newSectorCluster);
                        remainSizeInSubPages = subPagesPerPage;
                    }

                    remainSizeInSubPages--;
                    newSectorCluster->addSubPage(subFilter.first * 64 + subFilter.second);

                }

            }
        }

        return sectorCluster;
    }

    BitFilter::BitFilter(sim_time_type T_executeThreshold, uint64_t numberOfSubpages, uint32_t subPagesPerPage)
    {
        this->subPagesPerPage = subPagesPerPage;

        this->T_executeThreshold = T_executeThreshold;
        this->T_lastRead = 0;
        filterSize = 0;

    }

    BitFilter::~BitFilter()
    {
    }

    void BitFilter::addBit(const LPA_type &lpa, const page_status_type &subPageBitmap)
    {
        for(auto subPageOffset = 0; subPageOffset < subPagesPerPage; subPageOffset++){
            if(((page_status_type(1) << subPageOffset) & subPageBitmap) > 0){
                key_type key = MAKE_KEY(lpa, subPageOffset);
                auto subFilter = filter.find(key / 64);
                if(subFilter == filter.end()){
                    subFilter = filter.insert({key / 64, 0}).first;
                }
                filterSize++;
                subFilter->second |= (key % 64);
            }
        }
        T_lastRead = CurrentTimeStamp;
    }

    void BitFilter::removeBit(const LPA_type &lpa, const page_status_type &subPageBitmap)
    {
        for(auto subPageOffset = 0; subPageOffset < subPagesPerPage; subPageOffset++){
            if(((page_status_type(1) << subPageOffset) & subPageBitmap) > 0){
                key_type key = MAKE_KEY(lpa, subPageOffset);
                auto subFilter = filter.find(key / 64);
                if(subFilter != filter.end()){
                    filterSize--;
                    subFilter->second &= ~(subPageOffset);
                }
            }
        }
    }

    void BitFilter::reset()
    {
        filter.clear();
        T_lastRead = CurrentTimeStamp;
    }

    void BitFilter::polling()
    {
        if(((CurrentTimeStamp - T_lastRead) > T_executeThreshold) && (filterSize > subPagesPerPage * SUB_PAGE_UNIT)){
            startClustering();
        }
    }

    void BitFilter::startClustering()
    {
        std::list<key_type> subPagesToRead;
        for(auto subFilter : filter){
            for(auto subPageOffset = 0; subPageOffset < subPagesPerPage; subPageOffset){
                if(((page_status_type(1) << subPageOffset) & subFilter.second) > 0){
                    key_type key = MAKE_KEY(subFilter.first, subPageOffset);
                    subPagesToRead.push_back(key);
                }
            }
        }

        sectorLog->sendReadForClustering(subPagesToRead);


    }

}
