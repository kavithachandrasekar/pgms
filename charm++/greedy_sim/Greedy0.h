/*Distributed Graph Refinement Strategy*/
#ifndef _DISTLB_H_
#define _DISTLB_H_

#include <vector>
#include <unordered_map>

#define STANDALONE_DIFF
#include "ckgraph.h"
#include "BaseLB.h"
#include "CentralLB.h"
#include "Greedy0.decl.h"

typedef void (*callback_function)(void*);
class Greedy0 : public CBase_Greedy0 {
    Greedy0_SDAG_CODE
public:
    Greedy0(int nx, int ny);
    ~Greedy0();
    void AtSync(void);
    void work();
    void MaxLoad(double val);
    void AvgLoad(double val);
    void computeCommBytes();
    bool obj_delete_on_node(int node, int objId);

    int obj_node_map(int objId);

    void createObjList();
    int get_obj_idx(int objHandleId);
    std::vector<int>map_obj_id;
private:
    class ProcLoadGreater;
    class ObjLoadGreater;

    int stats_msg_count;
    int numNodes;
    int* pe_obj_count;
    std::vector<CkVertex> objects;
    std::vector<std::vector<int>> objectComms;
    std::vector<std::vector<int>> obj_to_pe_map;
    int neighborCount;
    int done;
    double* toSendLoad;
    double* toReceiveLoad;

//    int NX, NY;
    double avgLoadNeighbor;


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

