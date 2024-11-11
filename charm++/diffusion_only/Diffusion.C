/** \file Diffusion.C
 *  Authors: Monika G
 *           Kavitha C
 *
 */

/**
 *  1. Each node has a list of neighbors (bi-directional) (2d nbors here - simplified case)
 *  2. Over multiple iterations, each node diffuses load to neighbor nodes
 *     by only passing load values
 */
#include "Diffusion.h"
#include "../sim_headers/common_lbsim.h"

#include "Heap_helper.C"
#define DEBUGF(x) CmiPrintf x;
#define DEBUGL(x) /*CmiPrintf x*/ ;
#define DEBUGL2(x) /*CmiPrintf x*/ ;
#define DEBUGE(x) CmiPrintf x;

#define NUM_NEIGHBORS 4

#define ITERATIONS 80

#define SELF_IDX NUM_NEIGHBORS
#define EXT_IDX NUM_NEIGHBORS + 1

#define THRESHOLD 2

#define getNodeId(x, y, NY) x *NY + y
#define getX(node) (int)floor(node / NY)
#define getY(node) node % NY

#define BYTES 512
#define SIZE 1000

#include "Neighbor_list.C"

using std::vector;

#ifdef STANDALONE_DIFF
/*readonly*/ CProxy_Main mainProxy;
/*readonly*/ CProxy_Diffusion diff_array;
/*readonly*/ CProxy_GlobalMap nodeGroupProxy;

GlobalMap::GlobalMap()
{
  statsData = new BaseLB::LDStats;
}

class Main : public CBase_Main
{
  obj_imb_funcptr obj_imb;
  int numNodes;
  int stats_msg_count;

  std::vector<int> map_obid_pe;
  std::vector<int> map_obj_id;
  std::vector<std::vector<LBRealType>> map_pe_centroid;

  double start_time;
  int iter = 0;

  GlobalMap *nodeGroup;
  char *output_filename;

public:
  Main(CkArgMsg *m)
  {
    mainProxy = thisProxy;
    if (m->argc != 5)
    {
      CkPrintf("Usage: ./Diffusion <load_imb_fn: 1,2,3,4>, <filename>, <comm/centroid: 1,2>, <output filename>\n");
      CkExit();
    }
    int fn_type = atoi(m->argv[1]);

    // injecting load imbalance
    if (fn_type == 1)
      // for 1/3 PEs, every object on that PE has load set to 3.5
      // on all other PEs, every object has load set to 1.0
      // TODO: what was the original load? its just trashed?
      obj_imb = (obj_imb_funcptr)load_imb_by_pe;
    else if (fn_type == 2)
      //  randomly multiply object load by 0.8 or 1.2 (50% chance) for all objects
      obj_imb = (obj_imb_funcptr)load_imb_by_history;
    else if (fn_type == 3)
      // randomly inject load on 1 PE
      obj_imb = (obj_imb_funcptr)load_imb_rand_inject;
    else if (fn_type == 4)
      // randomly multiply object load by 5 or 0.2 (50% chance) for all objects on two paired PEs (rand)
      obj_imb = (obj_imb_funcptr)load_imb_rand_pair;
    else
    {
      CkPrintf("No load imbalance injected\n");
      obj_imb = (obj_imb_funcptr)no_imb;
    }

    centroid = atoi(m->argv[3]) == 2;
    output_filename = m->argv[4];

    const char *filename = m->argv[2];
    int i;
    FILE *f = fopen(filename, "r");
    if (f == NULL)
    {
      CkAbort("Fatal Error> Cannot open LB Dump file %s!\n", filename);
    }

    start_time = CkWallTimer();
    nodeGroupProxy = CProxy_GlobalMap::ckNew();

    nodeGroup = nodeGroupProxy.ckLocalBranch();
    BaseLB::LDStats *statsDatax = nodeGroup->statsData;

    statsDatax->objData.reserve(SIZE);
    statsDatax->from_proc.reserve(SIZE);
    statsDatax->to_proc.reserve(SIZE);
    statsDatax->commData.reserve(SIZE);

    PUP::fromDisk pd(f);
    PUP::machineInfo machInfo;

    pd((char *)&machInfo, sizeof(machInfo)); // read machine info
    PUP::xlater p(machInfo, pd);

    if (_lb_args.lbversion() > 1)
    {
      p | _lb_args.lbversion(); // write version number
      CkPrintf("LB> File version detected: %d\n", _lb_args.lbversion());
      CmiAssert(_lb_args.lbversion() <= LB_FORMAT_VERSION);
    }
    p | stats_msg_count;

    CmiPrintf("readStatsMsgs for %d pes starts ... \n", stats_msg_count);

    statsDatax->pup(p);

    CmiPrintf("n_obj: %zu n_migratable: %d \n", statsDatax->objData.size(), statsDatax->n_migrateobjs);

    // file f is closed in the destructor of PUP::fromDisk
    CmiPrintf("ReadStatsMsg from %s completed\n", filename);

    int nmigobj = 0;

    for (i = 0; i < statsDatax->objData.size(); i++)
    {
      if (statsDatax->objData[i].migratable)
        nmigobj++;
    }
    statsDatax->n_migrateobjs = nmigobj;

    // Generate a hash with key object id, value index in objs vector
    statsDatax->deleteCommHash();
    statsDatax->makeCommHash();
    numNodes = statsDatax->procs.size();
    // statsData->print();
    // create one diffusion obj per "node" = PE
    thisProxy.init();
  }
  void init()
  {
    // initiating algorithm, iterating over "nodes"

    // TODO: setting statsData for all diffusion objects (should be done in Diffusion)
    // for (int i = 0; i < numNodes; i++)
    // {
    //   Diffusion *diff_obj = diff_array(i).ckLocal();

    //   if (diff_obj == NULL)
    //   {
    //     CkPrintf("ERROR: diff obj is not local\n");
    //     CkExit();
    //   }
    //   diff_obj->statsData = statsData; // all diffusion objects get the same statsData
    // }

    // TODO: setting object mapping on diff_obj0 (should be done on all)
    BaseLB::LDStats *statsData = nodeGroup->statsData;
    map_obid_pe = std::vector<int>(statsData->objData.size(), 0);
    map_obj_id = std::vector<int>(statsData->objData.size(), 0);
    map_pe_centroid = std::vector<std::vector<LBRealType>>(numNodes, std::vector<LBRealType>(3, 0.0));

    // injecting load imbalance
    obj_imb(statsData);

    // set up initial maps
    map_obj_id.reserve(statsData->objData.size());
    map_obid_pe.reserve(statsData->objData.size());

    // centroid relevant variables
    int positionDim = statsData->objData[0].position.size();
    std::vector<std::vector<LBRealType>> pe_centroids(numNodes, std::vector<LBRealType>(positionDim, 0.0));
    std::vector<int> pe_obj_count(numNodes, 0);

    for (int obj = 0; obj < statsData->objData.size(); obj++)
    {
      // compute node-aggregate centroids

      int obj_pe = statsData->from_proc[obj];
      std::vector<LBRealType> obj_pos = statsData->objData[obj].position;
      for (int comp = 0; comp < positionDim; comp++)
        pe_centroids[obj_pe][comp] += obj_pos[comp];
      pe_obj_count[obj_pe]++;

      LDObjData &oData = statsData->objData[obj];
      if (!oData.migratable)
        continue;
      // CkPrintf("\nSimNode-%d Adding %dth = %d on PE-%d", 0, obj, oData.objID(), statsData->from_proc[obj]);

      map_obj_id[obj] = oData.objID();
      map_obid_pe[obj] = obj_pe;
    }

    for (int i = 0; i < numNodes; i++)
    {
      for (int comp = 0; comp < positionDim; comp++)
      {
        pe_centroids[i][comp] /= pe_obj_count[i];
      }
    }

    map_pe_centroid = pe_centroids;

    if (centroid)
    {
      CkPrintf("Using CENTROID approach\n");
      // finding aggregate centroids
    }
    else
    {
      CkPrintf("Using COMM approach\n");
    }

    GlobalMap *maps = nodeGroup;

    maps->map_obj_id = map_obj_id;
    maps->map_obid_pe = map_obid_pe;
    maps->map_pe_centroid = map_pe_centroid;

    diff_array = CProxy_Diffusion::ckNew(numNodes, map_obj_id, map_obid_pe, map_pe_centroid, numNodes);
    diff_array.AtSync();
  }

  void done()
  {
    BaseLB::LDStats *statsData = nodeGroup->statsData;
    for (int obj = 0; obj < statsData->objData.size(); obj++)
    {
      if (!statsData->objData[obj].migratable)
        continue;

      statsData->from_proc[obj] = nodeGroup->map_obid_pe[obj]; // map_obid_pe[obj];
    }
    const char *filename = output_filename;
    FILE *f = fopen(filename, "w");
    if (f == NULL)
    {
      CkAbort("Fatal Error> writeStatsMsgs failed to open the output file %s!\n", filename);
    }
    const PUP::machineInfo &machInfo = PUP::machineInfo::current();
    PUP::toDisk p(f);
    p((char *)&machInfo, sizeof(machInfo)); // machine info

    p | _lb_args.lbversion(); // write version number
    p | stats_msg_count;
    statsData->pup(p);

    fclose(f);

    CmiPrintf("WriteStatsMsgs to %s succeed!\n", filename);

    CkPrintf("DONE\n");
    fflush(stdout);
    CkPrintf("Total time taken = %lf\n", CkWallTimer() - start_time);
    CkExit(0);
  }
};
#endif

Diffusion::Diffusion(int node_count, std::vector<int> obj_id, std::vector<int> obid_pe, std::vector<std::vector<LBRealType>> pe_centroid)
{
  setMigratable(false);
  done = -1;
  round = 0;
  itr = 0;
  numNodes = node_count;
  notif = 0;
  finished = false;
  edgeCount = 0;
  edge_indices.reserve(100);

  map_obj_id = obj_id;
  map_obid_pe = obid_pe;
  map_pe_centroid = pe_centroid;

  nodeGroup = nodeGroupProxy.ckLocalBranch();
  statsData = nodeGroup->statsData;

  // setting up initial communcation
  for (int edge = 0; edge < statsData->commData.size(); edge++)
  {
    LDCommData &commData = statsData->commData[edge];
    if ((!commData.from_proc()) && (commData.recv_type() == LD_OBJ_MSG))
    {
      LDObjKey from = commData.sender;
      int fromNode = map_obid_pe[(get_obj_idx(from.objID()))];
      edgeCount++;
      edge_indices.push_back(edge);
    }
  }
}

Diffusion::~Diffusion() {}

void Diffusion::AtSync()
{
  contribute(CkCallback(CkReductionTarget(Diffusion, createObjs), thisProxy));
}

void Diffusion::createObjs()
{
  // CkPrintf("\n[SimNode#%d] createObjs", thisIndex);
  createObjList();

  // before LB statistics
  CkCallback cbm(CkReductionTarget(Diffusion, MaxLoad), thisProxy(0));
  contribute(sizeof(double), &my_load, CkReduction::max_double, cbm);
  CkCallback cba(CkReductionTarget(Diffusion, AvgLoad), thisProxy);
  contribute(sizeof(double), &my_load, CkReduction::sum_double, cba);

  sendToNeighbors.reserve(100); // NUM_NEIGHBORS);
  sendToNeighbors.clear();

  int do_again = 1;

  CkCallback cb(CkReductionTarget(Diffusion, findNBors), thisProxy);
  contribute(sizeof(int), &do_again, CkReduction::max_int, cb);
}

void Diffusion::passPtrs(double *loadNbors, double *toSendLd,
                         double *toRecvLd, void (*func)(void *), void *obj)
{
  loadNeighbors = loadNbors;
  toSendLoad = toSendLd;
  toReceiveLoad = toRecvLd;
  cb = func;
  objPtr = obj;
}

void Diffusion::setNeighbors(std::vector<int> nbors, int nCount, double load)
{
  neighborCount = nCount;
  for (int i = 0; i < neighborCount; i++)
  {
    sendToNeighbors.push_back(nbors[i]);
    toSendLoad[i] = 0.0;
    toReceiveLoad[i] = 0.0;
  }
  my_load = load;

  CkCallback cb(CkIndex_Diffusion::startDiffusion(), thisProxy);
  contribute(cb);
}

void Diffusion::createObjList()
{
  my_load = 0.0;
  int start_node_obj_idx = 0; // this should be taken from map in stencil3d

  int total_objs = statsData->objData.size();

  for (int obj = 0; obj < statsData->objData.size(); obj++)
  {
    LDObjData &oData = statsData->objData[obj];
    int pe = statsData->from_proc[obj];
    if (pe != thisIndex)
      continue;
    if (!oData.migratable)
    {
      if (!statsData->procs[pe].available)
        CmiAbort("Greedy0LB cannot handle nonmigratable object on an unavial processor!\n");
      continue;
    }
    double load = statsData->objData[obj].wallTime;
    objects.push_back(CkVertex(oData.handle.objID(), load, statsData->objData[obj].migratable, pe));
    my_load += load;
  }

  my_load_after_transfer = my_load;
  //  CkPrintf("\n[SimNode-%d] my_load Before Transfer = %lf\n", thisIndex,my_load_after_transfer);
  //  CkPrintf("\nThe number of objects on this node(#%d) = %d", thisIndex, nobj);
  /*
    for(int nobj = 0; nobj < (int)(my_load); nobj++) {
      objects[nobj] = CkVertex(nobj, 1.0, 1, 0);//oData.wallTime, statsData->objData[nobj].migratable, statsData->from_proc[nobj]);
    }
  */
}

bool Diffusion::obj_on_node(int objId)
{

  if (thisIndex == nodeGroup->map_obid_pe[objId])
    return true;
  return false;
}

std::vector<LBRealType> Diffusion::getCentroid(int pe)
{
  return map_pe_centroid[pe];
}

int Diffusion::get_obj_idx(int objHandleId)
{
  //  CkPrintf("\nAsking for %d", objHandleId);
  for (int i = 0; i < statsData->objData.size(); i++)
  {
    //    CkPrintf("\nPrinting[%d] = %d", i, diff0->map_obj_id[i]);
    if (nodeGroup->map_obj_id[i] == objHandleId)
    {
      return i;
    }
  }
  return -1;
}

int Diffusion::obj_node_map(int objId)
{

  return nodeGroup->map_obid_pe[objId];
}

void Diffusion::startDiffusion()
{
  for (int i = 0; i < neighborCount; i++)
  {
    // CkPrintf("\nMy[Node-%d] final neighbor[%d] = %d", thisIndex, i, sendToNeighbors[i]);
    toSendLoad[i] = 0.0;
    toReceiveLoad[i] = 0.0;
  }
  thisProxy[thisIndex].iterate();
}

int Diffusion::findNborIdx(int node)
{
  for (int i = 0; i < neighborCount; i++)
    if (sendToNeighbors[i] == node)
      return i;
  //  for(int i=0;i<neighborCount;i++)
  //  DEBUGE(("\n[%d]Couldnt find node %d in %d", thisIndex, node, sendToNeighbors[i]));
  //  CkExit(0);
  return -1;
}

double Diffusion::average()
{
  double sum = 0;
  for (int i = 0; i < neighborCount; i++)
  {
    sum += loadNeighbors[i];
  }
  // TODO: check the value
  return (sum / neighborCount);
}

bool Diffusion::AggregateToSend()
{
  bool res = false;
  for (int i = 0; i < neighborCount; i++)
  {
    toSendLoad[i] -= toReceiveLoad[i];
    if (toSendLoad[i] > 0)
      res = true;
  }
  return res;
}

void Diffusion::finishLB()
{
  finished = true;
  my_load = my_load_after_transfer;
  //  CkPrintf("\nNode-%d, my load = %lf", thisIndex, my_load_after_transfer);

  CkCallback cbm(CkReductionTarget(Diffusion, MaxLoad), thisProxy(0));
  contribute(sizeof(double), &my_load_after_transfer, CkReduction::max_double, cbm);
}

void Diffusion::updateLoad(double update)
{
  my_load_after_transfer += update;
}

void Diffusion::MaxLoad(double val)
{

  if (finished)
  {
    computeCommBytes(statsData, this, 0);
    computeSpread(statsData, this, 0);
  }
  DEBUGF(("[Iter: %d] Max PE load = %lf\n", itr, val));
  fflush(stdout);
  if (finished)
  {
    CkPrintf("-----------------------------------------------\n");
    mainProxy.done();
  }
}

void Diffusion::AvgLoad(double val)
{

  done++;
  if (thisIndex == 0)
    DEBUGF(("[%d]Avg Node load = %lf\n", done, val / numNodes));
#ifdef STANDALONE_DIFF
  //  CkPrintf("\n[SimNode#%d done=%d sending to %d nodes",thisIndex,done, numNodes);
  if (done == 1)
  {
    if (thisIndex == 0)
    {
      CkPrintf("-----------------------------------------------\n");
      computeCommBytes(statsData, this, 1);
      computeSpread(statsData, this, 1);
      CkPrintf("-----------------------------------------------\n");

      if (centroid)
        thisProxy.LoadBalancingCentroids();
      else
        thisProxy.LoadBalancing();
    }
  }
#else
  //    CkPrintf("\nCalling Obj potr %d", CkMyPe());
  cb(objPtr);
#endif
}

void Diffusion::PseudoLoadBalancing()
{
  std::string nbor_nodes_load = " ";
  for (int i = 0; i < neighborCount; i++)
  {
    nbor_nodes_load += " node-" + std::to_string(sendToNeighbors[i]) + "'s load= " + std::to_string(loadNeighbors[i]);
  }
  DEBUGL2(("[PE-%d, Node-%d] Pseudo Load Balancing , iteration %d my_load %f my_load_after_transfer %f avgLoadNeighbor %f (split = %s)\n", CkMyPe(), CkMyNode(), itr, my_load, my_load_after_transfer, avgLoadNeighbor, nbor_nodes_load.c_str()));
  double threshold = THRESHOLD * avgLoadNeighbor / 100.0;

  avgLoadNeighbor = (avgLoadNeighbor + my_load) / 2;
  double totalOverload = my_load - avgLoadNeighbor;
  double totalUnderLoad = 0.0;
  double thisIterToSend[neighborCount];
  for (int i = 0; i < neighborCount; i++)
    thisIterToSend[i] = 0.0;
  if (totalOverload > 0)
    for (int i = 0; i < neighborCount; i++)
    {
      if (loadNeighbors[i] < (avgLoadNeighbor - threshold))
      {
        thisIterToSend[i] = avgLoadNeighbor - loadNeighbors[i];
        totalUnderLoad += avgLoadNeighbor - loadNeighbors[i];
        //        DEBUGL2(("[PE-%d] iteration %d thisIterToSend %f avgLoadNeighbor %f loadNeighbors[%d] %f to node %d\n",
        //                thisIndex, itr, thisIterToSend[i], avgLoadNeighbor, i, loadNeighbors[i], sendToNeighbors[i]));
      }
    }
  if (totalUnderLoad > 0 && totalOverload > 0 && totalUnderLoad > totalOverload)
    totalOverload += threshold;
  else
    totalOverload = totalUnderLoad;
  DEBUGL2(("[%d] GRD: Pseudo Load Balancing Sending, iteration %d totalUndeload %f totalOverLoad %f my_load_after_transfer %f\n", CkMyPe(), itr, totalUnderLoad, totalOverload, my_load_after_transfer));
  for (int i = 0; i < neighborCount; i++)
  {
    if (totalOverload > 0 && totalUnderLoad > 0 && thisIterToSend[i] > 0)
    {
      //      DEBUGL2(("[%d] GRD: Pseudo Load Balancing Sending, iteration %d node %d(pe-%d) toSend %lf totalToSend %lf\n", CkMyPe(), itr, sendToNeighbors[i], CkNodeFirst(sendToNeighbors[i]), thisIterToSend[i], (thisIterToSend[i]*totalOverload)/totalUnderLoad));
      thisIterToSend[i] *= totalOverload / totalUnderLoad;
      toSendLoad[i] += thisIterToSend[i];
      DEBUGL2(("[Node-%d](my load = %lf-%lf) iteration %d thisIterToSend %f (total send %lf)  avgLoadNeighbor %f loadNeighbors[%d] %f to node %d\n",
               thisIndex, my_load, thisIterToSend[i], itr, thisIterToSend[i], toSendLoad[i], avgLoadNeighbor, i, loadNeighbors[i], sendToNeighbors[i]));
      if (my_load - thisIterToSend[i] < 0)
        CkAbort("Error: my_load (%f) - thisIterToSend[i] (%f) < 0\n", my_load, thisIterToSend[i]);
      my_load -= thisIterToSend[i];
    }
    if (thisIterToSend[i] < 0.0)
      thisIterToSend[i] = 0.0;
    int nbor_node = sendToNeighbors[i];
    thisProxy(nbor_node).PseudoLoad(itr, thisIterToSend[i], thisIndex);
  }
}

void Diffusion::LoadBalancing()
{
  //  Iterate over the comm data and for each object, store its comm bytes
  //  to other neighbor nodes and own node.

  // objectComms maintains the comm bytes for each object on this node
  // with the neighboring node
  // we also maintain comm within this node and comm bytes outside
  //(of this node and neighboring nodes)

  int n_objs = objects.size();
  gain_val = new int[n_objs];
  std::vector<std::vector<int>> objectComms(n_objs, std::vector<int>(NUM_NEIGHBORS + 2, 0));

  for (int edge = 0; edge < edge_indices.size(); edge++)
  {

    LDCommData &commData = statsData->commData[edge_indices[edge]];
    if ((!commData.from_proc()) && (commData.recv_type() == LD_OBJ_MSG))
    {
      LDObjKey from = commData.sender;
      LDObjKey to = commData.receiver.get_destObj();

      int fromNode = thisIndex; // Node = chare here so using thisIndex
      int toNode = map_obid_pe[(get_obj_idx(to.objID()))];

      // store internal bytes in the last index pos ? -q
      if (fromNode == toNode)
      {
        // internal comm
        int nborIdx = SELF_IDX;
        int fromObj = statsData->getHash(from);
        int toObj = statsData->getHash(to);

        if (fromObj != -1 && fromObj < n_objs)
          objectComms[fromObj][nborIdx] += commData.bytes;

        // lastKnown PE value can be wrong.
        if (toObj != -1 && toObj < n_objs)
          objectComms[toObj][nborIdx] += commData.bytes;
      }
      else
      { // External communication
        int nborIdx = findNborIdx(toNode);
        if (nborIdx == -1)
          nborIdx = EXT_IDX; // Store in last index if it is external bytes going to non-immediate neighbors
        int fromObj = statsData->getHash(from);
        // CkPrintf("[%d] GRD Load Balancing from obj %d and pos %d\n", CkMyPe(), fromObj, nborIdx);
        if (fromObj != -1 && fromObj < n_objs)
          objectComms[fromObj][nborIdx] += commData.bytes;
      }
    }
  } // end for

  // calculate the gain value, initialize the heap.
  double threshold = THRESHOLD * avgLoadNeighbor / 100.0;

  // if (thisIndex == 0)
  //   DEBUGL(("\nIterating through toSendLoad of size %lu", neighborCount));

  // if (n_objs != objectComms.size())
  //   DEBUGL(("\nError %d!=%d", n_objs, objectComms.size()));

  obj_arr = new int[n_objs];

  for (int i = 0; i < n_objs; i++)
  {
    int sum_bytes = 0;
    // comm bytes with all neighbors
    // if(i > objectComms.size()-1) continue;
    //    vector<int> comm_w_nbors = objectComms[i];
    // compute the sume of bytes of all comms for this obj

    sum_bytes = std::accumulate(objectComms[i].begin(), objectComms[i].end(), 0);

    // This gives higher gain value to objects that have more within node communication
    gain_val[i] = 2 * objectComms[i][SELF_IDX] - sum_bytes;
  }

  // T1: create a heap based on gain values, and its position also.

  //  objs.resize(n_objs);

  // Creating a minheap of objects based on gain value
  // InitializeObjHeap(obj_arr, n_objs, gain_val); // TODO: replace

  // For sorting: make pairs of object id and gain value
  std::vector<std::pair<double, int>> obj_gain_pairs(n_objs);
  for (int i = 0; i < n_objs; i++)
  {
    obj_gain_pairs[i] = std::make_pair(gain_val[i], i);

    int objHandle = objects[i].getVertexId();
    int obj_global_idx = get_obj_idx(objHandle);
    if (map_obid_pe[obj_global_idx] != thisIndex)
      CkPrintf("Error creating obj_gain_pairs: Object not on this node %d: local_id = %d, actual node = %d, global_idx = %d\n", thisIndex, i, map_obid_pe[obj_global_idx], obj_global_idx);
  }

  // SORT: sort the objects based on gain value (in decreasing order)
  std::sort(obj_gain_pairs.begin(), obj_gain_pairs.end(), std::greater<std::pair<double, int>>());

  // T2: Actual load balancingDecide which node it should go, based on object comm data structure. Let node be n
  int v_id;
  double totalSent = 0;
  int counter = 0;
  int migrated_obj_count = 0;
  int n_count = 0;

  while (my_load_after_transfer > 0.0)
  {
    if (obj_gain_pairs.empty())
    {
      break;
    }
    // v_id = heap_pop(obj_heap, ObjCompareOperator(&objects, gain_val), heap_pos);
    //  get item with highest gain (local id in v_id)
    auto front = obj_gain_pairs.front();
    obj_gain_pairs.erase(obj_gain_pairs.begin());

    v_id = front.second;
    int obj_gain = front.first;

    /*If the heap becomes empty*/
    // if (v_id == -1)
    // { // objects.size()){//v_id==-1) {
    //   DEBUGL(("\n On SimNode-%d, empty heap", thisIndex));
    //   break;
    // }

    int objHandle = objects[v_id].getVertexId();
    int obj_global_idx = get_obj_idx(objHandle);
    if (map_obid_pe[obj_global_idx] != thisIndex)
      CkAbort("Error: Object not on this node %d: local_id = %d, actual node = %d, global_idx = %d\n", thisIndex, v_id, map_obid_pe[obj_global_idx], obj_global_idx);

    if (!objects[v_id].isMigratable())
      CkAbort("Object in objects list must be migratable: local obj %d on pe %d\n", v_id, thisIndex);

    double currLoad = objects[v_id].getVertexLoad();

    // DEBUGL(("\n[PE-%d] object id = %d, load = %lf", thisIndex, v_id, currLoad));
    vector<int> comm = objectComms[v_id];
    int maxComm = 0;
    int maxi = -1; // index of neighbor to send to

    // sort neighbors based on toSendLoad
    vector<int> V(neighborCount);
    std::iota(V.begin(), V.end(), 0); // Initializing
    sort(V.begin(), V.end(), [&](int i, int j)
         { return toSendLoad[i] > toSendLoad[j]; });

    // TODO: Get the object vs communication cost ratio and work accordingly. (?)
    for (int i = 0; i < neighborCount; i++)
    {
      int l = V[i];
      // TODO: if not underloaded continue
      if (toSendLoad[l] > 0.0 && currLoad <= toSendLoad[l] * 1.35)
      {
        if (l != SELF_IDX && (maxi == -1 || maxComm < comm[l]))
        {
          maxi = l;
          maxComm = comm[l];
        }
      }
    }

    if (maxi != -1)
    {
      migrated_obj_count++;
      int node = sendToNeighbors[maxi];
      toSendLoad[maxi] -= currLoad;
      totalSent += currLoad;

      int receiverNodePE = node;

      // update global map
      nodeGroup->map_obid_pe[get_obj_idx(objHandle)] = receiverNodePE;

      // Diffusion *diffRecv = diff_array(receiverNodePE).ckLocal();
      // diffRecv->my_load_after_transfer += currLoad;

      diff_array(receiverNodePE).updateLoad(currLoad);
      // thisProxy[receiverNodePE].addObject(obj_global_idx);
      objects.erase(objects.begin() + v_id);

      my_load_after_transfer -= currLoad;
      //        CkPrintf("\nSending load %lf from node-%d(load %lf) to node-%d (load %lf)", currLoad, thisIndex, my_load_after_transfer, receiverNodePE,diffRecv->my_load_after_transfer);
      loadNeighbors[maxi] += currLoad;
    }
    else
    {
      DEBUGL(("[%d] maxi is negative currLoad %f \n", CkMyPe(), currLoad));
    }
  } // end of while

  for (int i = 0; i < neighborCount; i++)
  {
    double to_send_total = 0.0;
    if (toSendLoad[i] > 0.0)
    {
      to_send_total += toSendLoad[i];
      DEBUGL(("\nNode-%d (load %lf), I was not able to send load %lf to Node-%d", thisIndex, my_load_after_transfer, to_send_total, sendToNeighbors[i]));
    }
  }
  //    CkPrintf("\nSimNode#%d - After LB load = %lf and migrating %d objects", thisIndex, my_load, migrated_obj_count); fflush(stdout);
  CkCallback cbm(CkReductionTarget(Diffusion, finishLB), thisProxy);
  contribute(cbm); // sizeof(double), &my_load_after_transfer, CkReduction::max_double, cbm);

  // contribute(CkCallback(CkReductionTarget(Main, done), mainProxy));
}

void Diffusion::InitializeObjHeap(int *obj_arr, int n, int *gain_val)
{
  for (int i = 0; i < n; i++)
  {
    obj_arr[i] = i;
    obj_heap[i] = obj_arr[i];
    heap_pos[obj_arr[i]] = i;
  }
  heapify(obj_heap, ObjCompareOperator(&objects, gain_val), heap_pos);
}

void Diffusion::LoadBalancingCentroids()
{
  int n_objs = objects.size(); // objects only includes objects on my node?

  // For each object, store its distance to current centroid (as gain val)
  // and store dist to all neighboring node centroids
  std::vector<std::vector<double>> map_obj_to_neighbor_dist(n_objs);
  std::vector<double> map_obj_to_load(n_objs);
  std::vector<int> obj_local_to_global(n_objs);

  double *gain_value = new double[n_objs];

  for (int i = 0; i < n_objs; i++)
  {
    int objHandle = objects[i].getVertexId();
    int obj_idx = get_obj_idx(objHandle); // gets global obj index
    obj_local_to_global[i] = obj_idx;
    if (map_obid_pe[obj_idx] != thisIndex)
      CmiAbort("ERROR: object %d not on node %d\n", obj_idx, thisIndex);

    // object load is just wall time
    map_obj_to_load[i] = statsData->objData[obj_idx].wallTime;

    // current object is local
    std::vector<LBRealType> obj_pos = statsData->objData[obj_idx].position;

    // gain_value is just the distance to the current centroid
    std::vector<LBRealType> curr_centroid = getCentroid(thisIndex);
    gain_value[i] = (double)computeDistance(obj_pos, curr_centroid);

    // store the distance to all other centroids
    map_obj_to_neighbor_dist[i].resize(neighborCount);
    for (int n = 0; n < neighborCount; n++)
    {
      std::vector<LBRealType> n_centroid = getCentroid(sendToNeighbors[n]);
      map_obj_to_neighbor_dist[i][n] = computeDistance(obj_pos, n_centroid);
    }
  }

  // For sorting: make pairs of object id and gain value
  std::vector<std::pair<double, int>> obj_gain_pairs(n_objs);
  for (int i = 0; i < n_objs; i++)
  {
    obj_gain_pairs[i] = std::make_pair(gain_value[i], i);
  }

  // SORT: sort the objects based on gain value (in increasing order)
  std::sort(obj_gain_pairs.begin(), obj_gain_pairs.end(), std::less<std::pair<double, int>>());

  // Migration: iteratively picking item with most gain value
  while (my_load_after_transfer > 0)
  {
    if (obj_gain_pairs.empty())
      break;

    // pop front item out of sorted list (highest gain value)
    auto front = obj_gain_pairs.front();
    obj_gain_pairs.erase(obj_gain_pairs.begin());

    int obj_local_idx = front.second;
    int obj_gain = front.first;

    int obj_global_idx = obj_local_to_global[obj_local_idx];

    if (map_obid_pe[obj_global_idx] != thisIndex)
      CkAbort("ERROR: Object %d not on node %d\n", obj_global_idx, thisIndex);

    if (!statsData->objData[obj_global_idx].migratable)
      CkAbort("Object in objects list must be migratable: obj %d on pe %d\n", obj_global_idx, thisIndex);

    double currLoad = map_obj_to_load[obj_local_idx];

    // compute (neighbor_distance, neighbor_id) pairs for this object
    std::vector<std::pair<double, int>> neighbor_dist_pairs(neighborCount);
    for (int n = 0; n < neighborCount; n++)
    {
      int localNeighborId = n;
      int objDist = map_obj_to_neighbor_dist[obj_local_idx][localNeighborId];
      neighbor_dist_pairs[localNeighborId] = std::make_pair(objDist, localNeighborId);
    }

    // sort the neighbors based on distance to this object
    std::sort(neighbor_dist_pairs.begin(), neighbor_dist_pairs.end());

    // find the first neighbor that can take this object
    int localToSendNeighbor = -1;
    for (int n = 0; n < neighborCount; n++)
    {
      int local_n_id = neighbor_dist_pairs[n].second;
      if (toSendLoad[local_n_id] > 0.0 && currLoad <= toSendLoad[local_n_id] * 1.35)
      {
        localToSendNeighbor = local_n_id;
        break;
      }
    }

    // no neighbor chosen, obj doesn't migrate
    if (localToSendNeighbor == -1)
      continue;

    // object and neighbor have been chosen
    objects.erase(objects.begin() + obj_local_idx); // removing object from local list
    // localToSendNeighbor is the id of the neighbor in local context (used in sendToNeighbors, toSendLoad, etc.)
    // globalNeighborId is the global id of the neighbor (used in map_obid_pe and other global contexts)
    int globalNeighborId = sendToNeighbors[localToSendNeighbor];
    toSendLoad[localToSendNeighbor] -= currLoad;
    loadNeighbors[localToSendNeighbor] += currLoad;
    my_load_after_transfer -= currLoad;

    // global updates
    // diff0->map_obid_pe[obj_global_idx] = globalNeighborId;
    nodeGroup->map_obid_pe[obj_global_idx] = globalNeighborId;

    // Diffusion *diffRecv = diff_array(globalNeighborId).ckLocal();
    // diffRecv->my_load_after_transfer += currLoad;
    thisProxy[globalNeighborId].updateLoad(currLoad);
    // thisProxy[globalNeighborId].addObject(obj_global_idx);
  }

  CkCallback cbm(CkReductionTarget(Diffusion, finishLB), thisProxy);
  contribute(cbm);
}

// not needed because we dont use objects list again
void Diffusion::addObject(int obj_global_idx)
{
  int objHandle = statsData->objData[obj_global_idx].handle.objID();
  double currLoad = statsData->objData[obj_global_idx].wallTime;
  new_objects.push_back(CkVertex(objHandle, currLoad, statsData->objData[obj_global_idx].migratable, thisIndex));
}

#include "Diffusion.def.h"
