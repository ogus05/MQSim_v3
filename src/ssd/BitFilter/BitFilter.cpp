#include "BitFilter.h"
#include "SSD_Defs.h"
#include "Data_Cache_Manager_Flash_Advanced.h"


namespace SSD_Components{
    SubPageCluster::SubPageCluster()
    {
    }

    std::list<SubPageCluster*>* BitFilter::makeClusterList()
    {
        if(processingFilter->size() < sectorLog->subPagesPerPage){
            return NULL;
        }
        std::list<SubPageCluster*>* sectorClusterList = new std::list<SubPageCluster*>();


        SubPageCluster* newSectorCluster;
        uint32_t remainSizeInSubPages = 0;

        for(auto key : (*processingFilter)){
            if(remainSizeInSubPages == 0){
                newSectorCluster = new SubPageCluster();
                sectorClusterList->push_back(newSectorCluster);
                remainSizeInSubPages = sectorLog->subPagesPerPage;
            }
            newSectorCluster->clusteredSectors.push_back(key);
            remainSizeInSubPages--;
        }

        return sectorClusterList;
    }

    BitFilter* BitFilter::instance = NULL;
    BitFilter::BitFilter(sim_time_type T_executeThreshold, SectorLog* sectorLog)
    {
        this->T_executeThreshold = T_executeThreshold;
        processingFilter = NULL;
        if(T_executeThreshold != 0){
            instance = this;
            this->sectorLog = sectorLog;
            this->T_lastRead = 0;

            remainReadForClustering = 0;
            remainWriteForClustering = 0;

            filter = new std::set<key_type>();

            Simulator->AttachPerodicalFnc(polling);
            Simulator->AttachClearStats(reset);
        }
    }

    BitFilter::~BitFilter()
    {
        delete filter;
        if(isClusteringProcessing()){
            delete processingFilter;
        }
    }

    bool BitFilter::isClusteringProcessing()
    {
        return (processingFilter != NULL);
    }

    void BitFilter::addKey(const key_type key)
    {
        if(T_executeThreshold != 0){
            filter->insert(key);
            T_lastRead = CurrentTimeStamp;
        }
    }

    void BitFilter::removeKey(const key_type key)
    {
        if(T_executeThreshold != 0){
            filter->erase(key);
            if(instance->isClusteringProcessing()){
                processingFilter->erase(key);
            }
        }
    }

    void BitFilter::reset()
    {
        instance->filter->clear();
        if(instance->isClusteringProcessing()){
            instance->processingFilter->clear();
        }
        instance->T_lastRead = CurrentTimeStamp;
    }

    void BitFilter::polling()
    {
        if(!instance->isClusteringProcessing()){
            if(((CurrentTimeStamp - instance->T_lastRead) > instance->T_executeThreshold) && (instance->filter->size() >= instance->sectorLog->subPagesPerPage)){
                std::list<key_type> subPagesToRead = std::list<key_type>(instance->filter->begin(), instance->filter->end());
                Stats2::addClusteringCount();
                instance->processingFilter = instance->filter;
                instance->filter = new std::set<key_type>();
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
        std::list<SubPageCluster*>* subPageClusterList = makeClusterList();

        if(subPageClusterList == NULL){
            endClustering();
        } else{
            remainWriteForClustering = subPageClusterList->size();
            sectorLog->sendSubPageWriteForClustering(*subPageClusterList);
            delete subPageClusterList;
        }

    }

    void BitFilter::endClustering()
    {
        T_lastRead = CurrentTimeStamp;
        delete processingFilter;
        processingFilter = NULL;
        
        sectorLog->handleWaitingReqsWhileClustering();
    }
}
