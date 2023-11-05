/* Pick NUM_NEIGHBORS in random */

void Diffusion::findNBors(int do_again) {
  requests_sent = 0;
  if(!do_again || round == 100) {
    neighborCount = sendToNeighbors.size();
    std::string nbor_nodes;
    for(int i = 0; i < neighborCount; i++) {
      nbor_nodes += "node-"+ std::to_string(sendToNeighbors[i])+", ";
    }
    DEBUGL(("node-%d with nbors %s\n", thisIndex, nbor_nodes.c_str()));

    loadNeighbors = new double[neighborCount];
    toSendLoad = new double[neighborCount];
    toReceiveLoad = new double[neighborCount];

    CkCallback cb(CkIndex_Diffusion::startDiffusion(), thisProxy);
    contribute(cb);
    return;
  }
  int potentialNb = 0;
  int myNodeId = thisIndex;
  int nborsNeeded = (NUM_NEIGHBORS - sendToNeighbors.size())/2;
  if(nborsNeeded > 0) {
    while(potentialNb < nborsNeeded) {
      int potentialNbor = rand() % (NX*NY);
      if(myNodeId != potentialNbor &&
          std::find(sendToNeighbors.begin(), sendToNeighbors.end(), potentialNbor) == sendToNeighbors.end()) {
        requests_sent++;
        thisProxy(potentialNbor).proposeNbor(myNodeId);
        potentialNb++;
      }
    }
  }
  else {
    int do_again = 0;
    CkCallback cb(CkReductionTarget(Diffusion, findNBors), thisProxy);
    contribute(sizeof(int), &do_again, CkReduction::max_int, cb);
  }
}

void Diffusion::proposeNbor(int nborId) {
  int agree = 0;
  if((NUM_NEIGHBORS-sendToNeighbors.size())-requests_sent > 0 && sendToNeighbors.size() < NUM_NEIGHBORS &&
      std::find(sendToNeighbors.begin(), sendToNeighbors.end(), nborId) == sendToNeighbors.end()) {
    agree = 1;
    sendToNeighbors.push_back(nborId);
    DEBUGL2(("\nNode-%d, round =%d Agreeing and adding %d ", thisIndex, round, nborId));
  } else {
    DEBUGL2(("\nNode-%d, round =%d Rejecting %d ", thisIndex, round, nborId));
  }
  thisProxy(nborId).okayNbor(agree, thisIndex);
}

void Diffusion::okayNbor(int agree, int nborId) {
  if(sendToNeighbors.size() < NUM_NEIGHBORS && agree && std::find(sendToNeighbors.begin(), sendToNeighbors.end(), nborId) == sendToNeighbors.end()) {
    DEBUGL2(("\n[Node-%d, round-%d] Rcvd ack, adding %d as nbor", thisIndex, round, nborId));
    sendToNeighbors.push_back(nborId);
  }

  requests_sent--;
  if(requests_sent > 0) return;

  int do_again = 0;
  if(sendToNeighbors.size()<NUM_NEIGHBORS)
    do_again = 1;
  round++;
  CkCallback cb(CkReductionTarget(Diffusion, findNBors), thisProxy);
  contribute(sizeof(int), &do_again, CkReduction::max_int, cb);
}

/* 3D and 2D neighbors for each cell in 3D/2D grid */

void Diffusion::pick3DNbors() {
#if NBORS_3D
  int x = getX(thisIndex);
  int y = getY(thisIndex);
  int z = getZ(thisIndex);

  //6 neighbors along face of cell
  sendToNeighbors.push_back(getNodeId(x-1,y,z));
  sendToNeighbors.push_back(getNodeId(x+1,y,z));
  sendToNeighbors.push_back(getNodeId(x,y-1,z));
  sendToNeighbors.push_back(getNodeId(x,y+1,z));
  sendToNeighbors.push_back(getNodeId(x,y,z-1));
  sendToNeighbors.push_back(getNodeId(x,y,z+1));

  //12 neighbors along edges
  sendToNeighbors.push_back(getNodeId(x-1,y-1,z));
  sendToNeighbors.push_back(getNodeId(x-1,y+1,z));
  sendToNeighbors.push_back(getNodeId(x+1,y-1,z));
  sendToNeighbors.push_back(getNodeId(x+1,y+1,z));

  sendToNeighbors.push_back(getNodeId(x-1,y,z-1));
  sendToNeighbors.push_back(getNodeId(x-1,y,z+1));
  sendToNeighbors.push_back(getNodeId(x+1,y,z-1));
  sendToNeighbors.push_back(getNodeId(x+1,y,z+1));

  sendToNeighbors.push_back(getNodeId(x,y-1,z-1));
  sendToNeighbors.push_back(getNodeId(x,y-1,z+1));
  sendToNeighbors.push_back(getNodeId(x,y+1,z-1));
  sendToNeighbors.push_back(getNodeId(x,y+1,z+1));
#if 0
  //neighbors at vertices
  sendToNeighbors.push_back(getNodeId(x-1,y-1,z-1));
  sendToNeighbors.push_back(getNodeId(x-1,y-1,z+1));
  sendToNeighbors.push_back(getNodeId(x-1,y+1,z-1));
  sendToNeighbors.push_back(getNodeId(x-1,y+1,z+1));

  sendToNeighbors.push_back(getNodeId(x+1,y-1,z-1));
  sendToNeighbors.push_back(getNodeId(x+1,y-1,z+1));
  sendToNeighbors.push_back(getNodeId(x+1,y+1,z-1));
  sendToNeighbors.push_back(getNodeId(x+1,y+1,z+1));
#endif

   //Create 2d neighbors
#if 0
  if(thisIndex.x > 0) sendToNeighbors.push_back(getNodeId(thisIndex.x-1, thisIndex.y));
  if(thisIndex.x < N-1) sendToNeighbors.push_back(getNodeId(thisIndex.x+1, thisIndex.y));
  if(thisIndex.y > 0) sendToNeighbors.push_back(getNodeId(thisIndex.x, thisIndex.y-1));
  if(thisIndex.y < N-1) sendToNeighbors.push_back(getNodeId(thisIndex.x, thisIndex.y+1));
#endif

  int size = sendToNeighbors.size();
  int count = 0;

  for(int i=0;i<size-count;i++) {
    if(sendToNeighbors[i] < 0)  {
      sendToNeighbors[i] = sendToNeighbors[size-1-count];
      sendToNeighbors[size-1-count] = -1;
      i -= 1;
      count++;
    }
  }
  sendToNeighbors.resize(size-count);

  findNBors(0);
#endif
}
