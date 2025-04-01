/* Pick NUM_NEIGHBORS in random */

void Diffusion::createCommList()
{
  pick = 0;
  long ebytes[numNodes];
  std::fill_n(ebytes, numNodes, 0);
  node_idx = new int[numNodes];
  for (int i = 0; i < numNodes; i++)
    node_idx[i] = -1;

  for (int edge = 0; edge < edge_indices.size(); edge++)
  {
    LDCommData &commData = statsData->commData[edge_indices[edge]];
    if ((!commData.from_proc()) && (commData.recv_type() == LD_OBJ_MSG))
    {
      LDObjKey from = commData.sender;
      LDObjKey to = commData.receiver.get_destObj();

      int fromobj = get_obj_idx(from.objID());
      int toobj = get_obj_idx(to.objID());
      if (fromobj == -1 || toobj == -1)
        continue;
      int fromNode = obj_node_map(fromobj);
      if (fromNode != thisIndex)
        continue;
      int toNode = obj_node_map(toobj);

      if (thisIndex != toNode && toNode != -1)
        ebytes[toNode] += commData.bytes;
    }
  }

  // initialize cost per neighbor (cost is a misnomer: higher cost is better neighbor)
  // TODO: note that this cost can be zero... is this okay?
  for (int i = 0; i < numNodes; i++)
  {
    cost_for_neighbor[i] = ebytes[i];
  }

  sortArr(ebytes, numNodes, node_idx);
}

#define ROUNDS 25

void Diffusion::findNBors(int do_again)
{
  DEBUGL(("\nNode-%d, round =%d, sendToNeighbors.size() = %d", thisIndex, round, sendToNeighbors.size()));
  if (round == 0)
  {
    holds = new int[ROUNDS+1];
    for(int i=0;i<ROUNDS+1;i++)
      holds[i] = 0;

    cost_for_neighbor = {}; // dictionary of nbor keys to cost
    if (centroid)
    {
      pick = 0;
      createDistNList();
    }
    else
    {
      createCommList();
    }
  }

  CkCallback cb(CkIndex_Diffusion::metricsAdded(), thisProxy);
  contribute(cb);
}

void Diffusion::metricsAdded()
{

  mstVisitedPes.clear();

  double init_and_parent[3];
  init_and_parent[0] = 0;
  init_and_parent[1] = -1;
  init_and_parent[2] = 0;
  
  /*
  int do_again = 1;
  CkCallback cb(CkReductionTarget(Diffusion, findRemainingNbors), thisProxy);
  contribute(sizeof(int), &do_again, CkReduction::max_int, cb);
  */

  buildMSTinRounds(init_and_parent, 2);
//   findRemainingNbors(1);
}

void Diffusion::findRemainingNbors()
{
  if(round < ROUNDS && thisIndex==0) {
    CkCallback cb(CkReductionTarget(Diffusion, findRemainingNbors), thisProxy);
    CkStartQD(cb);
  }

  if (round == ROUNDS)
  {
    neighborCount = sendToNeighbors.size();
    for(int i=0;i<sendToNeighbors.size();i++) {
      CkPrintf("\nEdge (%d,%d),", thisIndex, sendToNeighbors[i]);
    }

    loadNeighbors = new double[neighborCount];
    toSendLoad = new double[neighborCount];
    toReceiveLoad = new double[neighborCount];

    CkCallback cb(CkIndex_Diffusion::startDiffusion(), thisProxy);
    contribute(cb);
    return;
  }
  round++;
  
  int myNodeId = thisIndex;
  int nborsNeeded = NUM_NEIGHBORS - sendToNeighbors.size() - holds[round];
  int local_tries = 0;

  if(nborsNeeded > 0)
  {
    CkPrintf("[Node-%d]neighbors (%d/%d) still needed\n", thisIndex, sendToNeighbors.size(), NUM_NEIGHBORS);
    while(local_tries < nborsNeeded)
    {
      pick = (pick + 1)%(NUM_NEIGHBORS);
      int potentialNbor = node_idx[pick]; //pick - better logic needed here

      if(potentialNbor == -1) {
        local_tries++;
        continue;
      }
      if (myNodeId != potentialNbor &&
          std::find(sendToNeighbors.begin(), sendToNeighbors.end(), potentialNbor) == sendToNeighbors.end() &&
          potentialNbor < numNodes &&
          potentialNbor >= 0)
      {
        node_idx[pick] = -1;
        CkPrintf("Node-%d sending request round =%d, potentialNbor = Node-%d\n", thisIndex, round, potentialNbor);
        thisProxy(potentialNbor).askNbor(myNodeId, round);
      }
      local_tries++;
    }
  }
}

void Diffusion::askNbor(int nborId, int rnd)
{ 
  int agree = 0;
  int nborsNeeded = NUM_NEIGHBORS - sendToNeighbors.size() - holds[rnd];
  if (nborsNeeded>0 &&
      std::find(sendToNeighbors.begin(), sendToNeighbors.end(), nborId) == sendToNeighbors.end())
  { 
    //HOLD A SPOT THOUGH on THIS ROUND!!
    agree = 1;
    holds[rnd]++;

//    sendToNeighbors.push_back(nborId);
    DEBUGL2(("\nNode-%d (holds[%d]=%d), (%d- %d- %d> 0?) round =%d Agreeing to hold for %d ", thisIndex, rnd, holds[rnd], NUM_NEIGHBORS, sendToNeighbors.size(), holds[rnd]-1,
    round, nborId));
  }
  else
  { 
    DEBUGL2(("\nNode-%d, round =%d Rejecting %d ", thisIndex, round, nborId));
  }
  thisProxy(nborId).okayNbor(agree, thisIndex);
}

void Diffusion::okayNbor(int agree, int nborId)
{ 
  int nborsNeeded = NUM_NEIGHBORS - sendToNeighbors.size() - holds[round];
  if (nborsNeeded > 0 && agree && std::find(sendToNeighbors.begin(), sendToNeighbors.end(), nborId) == sendToNeighbors.end())
  { 
    DEBUGL2(("\n[Node-%d, round-%d] Rcvd ack, adding %d as nbor (neighbors:%d/%d, holds[%d]=%d)", thisIndex, round, nborId,sendToNeighbors.size(), NUM_NEIGHBORS, round, holds[round]));
    sendToNeighbors.push_back(nborId);
    thisProxy[nborId].ackNbor(thisIndex);
  } else {
    CkPrintf("\n[Node-%d] Decided not to pursue orig request to node %d", thisIndex, nborId);
  }
}

void Diffusion::ackNbor(int nborId) {
  if(std::find(sendToNeighbors.begin(), sendToNeighbors.end(), nborId) == sendToNeighbors.end()) {
    CkPrintf("\n[Node-%d] Adding neighbor [%d] through final ack (neighbors:%d/%d)", thisIndex, nborId, sendToNeighbors.size(), NUM_NEIGHBORS);
    sendToNeighbors.push_back(nborId);
  }
}


void Diffusion::buildMSTinRounds(double *init_and_parent, int n)
{
  // double cost = init_and_parent[0];
  double from = init_and_parent[1];
  double to = init_and_parent[2]; // new node added to graph

  // correctness checks for reduction input
  // note: if from = -1, this is fine because this is how we initialize the graph
  // TODO: optimization: remove the first round of this algo and just start with node 0 in the graph

  assert(to != from);
  assert(to != -1);

  mstVisitedPes.push_back(to);

  // initiator is new node added to graph
  // assert that to is not already in graph
  if (thisIndex == to)
  {
    if (from != -1)
    {
      // this check ensures that during the first round (when to = 0, from = -1), we don't add -1 to the neighbors
      sendToNeighbors.push_back(from);
    }
  }

  if (thisIndex == from)
  {
    sendToNeighbors.push_back(to);
  }

  if (mstVisitedPes.size() == numNodes)
  {
    // all nodes have been visited, MST is complete
    int do_again = 1;
    CkCallback cb(CkReductionTarget(Diffusion, findRemainingNbors), thisProxy);
    contribute(sizeof(int), &do_again, CkReduction::max_int, cb);
  }
  else
  {
    // find best new edge to add, based on cost
    double newNbor = -1;
    double newParent = -1;
    double newCost = 0; // TODO: cost is a misnomer, we want to maximize the cost

    // check if thisIndex is in mstVisitedPes
    if (std::find(mstVisitedPes.begin(), mstVisitedPes.end(), thisIndex) != mstVisitedPes.end() && sendToNeighbors.size()<NUM_NEIGHBORS)
    {
      // node in visited set
      // pick best edge (it is best because node_idx are sorted by preference)
      while (1)
      {

        pick = (pick + 1) % numNodes;
        int checkNbor = node_idx[pick];
        if (std::find(mstVisitedPes.begin(), mstVisitedPes.end(), checkNbor) == mstVisitedPes.end()
            && checkNbor != thisIndex && checkNbor < numNodes && checkNbor >= 0)
        {
          newNbor = (double)checkNbor;
          newParent = thisIndex;
          newCost = cost_for_neighbor[newNbor];
          break;
        }
      }
    }

    // contribute to reduction
    double init_and_parent_new[3];
    init_and_parent_new[0] = newCost;
    init_and_parent_new[1] = newParent;
    init_and_parent_new[2] = newNbor;

    contribute(sizeof(double) * 3, init_and_parent_new, findBestEdgeType, CkCallback(CkReductionTarget(Diffusion, buildMSTinRounds), thisProxy));
  }
}

/* This function creates a list of neighbors, stored in node_idx, and sorted by "position" distance from the current node */
void Diffusion::createDistNList()
{
  // initialization
  long distance[numNodes];
  node_idx = new int[numNodes];

  // compute distance from local aggregate centroid to all other aggregate centroids
  if (getCentroid(thisIndex).size() == 0)
  {
    CkPrintf("Error: map_pe_centroid is empty\n");
    CkExit();
  }
  std::vector<LBRealType> myCentroid = getCentroid(thisIndex);

  for (int n = 0; n < numNodes; n++)
  {
    node_idx[n] = n;
    distance[n] = 0;
    if (n == thisIndex)
    {
      continue;
    }

    std::vector<LBRealType> oppCentroid = getCentroid(n);
    for (int i = 0; i < myCentroid.size(); i++)
    {
      distance[n] += (myCentroid[i] - oppCentroid[i]) * (myCentroid[i] - oppCentroid[i]);
    }
  }

  for (int nbor = 0; nbor < numNodes; nbor++)
  {
    // cost is a misnomer: higher cost is better neighbor
    if (distance[nbor] != 0)
      cost_for_neighbor[nbor] = 1 / (double)distance[nbor]; // neighbor with high distance has low value

    else
      cost_for_neighbor[nbor] = 100000000; // neighbor with 0 distance has high value
  }

  // sort neighbors based on centroid distance
  pairedSort(node_idx, distance, numNodes);
}


void Diffusion::pairedSort(int *A, long *B, int n)
{
  // sort array A based on corresponding values in B (both of size n)
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

void Diffusion::sortArr(long arr[], int n, int *node_idx)
{
  std::vector<std::pair<long, int>> vp;
  // Inserting element in pair vector
  // to keep track of previous indexes
  for (int i = 0; i < n; ++i)
  {
    vp.push_back(std::make_pair(arr[i], i));
  }
  // Sorting pair vector
  sort(vp.rbegin(), vp.rend());
  int rank = 0;
  for (int i = 0; i < numNodes; i++)
    if (thisIndex != vp[i].second) // Ideally we shouldn't need to check this
      node_idx[rank++] = vp[i].second;
  if (rank == 0)
    DEBUGL(("\nPE-%d Error!!!!!", CkMyPe()));
}
