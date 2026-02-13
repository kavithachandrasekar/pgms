Simulation and visualizaiton tools for various Charm++ load balancing strategies.
- each strategy generates a json output that can be parsed by the chare_mapping_vis tool for visualization and stats
- all strategies can accept a traditional lbdata.dat.0 dump binary file, but Diffusion also supports reading from the json format
- stencil3d_lb is a basic charm++ program to generate data/test out strategies.
- load imbalance can also be injected via strategies in sim_headers/common_lbsim.h

General workflow and usage:
- Generate an lbdump binary file from stencil3d_lb or any other Charm++ program that uses load balancing:
    - the binary lbdump is produced by adding the `+LBDump <lb iter to dump>` option. Currently only works with CentralLB (there's some initial support for a json dump in DiffusionLB)
    - this captures load, location, and communication patterns for all chares
    - more details in the README under stencil3d_lb
- Run a simulated load balancing strategy on this lbdump. This will produce a json output which describes the new load distribution and all other object qualities (disclaimer: the communication patterns in this json are buggy)
- Visualize the resulting json data with the chare_mapping_vis tool
