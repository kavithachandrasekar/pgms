
This program is meant to be run on 2 physical nodes (or for baseline comparison, 1 node with 2 processes), and with an even number of PEs p =2q. 

Each PE i on node 0 sends k (100 hardwired?)  messages of some size to pe q+i (i.e. the corresponding pe
on the node 0). After receiving all the messages, the receiver pes on node 0 sends an ack back to pe 0,
where PE0 prints the execution time. 

To continue this for multiple message sizes, pe 0 should send a message to all pes in the first half,
which start the benchmark again for the next size, until the largest size we care about is reached.

It may be simpler for now to just do all power of 2 message sizes betwee 8 and 64k, but maybe instead of doubling, we could quadruple message size (to redduce the number of experiments).. 8, 32, 128, ...


