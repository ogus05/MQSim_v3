#include "BitFilter.h"
#include "SSD_Defs.h"


namespace SSD_Components{
    SubPageCluster::SubPageCluster()
    {
    }

    std::list<SubPageCluster*> BitFilter::makeClusterList()
    {
        std::list<SubPageCluster*> sectorClusterList;

        SubPageCluster* newSectorCluster = new SubPageCluster();
        sectorClusterList.push_back(newSectorCluster);
        uint32_t remainSizeInSubPages = sectorLog->subPagesPerPage;

        for(auto key : filter){
            newSectorCluster->clusteredSectors.push_back(key);
            remainSizeInSubPages--;
            if(remainSizeInSubPages == 0){
                newSectorCluster = new SubPageCluster();
                sectorClusterList.push_back(newSectorCluster);
                remainSizeInSubPages = sectorLog->subPagesPerPage;
            }
        }

        return sectorClusterList;
    }

    BitFilter* BitFilter::instance = NULL;
    BitFilter::BitFilter(sim_time_type T_executeThreshold, SectorLog* sectorLog)
    {
        instance = this;
        this->sectorLog = sectorLog;
        isClustering = false;
        this->T_executeThreshold = T_executeThreshold;
        this->T_lastRead = 0;

        remainReadForClustering = 0;
        remainWriteForClustering = 0;

        if(T_executeThreshold != 0){
            Simulator->AttachPerodicalFnc(polling);
            Simulator->AttachClearStats(reset);
        }
    }

    BitFilter::~BitFilter()
    {
    }

    bool BitFilter::isClusteringProcessing()
    {
        return isClustering;
    }

    void BitFilter::addBit(const key_type key)
    {
        filter.insert(key);
        T_lastRead = CurrentTimeStamp;
    }

    void BitFilter::removeBit(const key_type key)
    {
        if(filter.find(key) != filter.end()){
            filter.erase(key);
        }
    }

    void BitFilter::reset()
    {
        instance->filter.clear();
        instance->T_lastRead = CurrentTimeStamp;
    }

    void BitFilter::polling()
    {
        if(!instance->isClustering){
            if(((CurrentTimeStamp - instance->T_lastRead) > instance->T_executeThreshold) && (instance->filter.size() >= instance->sectorLog->subPagesPerPage)){
                instance->isClustering = true;
                std::list<key_type> subPagesToRead = std::list<key_type>(instance->filter.begin(), instance->filter.end());
                instance->sectorLog->sendReadForClustering(subPagesToRead);
            }
        }
    }

    void BitFilter::setRemainRead(uint32_t remainReadForClustering)
    {
        this->remainReadForClustering = remainReadForClustering;
    }

    void BitFilter::handleClusteringReadIsArrived()
    {
        remainReadForClustering--;
        if(remainReadForClustering == 0){
            startClustering();
        }
    }

    void BitFilter::handleClusteringWriteIsArrived()
    {
        remainWriteForClustering--;
        if(remainWriteForClustering == 0){
            endClustering();
        }
    }

    void BitFilter::startClustering()
    {
        std::list<SubPageCluster*> subPageClusterList = makeClusterList();

        remainWriteForClustering = subPageClusterList.size();
        for(SubPageCluster* subPageCluster : subPageClusterList){
            sectorLog->sendSubPageWriteForClustering(subPageCluster->clusteredSectors);
        }
    }

    void BitFilter::endClustering()
    {
        isClustering = false;
        T_lastRead = CurrentTimeStamp;
        filter.clear();
        sectorLog->sectorMap->checkMergeIsRequired();
        for(std::list<NVM_Transaction*>* trList : pendingTrList){
            sectorLog->handleInputTransaction(*trList);
            delete trList;
        }
        pendingTrList.clear();
    }
    void BitFilter::addPendingTrListUntilClustering(std::list<NVM_Transaction *>& transaction_list)
    {
        pendingTrList.push_back(new std::list<NVM_Transaction*>(transaction_list));
    }
}
