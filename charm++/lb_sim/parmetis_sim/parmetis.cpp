#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include "metis.h"
#include "parmetis.h"
#include "json.hpp"

using json = nlohmann::json;

struct ObjectData {
    int id;
    int oldpe;
    int newpe;
    double wallTime;
    bool migratable;
};

struct CommEdge {
    int from_obj;
    int to_obj;
    double bytes;
};

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    const char* json_file = "lbdump.json";
    if (argc > 1) {
        json_file = argv[1];
    }

    // Read JSON file on rank 0
    json jsonData;
    int n_procs = 0;
    int n_nodes = 0;
    int total_objs = 0;
    std::vector<ObjectData> all_objects;
    std::vector<CommEdge> all_comm;

    if (rank == 0) {
        std::ifstream f(json_file);
        if (!f.is_open()) {
            fprintf(stderr, "Error: Cannot open file %s\n", json_file);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        jsonData = json::parse(f);
        f.close();

        n_procs = jsonData["n_procs"];
        n_nodes = jsonData["n_nodes"];

        printf("Read JSON: n_procs=%d, n_nodes=%d\n", n_procs, n_nodes);

        // Check if MPI size matches n_procs
        if (size != n_procs) {
            fprintf(stderr, "ERROR: Number of MPI processes (%d) does not match n_procs in JSON (%d)\n", 
                    size, n_procs);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        // Read objects
        auto objData = jsonData["objData"];
        for (auto& [key, value] : objData.items()) {
            ObjectData obj;
            obj.id = value["id"];
            obj.oldpe = value["oldpe"];
            obj.wallTime = value["wallTime"];
            obj.migratable = value["migratable"];
            all_objects.push_back(obj);
        }
        total_objs = all_objects.size();
        printf("Read %d objects\n", total_objs);

             
   
        // Sort objects by oldpe
        std::sort(all_objects.begin(), all_objects.end(), 
                  [](const ObjectData& a, const ObjectData& b) { return a.oldpe < b.oldpe; });
    

        // Read communication data
        if (jsonData.contains("commData")) {
            auto commData = jsonData["commData"];
            for (auto& [key, value] : commData.items()) {
                CommEdge edge;
                edge.from_obj = value["sender_obj"]["objID"];
                edge.to_obj = value["receiver_obj"]["objID"];
                edge.bytes = value["msg_size"];
                all_comm.push_back(edge);
            }
            printf("Read %d communication edges\n", (int)all_comm.size());
        }
    }

    // Broadcast n_procs and validate on all ranks
    MPI_Bcast(&n_procs, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&n_nodes, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&total_objs, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank != 0 && size != n_procs) {
        fprintf(stderr, "ERROR on rank %d: Number of MPI processes (%d) does not match n_procs in JSON (%d)\n", 
                rank, size, n_procs);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // Distribute objects to ranks based on oldpe
    // Build vtxdist: each rank gets objects with oldpe == rank
    std::vector<int> objs_per_rank(size, 0);
    
    if (rank == 0) {
        for (const auto& obj : all_objects) {
            if (obj.oldpe >= 0 && obj.oldpe < size) {
                objs_per_rank[obj.oldpe]++;
            } else
            {
                printf("ERROR: Object %d has invalid oldpe %d\n", obj.id, obj.oldpe);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            
        }
    }
    
    // Broadcast object counts
    MPI_Bcast(objs_per_rank.data(), size, MPI_INT, 0, MPI_COMM_WORLD);

    // Build vtxdist
    idx_t *vtxdist = (idx_t*)malloc((size+1) * sizeof(idx_t));
    vtxdist[0] = 0;
    for (int i = 0; i < size; i++) {
        vtxdist[i+1] = vtxdist[i] + objs_per_rank[i];
    }

    idx_t nvtxs_local = vtxdist[rank+1] - vtxdist[rank];
    // printf("Rank %d: owns %d vertices (global range [%d, %d))\n", 
    //        rank, (int)nvtxs_local, (int)vtxdist[rank], (int)vtxdist[rank+1]);

    // Build map from object ID to global vertex index
    std::map<int, int> objid_to_vtx;
    std::vector<ObjectData> local_objects;

    printf("Rank %d: building has nvtxs_local=%d\n", rank, (int)nvtxs_local);
    
    if (rank == 0) {
        int vtx_idx = 0;
        for (const auto& obj : all_objects) {
            objid_to_vtx[obj.id] = vtx_idx++; // maps objID to global vertex index (to index into all_objects)
        }
    }

    // Broadcast the mapping size and data
    int map_size = objid_to_vtx.size();
    MPI_Bcast(&map_size, 1, MPI_INT, 0, MPI_COMM_WORLD);
    
    std::vector<int> map_keys(map_size);
    std::vector<int> map_vals(map_size);
    
    if (rank == 0) {
        int i = 0;
        for (const auto& kv : objid_to_vtx) {
            map_keys[i] = kv.first;
            map_vals[i] = kv.second;
            i++;
        }
    }
    
    MPI_Bcast(map_keys.data(), map_size, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(map_vals.data(), map_size, MPI_INT, 0, MPI_COMM_WORLD);
    
    if (rank != 0) {
        for (int i = 0; i < map_size; i++) {
            objid_to_vtx[map_keys[i]] = map_vals[i];
        }
    }

    // Scatter objects to appropriate ranks
    // Instead of using MPI_Scatterv with MPI_BYTE (which has issues with struct padding),
    // we'll broadcast the sorted array and each rank extracts its objects
   
    
    // Broadcast total object count
    MPI_Bcast(&total_objs, 1, MPI_INT, 0, MPI_COMM_WORLD);
    
    // Non-root ranks need to allocate space
    if (rank != 0) {
        all_objects.resize(total_objs);
    }
    
    // Broadcast all objects
    MPI_Bcast(all_objects.data(), total_objs * sizeof(ObjectData), MPI_BYTE, 0, MPI_COMM_WORLD);
    
    // Each rank extracts its own objects
    local_objects.clear();
    for (const auto& obj : all_objects) {
        if (obj.oldpe == rank) {
            local_objects.push_back(obj);
        }
    }
    
    if (local_objects.size() != nvtxs_local) {
        printf("ERROR Rank %d: expected %d objects but got %zu\n", 
               rank, (int)nvtxs_local, local_objects.size());
    }

    // Build local adjacency lists
    std::vector<std::set<int>> local_adj(nvtxs_local);
    std::vector<std::map<int, double>> local_edge_weights(nvtxs_local);
    
    // Broadcast communication edges
    int num_comm = 0;
    if (rank == 0) {
        num_comm = all_comm.size();
    }
    MPI_Bcast(&num_comm, 1, MPI_INT, 0, MPI_COMM_WORLD);
    
    if (rank != 0) {
        all_comm.resize(num_comm);
    }
    
    MPI_Bcast(all_comm.data(), num_comm * sizeof(CommEdge), MPI_BYTE, 0, MPI_COMM_WORLD);

    // Build adjacency lists for local objects
    for (const auto& edge : all_comm) {
        auto from_it = objid_to_vtx.find(edge.from_obj);
        auto to_it = objid_to_vtx.find(edge.to_obj);
        
        if (from_it != objid_to_vtx.end() && to_it != objid_to_vtx.end()) {
            int from_vtx = from_it->second;
            int to_vtx = to_it->second;
            
            // Check if from_vtx is local to this rank
            if (from_vtx >= vtxdist[rank] && from_vtx < vtxdist[rank+1]) {
                int local_idx = from_vtx - vtxdist[rank];
                local_adj[local_idx].insert(to_vtx);
                local_edge_weights[local_idx][to_vtx] += edge.bytes;
            }
        } else {
            printf("Error: Communication edge references unknown object IDs (%d -> %d) on rank %d\n",
                   edge.from_obj, edge.to_obj, rank);
        }
    }

    // Build ParMETIS arrays
    idx_t *xadj = (idx_t*)malloc((nvtxs_local + 1) * sizeof(idx_t));
    idx_t *vwgt = (idx_t*)malloc(nvtxs_local * sizeof(idx_t));
    
    // Count total edges
    int total_edges = 0;
    for (int i = 0; i < nvtxs_local; i++) {
        total_edges += local_adj[i].size();
    }
    
    idx_t *adjncy = (idx_t*)malloc(total_edges * sizeof(idx_t));
    idx_t *adjwgt = (idx_t*)malloc(total_edges * sizeof(idx_t));
    
    // Fill arrays
    xadj[0] = 0;
    int edge_idx = 0;
    
    // Scale wallTime to integer weights
    double vertex_scale = atof(argv[3]);
    // Use a separate (potentially larger) scale for edges to prioritize communication
    double edge_scale = argc > 4 ? atof(argv[4]) : vertex_scale;
    
    for (int i = 0; i < nvtxs_local; i++) {
        vwgt[i] = (idx_t)(local_objects[i].wallTime * vertex_scale);
        if (vwgt[i] == 0) vwgt[i] = 1;  // ensure non-zero weight
        
        for (int nbor : local_adj[i]) {
            adjncy[edge_idx] = nbor;
            // Scale edge weights - can be different from vertex scale
            adjwgt[edge_idx] = (idx_t)(local_edge_weights[i][nbor] * edge_scale);
            if (adjwgt[edge_idx] == 0) adjwgt[edge_idx] = 1;  // ensure non-zero
            edge_idx++;
        }
        xadj[i+1] = edge_idx;
    }
    

    // ParMETIS partitioning
    idx_t ncon = 1;
    idx_t wgtflag = 3;  // both vertex and edge weights
    idx_t numflag = 0;  // C-style numbering
    idx_t nparts = size;  // partition into n_procs parts
    
    real_t *tpwgts = (real_t*)malloc(nparts * ncon * sizeof(real_t));
    for(int i = 0; i < nparts * ncon; i++) {
        tpwgts[i] = 1.0 / nparts;
    }
    
    real_t ubvec[1] = {1.01};  // Tighter imbalance tolerance (1% instead of 5%)
    idx_t options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_DBGLVL] = 0;  // Suppress ParMETIS debug output
    options[METIS_OPTION_UFACTOR] = 1;  // Minimize load imbalance (default is 30)
    
    idx_t *part = (idx_t*)malloc(nvtxs_local * sizeof(idx_t));
    
    // Initialize part with current assignment (oldpe)
    for (int i = 0; i < nvtxs_local; i++) {
        part[i] = local_objects[i].oldpe;
    }
    
    // Set migration costs (vsize) - uniform cost (all objects equally expensive to migrate)
    idx_t *vsize = (idx_t*)malloc(nvtxs_local * sizeof(idx_t));
    for (int i = 0; i < nvtxs_local; i++) {
        vsize[i] = 1;  // Uniform migration cost
    }
    
    idx_t edgecut;

    // Compute initial load per rank (before partitioning)
    double local_initial_load = 0;
    
    for(int i = 0; i < nvtxs_local; i++) {
        local_initial_load += local_objects[i].wallTime;
    }
    
    double global_initial_sum_load;
    double global_initial_max_load;
    MPI_Reduce(&local_initial_load, &global_initial_sum_load, 1, 
                  MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    MPI_Reduce(&local_initial_load, &global_initial_max_load, 1, 
                  MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    
    if (rank == 0) {    
        double avg_load_before = global_initial_sum_load / size;
        printf("\n=== Before Partitioning ===\n");        
        printf("\t Max rank load = %f\n", global_initial_max_load);
        printf("\t Avg rank load = %f\n", avg_load_before);
        printf("\t Total load = %f\n", global_initial_sum_load);
        printf("\t Load imbalance (max/avg) = %.3f\n", global_initial_max_load / avg_load_before);
    } 
    
    // Build a vtx -> oldpe mapping for computing communication stats before partitioning
    std::vector<int> vtx_to_oldpe(total_objs);
    for (const auto& obj : all_objects) {
        int vtx = objid_to_vtx[obj.id];
        vtx_to_oldpe[vtx] = obj.oldpe;
    }
    
    // Compute internal and external communication BEFORE partitioning
    // (based on initial oldpe assignments)
    // Only compute on rank 0 to avoid counting edges multiple times
    double local_internal_comm_before = 0.0;
    double local_external_comm_before = 0.0;
    
    if (rank == 0) {
        for (const auto& edge : all_comm) {
            auto from_it = objid_to_vtx.find(edge.from_obj);
            auto to_it = objid_to_vtx.find(edge.to_obj);
            
            if (from_it != objid_to_vtx.end() && to_it != objid_to_vtx.end()) {
                int from_vtx = from_it->second;
                int to_vtx = to_it->second;
                
                int from_oldpe = vtx_to_oldpe[from_vtx];
                int to_oldpe = vtx_to_oldpe[to_vtx];
                
                // Skip edges where oldpe is invalid (not in [0, size))
                if (from_oldpe < 0 || from_oldpe >= size || to_oldpe < 0 || to_oldpe >= size) {
                    printf("ERROR: Edge from obj %d to obj %d has unknown partition assignments\n", 
                           edge.from_obj, edge.to_obj);
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
                
                if (from_oldpe == to_oldpe) {
                    local_internal_comm_before += edge.bytes;
                } else {
                    local_external_comm_before += edge.bytes;
                }
            } else {
                printf("WARNING: Edge from obj %d to obj %d has unknown object IDs\n", 
                       edge.from_obj, edge.to_obj);
                       MPI_Abort(MPI_COMM_WORLD, 1);
            }
        }
        printf("\t Internal communication = %.5f MB\n", local_internal_comm_before / (1024.0 * 1024.0));
        printf("\t External communication = %.5f MB\n", local_external_comm_before / (1024.0 * 1024.0));
        printf("\t Total communication = %.5f MB (total edges %d)\n", (local_internal_comm_before + local_external_comm_before) / (1024.0 * 1024.0), (int)all_comm.size());
    }

    // Use AdaptiveRepart since we have an existing partition (oldpe)
    real_t itr = atof(argv[2]);  // Migration control factor (lower = more aggressive rebalancing, prioritize load balance)
    
    if (rank == 0) {
        printf("\n=== Running ParMETIS_V3_AdaptiveRepart (itr=%.3f, ubvec=%.3f, vsize=uniform) ===\n", itr, ubvec[0]);
    }
    
    MPI_Comm comm = MPI_COMM_WORLD;
    int result = ParMETIS_V3_AdaptiveRepart(
        vtxdist,
        xadj,
        adjncy,
        vwgt,
        vsize,     // vertex size for migration cost
        adjwgt,
        &wgtflag,
        &numflag,
        &ncon,
        &nparts,
        tpwgts,
        ubvec,
        &itr,
        options,
        &edgecut,
        part,
        &comm
    );

    if(rank == 0) {
        if(result == METIS_OK) {
            printf("\n=== Adaptive Repartitioning succeeded ===\n");
            printf("Edge cut = %d\n", (int)edgecut);
            // print new vs old partition assignments
            // printf("VertexID\tOldPart\tNewPart\n");
            // for(int i = 0; i < nvtxs_local; i++) {
            //     printf("%d\t%d\t%d\n", (int)(vtxdist[rank] + i), local_objects[i].oldpe, part[i]);
            // }

        } else {
            printf("Repartitioning failed with result = %d\n", result);
        }
    }
    
    // Compute load per partition (each rank computes for its local objects)
    double local_partition_loads[size];
    for (int i = 0; i < size; i++) {
        local_partition_loads[i] = 0.0;
    }
    
    for(int i = 0; i < nvtxs_local; i++) {
        int target_part = part[i];
        local_partition_loads[target_part] += local_objects[i].wallTime;
    }
    
    // Sum up partition loads from all ranks
    double global_partition_loads[size];
    MPI_Reduce(local_partition_loads, global_partition_loads, size, 
               MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    
    if (rank == 0) {
        double max_load = 0.0;
        double total_load = 0.0;
        for (int i = 0; i < size; i++) {
            total_load += global_partition_loads[i];
            if (global_partition_loads[i] > max_load) {
                max_load = global_partition_loads[i];
            }
        }
        double avg_load = total_load / size;
        printf("\n=== After Partitioning ===\n");
        printf("Max partition load = %f\n", max_load);
        printf("Avg partition load = %f\n", avg_load);
        printf("Total load = %f\n", total_load);
        printf("Load imbalance = %.3f\n", max_load / avg_load);
    }
    
    // Compute migrations: count how many objects changed processors
    int local_migrations = 0;
    for (int i = 0; i < nvtxs_local; i++) {
        if (local_objects[i].oldpe != part[i]) {
            local_migrations++;
        }
    }
    
    int total_migrations = 0;
    MPI_Reduce(&local_migrations, &total_migrations, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    
    if (rank == 0) {
        printf("Number of migrations = %d (%.1f%%)\n", 
               total_migrations, 100.0 * total_migrations / total_objs);
    }
    
    // Compute internal and external communication
    // First, gather all partition assignments to all ranks
    std::vector<idx_t> all_parts(total_objs); // TODO: the final partitions are missing objects somehow...
    
    // Each rank contributes its local partition assignments
    std::vector<idx_t> send_parts(total_objs, -1);
    for (int i = 0; i < nvtxs_local; i++) {
        // Use the object ID to find the correct global vertex index
        int obj_id = local_objects[i].id;
        int global_vtx = objid_to_vtx[obj_id];
        send_parts[global_vtx] = part[i];
        assert(part[i] >= 0 && part[i] < size);
        // printf("Rank %d: Object ID %d (global vtx %d) -> part %d\n", 
        //        rank, obj_id, global_vtx, (int)part[i]);
    }
    
    // Use Allreduce with MAX to combine (since non-owned entries are -1)
    MPI_Allreduce(send_parts.data(), all_parts.data(), total_objs,
                  MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    
    // Now compute communication stats
    // Only compute on rank 0 to ensure we count all edges exactly once
    double local_internal_comm = 0.0;
    double local_external_comm = 0.0;
    
    if (rank == 0) {
        printf("\n=== Communication After Repartitioning (%d edges) ===\n", (int)all_comm.size());
        for (const auto& edge : all_comm) {
            auto from_it = objid_to_vtx.find(edge.from_obj);
            auto to_it = objid_to_vtx.find(edge.to_obj);
            
            if (from_it != objid_to_vtx.end() && to_it != objid_to_vtx.end()) {
                int from_vtx = from_it->second;
                int to_vtx = to_it->second;
                
                int from_part = all_parts[from_vtx];
                int to_part = all_parts[to_vtx];
                
                // Skip edges where partition assignment is unknown (-1)
                if (from_part <= -1 || to_part <= -1) {
                    printf("ERROR: Edge from obj %d (from %d) to obj %d (to %d) has unknown partition assignments\n", 
                           edge.from_obj, from_part, edge.to_obj, to_part);
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
                
                if (from_part == to_part) {
                    local_internal_comm += edge.bytes;
                } else {
                    local_external_comm += edge.bytes;
                }
            }
            else {
                printf("ERROR: Edge from obj %d to obj %d has unknown object IDs\n", 
                       edge.from_obj, edge.to_obj);
                       MPI_Abort(MPI_COMM_WORLD, 1);
            }
        }
    }

    
    if (rank == 0) {
        printf("Internal communication = %.5f MB\n", local_internal_comm / (1024.0 * 1024.0));
        printf("External communication = %.5f MB\n", local_external_comm / (1024.0 * 1024.0));
        printf("Total communication = %.5f MB (total edges %d)\n", (local_internal_comm + local_external_comm) / (1024.0 * 1024.0), (int)all_comm.size());
    }
    
    // Print partition assignments
    for(int i = 0; i < nvtxs_local; i++) {
        int global_vtx = vtxdist[rank] + i;
        // printf("Rank %d: Object ID %d (global vtx %d, oldpe=%d) -> part %d\n", 
        //        rank, local_objects[i].id, global_vtx, local_objects[i].oldpe, (int)part[i]);
    }

    // Write results back to JSON file (only rank 0)
    if (rank == 0) {
        // Update all_objects with new partition assignments from all_parts
        for (auto& obj : all_objects) {
            int vtx = objid_to_vtx[obj.id];
            obj.newpe = all_parts[vtx];
        }
        
        // Update the JSON data
        for (auto& [key, value] : jsonData["objData"].items()) {
            int obj_id = value["id"];
            // Find the object in all_objects
            auto it = std::find_if(all_objects.begin(), all_objects.end(),
                                   [obj_id](const ObjectData& o) { return o.id == obj_id; });
            if (it != all_objects.end()) {
                value["newpe"] = it->newpe;
            }
        }
        
        // Write to output file
        std::string output_file = std::string(json_file) + ".parmetis_out.json";
        std::ofstream out(output_file);
        if (out.is_open()) {
            out << jsonData.dump(2);  // Pretty print with 2-space indentation
            out.close();
            printf("\n=== Results written to %s ===\n", output_file.c_str());
        } else {
            fprintf(stderr, "ERROR: Could not open output file %s\n", output_file.c_str());
        }
    }

    // Cleanup
    free(vtxdist);
    free(xadj);
    free(adjncy);
    free(vwgt);
    free(vsize);
    free(adjwgt);
    free(part);
    free(tpwgts);

    MPI_Finalize();
    return 0;
}
