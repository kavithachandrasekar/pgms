#include "DiffusionSim.h"
#include "DiffusionNeighbors.C"
#include "DiffusionMetric.C"
#include "DiffusionPseudo.C"

#include "DiffusionCore.C"

#include "../sim_headers/lbdump_jsontools.h"

#include "LBSimulation.h"


/*readonly*/ CProxy_Main mainProxy;
/*readonly*/ CProxy_NodeCache nodeCacheProxy;
/*readonly*/ CProxy_DiffusionLB diffusion_array;
/*readonly*/ std::string input_filename;

#define ITERATIONS 40


obj_imb_funcptr getImbalanceFunction(int fn_type)
{
    obj_imb_funcptr obj_imb;
    switch (fn_type)
    {
    case 1:
        // for 1/3 PEs, every object on that PE has load set to 3.5
        // on all other PEs, every object has load set to 1.0
        // TODO: what was the original load? its just trashed?
        obj_imb = (obj_imb_funcptr)load_imb_by_pe;
        break;
    case 2:
        // randomly multiply object load by 0.8 or 1.2 (50% chance) for all objects
        obj_imb = (obj_imb_funcptr)load_imb_by_history;
        break;
    case 3:
        // randomly inject load on 1 PE
        obj_imb = (obj_imb_funcptr)load_imb_rand_inject;
        break;
    case 4:
        // randomly multiply object load by 5 or 0.2 (50% chance) for all objects on two paired PEs (rand)
        obj_imb = (obj_imb_funcptr)load_imb_rand_pair;
        break;
    case 5:
        // all pe randomly increase or decrease by up to %20
        obj_imb = (obj_imb_funcptr)load_imb_all_on_pe;
        break;
    case 6:
        obj_imb = (obj_imb_funcptr)load_setconst;
        break;
    default:
        CkPrintf("No load imbalance injected\n");
        obj_imb = (obj_imb_funcptr)no_imb;
        break;
    }

    return obj_imb;
}
void readInputStats(const char *input_filename, BaseLB::LDStats *statsData, int &stats_msg_count)
{
    FILE *f = fopen(input_filename, "r");
    if (f == NULL)
        CkAbort("Fatal Error> Cannot open LB Dump file %s!\n", input_filename);

    bool isJson = false;
    const char* dot = strrchr(input_filename, '.');
    if (dot != nullptr && strcmp(dot, ".json") == 0) {
        isJson = true;
    }

    if (!isJson) {
        // this only works if file was pupped via CentralLB
        PUP::fromDisk pd(f);
        PUP::machineInfo machInfo;
        pd((char *)&machInfo, sizeof(machInfo));	// machine info

        pd|_lb_args.lbversion();		// write version number
        pd|stats_msg_count;
        statsData->pup(pd);
    }
    else
    {
        if (_lb_args.diffusionCommOn())
            CkAbort("Simulator doesn't work with JSON and comm yet. Use centroid, or generate initial data some other way.\n");
        read_from_json(f, statsData);
    }

    statsData->makeCommHash(); // set up the ldstats objHash, which maps LDObjKey to index in objData
}

Main::Main(CkArgMsg *m)
{
    mainProxy = thisProxy;
    if (m->argc != 4)
    {
        CkPrintf("Usage: ./Diffusion <load_imb_fn: 1,2,3,4>, <input_filename>, <output_filename>\n");
        CkExit();
    }

    // Selecting load imbalance function
    int fn_type = atoi(m->argv[1]);
    obj_imb = getImbalanceFunction(fn_type);

    input_filename = m->argv[2];
    output_filename = m->argv[4];

    globalStatsData = new BaseLB::LDStats();
    readInputStats(input_filename.c_str(), globalStatsData, stats_msg_count);
    numNodes = globalStatsData->n_nodes;

    CkPrintf("Global stats from %s parsed by Main: %d nodes and %d migratable objects \n", input_filename.c_str(), numNodes, globalStatsData->n_migrateobjs);

    nodeCacheProxy = CProxy_NodeCache::ckNew();
}

void Main::init()
{
    diffusion_array = CProxy_DiffusionLB::ckNew(numNodes);
}

void Main::collectMaxLoad(double load)
{
    statsBefore.maxload = load;
    printStats(statsBefore);
}

void Main::collectMaxLoadFinal(double load)
{
    statsAfter.maxload = load;
    printStats(statsAfter);
    CkExit();
}

void Main::finalStats(double *comm, int n) {
    double internalBytes = comm[0];
    double externalBytes = comm[1];
    double loadSum = comm[2];

    statsAfter.internal = internalBytes;
    statsAfter.external = externalBytes;
    statsAfter.avgload = loadSum / numNodes;

    diffusion_array.reportMaxLoad(true);
}


void Main::checkStats(double *comm, int n)
{
    double computedInternal = comm[0];
    double computedExternal = comm[1];
    double loadSum = comm[2];

    double internalBytes = 0.0;
    double externalBytes = 0.0;
    double load = 0.0;
    double maxLoad = 0.0;

    computeCommBytes(globalStatsData, internalBytes, externalBytes, true);
    computeLoad(globalStatsData, load);

    if (computedInternal - internalBytes > 1e-6 || computedExternal - externalBytes > 1e-6)
        CkAbort("Fatal Error> Global and locally computed bytes don't match: %f %f!\n", computedInternal, internalBytes);

    if (loadSum - load > 1e-6)
        CkAbort("Fatal Error> Global and locally computed load don't match: %f %f!\n", loadSum, load);

    statsBefore.internal = internalBytes;
    statsBefore.external = externalBytes;
    statsBefore.avgload = load / numNodes;

    diffusion_array.reportMaxLoad(false);
}

void printStats(statsToPrint &stats)
{
    CkPrintf("- Internal comm %f MB, External comm %f MB\n", stats.internal / (1024 * 1024), stats.external / (1024 * 1024));
    CkPrintf("- Average load %f\n", stats.avgload);
    CkPrintf("- Max load %f\n", stats.maxload);
}

void Main::done()
{
    CkExit();
}

NodeCache::NodeCache()
{
    globalStatsData = new BaseLB::LDStats();
    int stats_msg_count = 0;
    readInputStats(input_filename.c_str(), globalStatsData, stats_msg_count);

    CkPrintf("Global stats from %s parsed by NodeCache%d: %d nodes and %d migratable objects \n", input_filename.c_str(), thisIndex, numNodes, globalStatsData->n_migrateobjs);
    contribute(CkCallback(CkReductionTarget(Main, init), mainProxy));
}

DiffusionLB::~DiffusionLB()
{
#if CMK_LBDB_ON
  delete nodeStats;
  delete[] gain_val;
#endif
}

void DiffusionLB::setupLocalStats(BaseLB::LDStats *statsData)
{

    BaseLB::LDStats *globalStats = myNodeCache->globalStatsData;

    nodeStats->objData.clear();
    nodeStats->from_proc.clear();
    nodeStats->to_proc.clear();
    nodeStats->commData.clear();


    // get relevant object stats
    int nmigratable = 0;
    for (int obj = 0; obj < globalStats->objData.size(); obj++)
    {
        LDObjData &oData = globalStats->objData[obj];
        int pe = globalStats->from_proc[obj];

        if (pe == thisIndex)
        {
            // this is the local object
            nodeStats->objData.push_back(oData);
            nodeStats->from_proc.push_back(pe);
            nodeStats->to_proc.push_back(pe);

            if (oData.migratable)
                nmigratable++;
        }
    }
    nodeStats->n_migrateobjs = nmigratable;
    nodeStats->makeCommHash(); // set up the ldstats objHash, which maps LDObjKey to index in objData

    objs.clear();
    objs.resize(nodeStats->objData.size());

    for (int nobj = 0; nobj < nodeStats->objData.size(); nobj++)
    {
        LDObjData& oData = nodeStats->objData[nobj];
        objs[nobj] = CkVertex(nobj, oData.wallTime, nodeStats->objData[nobj].migratable,
                              nodeStats->from_proc[nobj]);
    }

    // get relevant comm stats
    for (int comm = 0; comm < globalStats->commData.size(); comm++)
    {
        LDCommData &commData = globalStats->commData[comm];
        if (!commData.from_proc() && commData.recv_type() == LD_OBJ_MSG)
        {
            LDObjKey from = commData.sender;
            LDObjKey to = commData.receiver.get_destObj();
            int fromobj = statsData->getHash(from); // this replaces the simulator get_obj_idx
            int toobj = statsData->getHash(to);

            if (fromobj == -1)
                continue;

            int fromnode = statsData->from_proc[fromobj];

            if (fromnode != thisIndex)
                continue;

            nodeStats->commData.push_back(commData);
        }
    }
}

void computeCommBytes(BaseLB::LDStats *statsData, double &internal, double &external, bool before)
{

    for (int edge = 0; edge < statsData->commData.size(); edge++)
    {
        LDCommData &commData = statsData->commData[edge];
        if (!commData.from_proc() && commData.recv_type() == LD_OBJ_MSG)
        {
            LDObjKey from = commData.sender;
            LDObjKey to = commData.receiver.get_destObj();
            int fromobj = statsData->getHash(from); // this replaces the simulator get_obj_idx
            int toobj = statsData->getHash(to);

            if (fromobj == -1)
                CkAbort("Fatal Error> Cannot find fromobj which I should own!");

            int fromNode = before ? statsData->from_proc[fromobj] : statsData->to_proc[fromobj];

            int toNode = -1;
            if (toobj != -1)
                toNode = before ? statsData->from_proc[toobj] : statsData->to_proc[toobj];

            // note: neither fromobj nor toobj should be -1 if this is done on global stats

            // store internal bytes in the last index pos ? -q
            if (fromNode == toNode)
                internal += commData.bytes;
            else
                external += commData.bytes;
        }
    }
}

void computeLoad(BaseLB::LDStats *statsData, double &load)
{
    for (int obj = 0; obj < statsData->objData.size(); obj++)
    {
        LDObjData &oData = statsData->objData[obj];
        int pe = statsData->from_proc[obj];

        // this is the local object
        load += oData.wallTime;
    }
}

void DiffusionLB::reportMaxLoad(bool final)
{
    if (final)
    {
        contribute(sizeof(double), &my_loadAfterTransfer, CkReduction::max_double, CkCallback(CkReductionTarget(Main, collectMaxLoadFinal), mainProxy));
    }
    else
    {
        contribute(sizeof(double), &my_loadAfterTransfer, CkReduction::max_double, CkCallback(CkReductionTarget(Main, collectMaxLoad), mainProxy));
    }
}

DiffusionLB::DiffusionLB()
{
    myNodeCache = nodeCacheProxy.ckLocalBranch();
    nodeStats = new BaseLB::LDStats();

    setupLocalStats(nodeStats);

    double internalBytes = 0.0;
    double externalBytes = 0.0;
    double load = 0.0;
    computeCommBytes(nodeStats, internalBytes, externalBytes, true);
    computeLoad(nodeStats, load);

    my_load = load;
    my_loadAfterTransfer = my_load;


    // setup for DiffusionNeighbors.C
    round = 0;
    nodeSize = 1;
    myNodeId = thisIndex;
    acks = 0;
    max = 0;
    round = 0;
    rank0_barrier_counter = 0;

    myNodeId = thisIndex;
    rank0PE = thisIndex;
    nodeSize = 1;
    statsReceived = 0;
    total_migrates = 0;

    numPes = numNodes;
    CkPrintf("DiffusionLB on PE %d: numPes = %d\n", thisIndex, numPes);

    if (myNodeId == 0)
    {
        fullStats = new BaseLB::LDStats(CkNumPes());
        
     }
     
     pe_load.resize(nodeSize);

    CkCallback cs(CkReductionTarget(Main, checkStats), mainProxy);
    double comm[3];
    comm[0] = (double)internalBytes;
    comm[1] = (double)externalBytes;
    comm[2] = (double)load;
    contribute(sizeof(double) * 3, comm, CkReduction::sum_double, cs);

    // stats collection can happen concurrently with LB
    thisProxy[thisIndex].findNBors(0);
}

void DiffusionLB::WithinNodeLB()
{
    if (thisIndex == 0)
    if (_lb_args.debug()) CkPrintf("--------STARTING WITHIN NODE LB--------\n");

    if(nodeSize==1) {
      if (_lb_args.debug()) CkPrintf("--------Node size is 1--------\n");

    if (thisIndex == 0)
    {
      if (step() == LBSimulation::dumpStep)
      {
        CkCallback cb(CkIndex_DiffusionLB::ProcessFinalStats(), thisProxy);
         CkStartQD(cb);
      }
      else
      {
        CkCallback cb(CkIndex_DiffusionLB::ProcessMigrations(), thisProxy);
        CkStartQD(cb);
      }
    }
   
  } else {
    CkAbort("nodesize should always be 1 in simulator");
  }

}

void DiffusionLB::pairedSort(int *A, std::vector<double> B)
{
    // sort array A based on corresponding values in B (both of size n)
    int n = B.size();
    std::vector<std::pair<long, int>> vp;
    for (int i = 0; i < n; ++i)
    {
        vp.push_back(std::make_pair(B[i], A[i]));
    }

    sort(vp.begin(), vp.end());

    // convert A back to array
    for (int i = 0; i < n; ++i)
    {
        A[i] = vp[i].second;
    }
}

int DiffusionLB::GetPENumber(int& obj_id)
{
    return 0;
}

void DiffusionLB::LoadReceived(int objId, int from0PE)
{
    total_migrates++;
}

int DiffusionLB::step() {
    return LBSimulation::dumpStep;
}

void DiffusionLB::ProcessMigrations()
{
    double internalBytes = 0.0;
    double externalBytes = 0.0;
   computeCommBytes(nodeStats, internalBytes, externalBytes, false);

   CkCallback cs(CkReductionTarget(Main, finalStats), mainProxy);
    double comm[3];
    comm[0] = (double)internalBytes;
    comm[1] = (double)externalBytes;
    comm[2] = (double)my_loadAfterTransfer;
    contribute(sizeof(double) * 3, comm, CkReduction::sum_double, cs);
}

#include "DiffusionSim.def.h"
