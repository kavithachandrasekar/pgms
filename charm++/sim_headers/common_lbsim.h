typedef void (*obj_imb_funcptr)(BaseLB::LDStats*);

static void load_imb_by_pe(BaseLB::LDStats *statsData) {
  for(int obj = 0 ; obj < statsData->objData.size(); obj++) {
    LDObjData &oData = statsData->objData[obj];
    int pe = statsData->from_proc[obj];
    if (!oData.migratable) {
      if (!statsData->procs[pe].available)
        CmiAbort("LB sim cannot handle nonmigratable object on an unavial processor!\n");
      continue;
    }
    double load = 1.0;
    //if(pe==5) load = 1.8;
    //if(pe==14) load = 0.2;
//    if(pe == 5) load = 4.0;
    if(pe%3==0) load = 3.5;
    //if(pe==5 || pe == 6||pe == 9 || pe ==10) load = 1.0;
    //if(pe==1 || pe == 2 || pe == 13 || pe ==14) load = 2.0;
    statsData->objData[obj].wallTime = load; 
  }
}

static void load_imb_by_history(BaseLB::LDStats *statsData) {
  for(int obj = 0 ; obj < statsData->objData.size(); obj++) {
    LDObjData &oData = statsData->objData[obj];
    int pe = statsData->from_proc[obj];
    if (!oData.migratable) {
      if (!statsData->procs[pe].available)
        CmiAbort("LB sim cannot handle nonmigratable object on an unavial processor!\n");
      continue;
    }
    int a=rand()%2;
    if(a)
      statsData->objData[obj].wallTime *= 0.6;
    else
      statsData->objData[obj].wallTime *= 1.4;
  }
}

template <typename T> static void computeCommBytes(BaseLB::LDStats *statsData, T* obj, int before) {
  double internalBytes = 0.0;
  double externalBytes = 0.0;
//  CkPrintf("\nNumber of edges = %d", statsData->commData.size());

//#pragma omp parallel for num_threads(4)
  for(int edge = 0; edge < statsData->commData.size(); edge++) {
    LDCommData &commData = statsData->commData[edge];
    if(!commData.from_proc() && commData.recv_type()==LD_OBJ_MSG)
    {
      LDObjKey from = commData.sender;
      LDObjKey to = commData.receiver.get_destObj();
      int fromobj = obj->get_obj_idx(from.objID());
      int toobj = obj->get_obj_idx(to.objID());
      if(fromobj == -1 || toobj == -1) continue;
      int fromNode = obj->obj_node_map(fromobj);
      int toNode = obj->obj_node_map(toobj);
      if(!before) {
        fromNode = obj->obj_updated_node_map(fromobj);
        toNode = obj->obj_updated_node_map(toobj);
      }

      //store internal bytes in the last index pos ? -q
      if(fromNode == toNode)
        internalBytes+= commData.bytes;//internal_arr[omp_get_thread_num()] += commData.bytes;
      else// External communication
        externalBytes += commData.bytes;//external_arr[omp_get_thread_num()] += commData.bytes;
    }
  // else {
  //    CkPrintf("\nNot the kind of edge we want");
  //  }
  } // end for

  const char* tag = "Before";
  if(!before)
    tag = "After";
  CkPrintf("\n[%s LB] Internal comm Mbytes = %lf, External comm Mbytes = %lf", tag, internalBytes/(1024*1024), externalBytes/(1024*1024));
}
