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
    if(pe%3==0) load = 3.5;
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
      statsData->objData[obj].wallTime *= 0.8;
    else
      statsData->objData[obj].wallTime *= 1.2;
  }
}
