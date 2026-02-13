# Diffusion Simulation Infrastructure:

Currently only works on a single node (can't run simulator in parallel), but I think there should be an easy fix for that... Also note that the simulator assumes PE = Nodes (so it doesn't support within-process load balancing that the true Diffusion strategy supports) - if you have a dump file that uses 4 processes with smp 2, the simulator will assume 8 processes.

Usage: `./DiffusionSim <imbalance type> <input> <output> <niters> +LBnoMST`
where
- `imbalance type` is an int (from 0 to 5 rn) that maps to a specific imbalance function. See the argument parsing logic for this, and the function definitions in `../sim_headers/common_lbsim.h`. These are functions that apply synthetic load imbalance to the application objects, in varying patterns. The function is applied via the `obj_imb` call. There's also a relevant `load_setconst` function which sets the loads of all objects to 1 (its currently commented out in Diffusion).
- `input` the input file. can be JSON (only supported in centroid/coordinate mode) or binary 
- `output` not used rn? but we should change the code to write the output dump to "output_name.json"
- `niters` is the number of iterations to run the simulation. imbalance is injected at every iteration using the `imbalance type`. The stats should be printed out at every iteration, with only the json dump at the end.
with the following optional arguments:
- `+LBDiffusionNumNbors <n>` to specify the number of neighbors per node (default is 1 I think)
- you should always use LBnoMST... initially we supported the MST but it was super slow and didn't seem necessary, but might become important again later
- `+LBDiffusionCommOn` use this if you want to use the communication-based strategy. to use the centroid/coordinate strategy, remove this option.
