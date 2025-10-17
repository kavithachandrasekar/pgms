#ifndef _DIFFUSIONSIM_H_
#define _DIFFUSIONSIM_H_

#include "ckgraph.h"
#include "BaseLB.h"
#include "CentralLB.h"
#include "DiffusionSim.decl.h"
#include "../sim_headers/common_lbsim.h"

#define NUM_NEIGHBORS _lb_args.diffusionNumNbors()

/*readonly*/ int numNodes;
/*readonly*/ int numPes;

struct statsToPrint
{
    double internal;
    double external;
    double avgload;
    double maxload;
};

class Main : public CBase_Main
{
private:
    //  BaseLB::LDStats *statsData;
    obj_imb_funcptr obj_imb;
    char *output_filename;

    BaseLB::LDStats *globalStatsData;
    int stats_msg_count;

    statsToPrint statsBefore;
    statsToPrint statsAfter;

public:
    Main(CkArgMsg *m);
    void init();
    void checkStats(double *stats, int n);
    void collectMaxLoad(double load);

    void done();
};

class NodeCache : public CBase_NodeCache
{
public:
    BaseLB::LDStats *globalStatsData;
    NodeCache();
};

class DiffusionLB : public CBase_DiffusionLB
{
private:
    BaseLB::LDStats *nodeStats;
    NodeCache *myNodeCache;

    int myNodeId;
    int rank0PE;

    double my_load;
    double my_loadAfterTransfer;

    std::vector<CkVertex> objs;
    

    void setupLocalStats(BaseLB::LDStats *statsData);

    // for DiffusionNeighbors.C
    int round, requests_sent, pick;
    int acks;
    int max;
    std::unordered_map<int, double> cost_for_neighbor;
    std::vector<int> sendToNeighbors; // Neighbors to which curr node has to send load.
    int *holds;
    std::vector<int> mstVisitedPes;
    int rank0_barrier_counter;
    double best_weight;
    int best_from;
    int best_to;
    int all_tos_negative;
    bool visited;
    int *node_idx; // nbors;
    int nodeSize;
    int neighborCount;
    std::vector<std::vector<LBRealType>> allNodeCentroids;
    std::vector<int> allNodeObjCount;
    std::vector<double> allNodeDistances;
    std::vector<std::vector<LBRealType>> nborCentroids;
    std::vector<double> nborDistances;
    std::vector<int> nborObjCount;
    std::vector<LBRealType> myCentroid;
    int position_dim;
    int centReceiveNode;

    void resetVarsMST();

public:
    DiffusionLB_SDAG_CODE
    DiffusionLB();
    ~DiffusionLB();
    void reportMaxLoad();

     int LBwriteStatsMsgs(BaseLB::LDStats* statsData);

    void ReceiveFinalStats(std::vector<bool> isMigratable, std::vector<int> from_proc,
                         std::vector<int> to_proc, int n_migrateobjs,
                         std::vector<std::vector<LBRealType>> positions,
                         std::vector<double> load);

    // in DiffusionNeighbors.C
    void findNBors(int do_again);
    void createCommList();
    void next_phase(int val);
    void sortArr(long *arr, int n, int *nbors);
    void addNeighbor(int nbor);

    void beginMST();
    void buildMSTinRounds(double best_weight, int best_from, int best_to);
    void next_MSTphase(double newweight, int newparent, int newto);
    void startFirstRound();
    void findNBorsRound();
    void proposeNbor(int nborId);
    void askNbor(int nborId, int rnd);
    void okayNbor(int agree, int nborId);
    void ackNbor(int nbor);
    void pairedSort(int *A, std::vector<double> B);

    void startStrategy();
    void WithinNodeLB();
    void AcrossNodeLB();

    void BuildStats();
    // centroid list SDAG helpers
    void initializeCentroid();
    void processReceiveCentroid(int node, std::vector<LBRealType> centroid, int objCount);
    void finishCentroidList();

    int findNborIdx(int node);
    void PseudoLoadBalancing();
    void pseudolb_barrier(int allZero);


    int pseudo_itr;  // iteration count
    int temp_itr;
    bool pseudo_done;
    int loadReceivers;

    std::vector<double> toSendLoad;
    std::vector<double> toReceiveLoad;
    std::vector<double> loadNeighbors;
    double my_pseudo_load;

    int* gain_val;

    int GetPENumber(int& obj_id);
    void LoadMetaInfo(LDObjHandle objHandle, int objId, double load, int from_pe, int to_pe);
    void LoadReceived(int objId, int fromPE);
    int step();

          void ProcessMigrations();
  void ProcessFinalStats();

  double averagePE();

    int statsReceived;

      BaseLB::LDStats* fullStats;

   int migrates_expected;
     std::vector<double> pe_load;
       std::vector<double> objectLoads;

       std::vector<LDObjHandle> objectHandles;
  std::vector<int> objectSrcIds;
  std::vector<int> objSenderPEs;

    int total_migrates;

    int numPes;


    int FindObjectHandle(LDObjHandle h);
};

void computeCommBytes(BaseLB::LDStats *statsData, double &internal, double &external);
void computeLoad(BaseLB::LDStats *statsData, double &load);
void printStats(statsToPrint &stats);


#endif /* _DIFFUSIONSIM_H_ */
