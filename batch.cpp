#include <iostream>
#include <unistd.h>
#include <vector>

using namespace std;

int main(){
    vector<string> ssdconfig{
        "ssdconfig_16KB.xml",
        // "ssdconfig_4KB.xml",
        "ssdconfig_8KB.xml",
    };
    vector<string> workload{
        "workload_D.xml",
        "workload_C.xml",
        "workload_B.xml",
        "workload_A.xml",
        "workload_F.xml",
    };

    for(auto& wl : workload){
        for(auto& config : ssdconfig){
            system(string("./MQSim -i " + config + " -w " + wl + " << c").c_str());
        }
    }
}