typedef void (*obj_imb_funcptr)(BaseLB::LDStats *);



static void load_imb_rand40(BaseLB::LDStats *statsData)
{
  for (int obj = 0; obj < statsData->objData.size(); obj++)
  {
    LDObjData &oData = statsData->objData[obj];
    int pe = statsData->from_proc[obj];
    if (!oData.migratable)
    {
      if (!statsData->procs[pe].available)
        CmiAbort("LB sim cannot handle nonmigratable object on an unavial processor!\n");
      continue;
    }
    int a = rand() % 2;
    if (a)
      statsData->objData[obj].wallTime *= 0.6;
    else
      statsData->objData[obj].wallTime *= 1.4;
  }
}

static void load_imb_rank0_overload(BaseLB::LDStats *statsData)
{
  int nprocs = statsData->n_nodes;
  int rank0_pe = 0;

  for (int obj = 0; obj < statsData->objData.size(); obj++)
  {
    LDObjData &oData = statsData->objData[obj];
    int pe = statsData->from_proc[obj];
    if (!oData.migratable)
    {
      if (!statsData->procs[pe].available)
        CmiAbort("LB sim cannot handle nonmigratable object on an unavial processor!\n");
      continue;
    }

    if (pe == rank0_pe)
    {
      statsData->objData[obj].wallTime *= 10.0;
    }
  }
}

static void load_imb_inject_middle(BaseLB::LDStats *statsData)
{
  int nprocs = statsData->n_nodes;
  int middle_pe = nprocs / 2;

  for (int obj = 0; obj < statsData->objData.size(); obj++)
  {
    LDObjData &oData = statsData->objData[obj];
    int pe = statsData->from_proc[obj];
    if (!oData.migratable)
    {
      if (!statsData->procs[pe].available)
        CmiAbort("LB sim cannot handle nonmigratable object on an unavial processor!\n");
      continue;
    }

    if (pe == middle_pe)
    {
      statsData->objData[obj].wallTime *= 2.0;
    }
  }
}

static void load_imb_bype40(BaseLB::LDStats *statsData)
{
  int nprocs = statsData->n_nodes;
  std::vector<double> scale(nprocs, 0);

  // fill scale with rand values between .8 and 1.2
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<> dis(0.8, 1.2);

  for (int i = 0; i < nprocs; i++)
  {
    scale[i] = dis(gen);
  }

  for (int obj = 0; obj < statsData->objData.size(); obj++)
  {
    LDObjData &oData = statsData->objData[obj];
    int pe = statsData->from_proc[obj];
    if (!oData.migratable)
    {
      if (!statsData->procs[pe].available)
        CmiAbort("LB sim cannot handle nonmigratable object on an unavial processor!\n");
      continue;
    }

    // double load = statsData->objData[obj].wallTime * scale[pe];
    double load = 1.0 * scale[pe];
    statsData->objData[obj].wallTime *= scale[pe];
  }
}

static void load_imb_bype_deterministic(BaseLB::LDStats *statsData)
{
  int nprocs = statsData->n_nodes;

  std::vector<double> scale(nprocs, 0);

  for (int i = 0; i < nprocs; i++)
  {
    if (i % 7 == 0)
      scale[i] = 1.4;
    else if (i % 7 == 1)
      scale[i] = 1.2;
    else if (i % 7 == 2)
      scale[i] = 0.5;
    else
      scale[i] = 1;
  }

  for (int obj = 0; obj < statsData->objData.size(); obj++)
  {
    LDObjData &oData = statsData->objData[obj];
    int pe = statsData->from_proc[obj];
    if (!oData.migratable)
    {
      if (!statsData->procs[pe].available)
        CmiAbort("LB sim cannot handle nonmigratable object on an unavial processor!\n");
      continue;
    }

    double load = scale[pe] ;
    statsData->objData[obj].wallTime = load;
  }
}

static void load_imb_rand_pair(BaseLB::LDStats *statsData, int factor = 5)
{

  int nprocs = statsData->nprocs();

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, nprocs - 1);

  int rand_pe1 = dis(gen); // overloading
  int rand_pe2 = dis(gen); // underloading

  while (rand_pe1 == rand_pe2)
    rand_pe2 = dis(gen);

  CkPrintf("<LOAD IMB> Randomly moving load from %d to %d\n", rand_pe1, rand_pe2);

  for (int obj = 0; obj < statsData->objData.size(); obj++)
  {
    LDObjData &oData = statsData->objData[obj];
    int pe = statsData->from_proc[obj];
    if (!oData.migratable)
    {
      if (!statsData->procs[pe].available)
        CmiAbort("LB sim cannot handle nonmigratable object on an unavial processor!\n");
      continue;
    }

    if (pe == rand_pe1)
      statsData->objData[obj].wallTime = statsData->objData[obj].wallTime * factor;
    else if (pe == rand_pe2)
      statsData->objData[obj].wallTime = statsData->objData[obj].wallTime / factor;
  }
}

static void load_setconst(BaseLB::LDStats *statsData)
{
  for (int obj = 0; obj < statsData->objData.size(); obj++)
  {
    LDObjData &oData = statsData->objData[obj];
    int pe = statsData->from_proc[obj];
    if (!oData.migratable)
    {
      if (!statsData->procs[pe].available)
        CmiAbort("LB sim cannot handle nonmigratable object on an unavial processor!\n");
      continue;
    }
    statsData->objData[obj].wallTime = .01;
  }
}

static void no_imb(BaseLB::LDStats *statsData)
{
  return;
}

template <typename T>
static void computeCommBytes(BaseLB::LDStats *statsData, T *obj, int before, double &internalBytes, double &externalBytes)
{
  internalBytes = 0.0;
  externalBytes = 0.0;
  //  CkPrintf("\nNumber of edges = %d", statsData->commData.size());

  // #pragma omp parallel for num_threads(4)
  for (int edge = 0; edge < statsData->commData.size(); edge++)
  {
    LDCommData &commData = statsData->commData[edge];
    if (!commData.from_proc() && commData.recv_type() == LD_OBJ_MSG)
    {
      LDObjKey from = commData.sender;
      LDObjKey to = commData.receiver.get_destObj();
      int fromobj = statsData->getHash(from); // this replaces the simulator get_obj_idx
      int toobj = statsData->getHash(to);
      if (fromobj == -1 || toobj == -1)
        continue;
      int fromNode = obj->obj_node_map(fromobj);
      int toNode = obj->obj_node_map(toobj);
      if (!before)
      {
        fromNode = obj->obj_updated_node_map(fromobj);
        toNode = obj->obj_updated_node_map(toobj);
      }

      // store internal bytes in the last index pos ? -q
      if (fromNode == toNode)
        internalBytes += commData.bytes; // internal_arr[omp_get_thread_num()] += commData.bytes;
      else                               // External communication
        externalBytes += commData.bytes; // external_arr[omp_get_thread_num()] += commData.bytes;
    }
    // else {
    //    CkPrintf("\nNot the kind of edge we want");
    //  }
  } // end for

  const char *tag = "Before";
  if (!before)
    tag = "After";
  CkPrintf("\n[%s LB] Internal comm Mbytes = %lf, External comm Mbytes = %lf", tag, internalBytes / (1024 * 1024), externalBytes / (1024 * 1024));

  internalBytes = internalBytes / (1024 * 1024);
  externalBytes = externalBytes / (1024 * 1024);

}
static double computeDistance(std::vector<LBRealType> a, std::vector<LBRealType> b)
{
  LBRealType dist = 0.0;
  for (int i = 0; i < a.size(); i++)
  {
    dist += (a[i] - b[i]) * (a[i] - b[i]);
  }
  dist = sqrt(dist);
  return dist;
}

template <typename T>
static void computeSpread(BaseLB::LDStats *statsData, T *obj, int before)
{
  int n_objs = statsData->objData.size();
  int n_procs = statsData->nprocs();

  double avg_spread = 0.0;

  if (n_objs > 0)
  {
    int posDim = statsData->objData[0].position.size();
    if (posDim > 0) // otherwise pos data not given
    {
      // first compute centroid for each proc
      std::vector<std::vector<double>> centroids(n_procs, std::vector<double>(posDim, 0.0));
      std::vector<int> objcount(n_procs, 0);
      std::vector<std::vector<std::vector<double>>> map_pe_to_obj_pos(n_procs);
      std::vector<double> spread(n_procs, 0.0);

      // accumulate positions to centroid
      for (int i = 0; i < n_objs; i++)
      {
        LDObjData &oData = statsData->objData[i];
        int pe = obj->obj_node_map(i);

        for (int comp = 0; comp < posDim; comp++)
          centroids[pe][comp] += oData.position[comp];
        objcount[pe]++;
      }

      // find centroid via averaging
      for (int i = 0; i < n_procs; i++)
      {
        if (objcount[i] > 0)
        {
          for (int comp = 0; comp < posDim; comp++)
          {
            centroids[i][comp] /= objcount[i];
          }
        }
      }

      // compute distance to each object
      for (int i = 0; i < n_objs; i++)
      {
        LDObjData &oData = statsData->objData[i];
        int pe = obj->obj_node_map_updated(i);
        spread[pe] += (double)computeDistance(oData.position, centroids[pe]);
      }

      // average distance for spread
      for (int i = 0; i < n_procs; i++)
      {
        if (objcount[i] > 0)
          spread[i] /= objcount[i];
      }

      // average spread over all procs
      avg_spread = std::accumulate(spread.begin(), spread.end(), 0.0) / n_procs;
    }
  }
  const char *tag = "Before";
  if (!before)
    tag = "After";
  CkPrintf("[%s LB] Position spread = %lf\n", tag, avg_spread);
}

obj_imb_funcptr getImbalanceFunction(int fn_type)
{
    obj_imb_funcptr obj_imb;
    switch (fn_type)
    {
    case 1:
        // randomly multiply object load by 0.6 or 1.4 (50% chance) for all objects
        obj_imb = (obj_imb_funcptr)load_imb_rand40;
        break;
    case 2:
        // inject load on middle pe (multiply by 2.0)
        obj_imb = (obj_imb_funcptr)load_imb_inject_middle;
        break;
    case 3:
        obj_imb = (obj_imb_funcptr)load_imb_bype40;
        break;

    case 4:
        obj_imb = (obj_imb_funcptr)load_imb_bype_deterministic;
        break;
    case 5:
        obj_imb = (obj_imb_funcptr)load_imb_rank0_overload;
        break;
    default:
        obj_imb = (obj_imb_funcptr)no_imb;
        break;
    }

    return obj_imb;
}
