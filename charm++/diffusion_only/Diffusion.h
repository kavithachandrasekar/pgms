/*Distributed Graph Refinement Strategy*/
#ifndef _DISTLB_H_
#define _DISTLB_H_

#include <vector>
#include <unordered_map>

#define STANDALONE_DIFF
#include "ckgraph.h"
#include "BaseLB.h"
#include "CentralLB.h"
#include "Diffusion.decl.h"

typedef void (*callback_function)(void*);
class Diffusion : public CBase_Diffusion {
    Diffusion_SDAG_CODE
public:
    Diffusion(int nx, int ny);
    ~Diffusion();
    void AtSync(void);
    void setNeighbors(std::vector<int> neighbors, int neighborCount, double load);
    void startDiffusion();
    void LoadReceived(int objId, int fromPE);
    void MaxLoad(double val);
    void AvgLoad(double val);
    void findNBors(int do_again);
    void pick3DNbors();
    void proposeNbor(int nborId); 
    void okayNbor(int agree, int nborId);
    void passPtrs(double *loadNbors, double *toSendLd,
                              double *toRecvLd, void (*func)(void*), void* obj);
    void computeCommBytes();

    int obj_node_map(int objId);

    void createObjs();
    void createObjList();
    void createObjGraph();
    bool obj_on_node(int objId);
    void LoadBalancing();
    int get_obj_idx(int objHandleId);
    std::vector<int>map_obj_id;
private:
    // aggregate load received
    int itr; // iteration count
    int temp_itr;
    int done;
    int statsReceived;
    int loadReceived;
    int round;
    int requests_sent;
    int stats_msg_count;
    int numNodes;
    int received_nodes;
    long internalBytes, externalBytes;
    int* pe_obj_count;
//    std::vector<double> *loadNeighbors;
    double *loadNeighbors;
    std::vector<int> sendToNeighbors; // Neighbors to which curr node has to send load.
    std::vector<CkVertex> objects;
    std::vector<std::vector<int>> link_dest;
    std::vector<std::vector<long>> link_bytes;
    std::vector<std::vector<int>> objectComms;
    std::vector<std::vector<int>> obj_to_pe_map;
    int neighborCount;
    double* toSendLoad;
    double* toReceiveLoad;

//    int NX, NY;
    double avgLoadNeighbor;

    // heap
    int* obj_arr;
    int* gain_val;
    std::vector<CkVertex> resdnt_objs;
    std::vector<std::tuple<int, int, double>> immig_objs;
    std::vector<int>pe_immig_assignment_map;
    std::vector<int> obj_heap;
    std::vector<int> heap_pos;


    callback_function cb;
    void* objPtr;

    bool AggregateToSend();
    double  average();
    double averagePE();
    int findNborIdx(int node);
    void PseudoLoadBalancing();
    void InitializeObjHeap(int* obj_arr, int n, int* gain_val);
public:
    BaseLB::LDStats *statsData;
    double my_load;
    double my_loadAfterTransfer;
};

#endif /* _DistributedLB_H_ */

