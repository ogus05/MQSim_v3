#include <iostream>
#include <vector>
#include <string>

using namespace std;

int main(){
    vector<string> configFileNameList = {
        "ssdconfig_4K8KB.xml",
        "ssdconfig_4K16KB.xml"
    };

    vector<string> workloadFileNameList = {
        "workload_D.xml",
        "workload_B.xml",
        "workload_C.xml",
        "workload_A.xml",
        "workload_F.xml",
    };
    for(auto& configFileName : configFileNameList){
        for(auto& workloadFileName : workloadFileNameList){
            system((string("./MQSim -i ") + configFileName + string(" -w ") + workloadFileName).c_str());
        }
    }
}