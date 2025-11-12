#%%

import matplotlib.pyplot as plt
import matplotlib.cm as cm

import numpy as np
import math
import random
import json
import sys
from matplotlib.patches import Patch

from math import floor


def plot_pes(data_old_pe, data_new_pe, positions, mode, highlight, three_d=True, show='both'):
  num_colors = max(data_old_pe) + 1
  cmap = cm.get_cmap('tab20', num_colors) if num_colors <= 20 else cm.get_cmap('hsv', num_colors)
  colors = [cmap(i) for i in range(num_colors)]
  
  # make a scatter plot of objects at their x and y poitiion
  x = [pos[0] / max(pos[0] for pos in positions) for pos in positions]
  y = [pos[1] / max(pos[1] for pos in positions) for pos in positions]
  if three_d: z = [pos[2] / max(pos[2] for pos in positions) for pos in positions]

  # Determine number of subplots based on show parameter
  num_plots = 2 if show == 'both' else 1
  figsize = (12, 6) if show == 'both' else (6, 6)

  if three_d:
    fig, axs_temp = plt.subplots(1, num_plots, figsize=figsize, subplot_kw={'projection': '3d'})
  else:
    fig, axs_temp = plt.subplots(1, num_plots, figsize=figsize)
  
  # Make axs always a list for consistent indexing
  axs = [axs_temp] if num_plots == 1 else axs_temp

  # Filter points to show only certain PE values (e.g., show only even PE values)

  show_condition = lambda pe: pe in highlight # Change this condition as needed
  
  xmax = max(pos[0] for pos in positions)
  ymax = max(pos[1] for pos in positions)
  if three_d: zmax = max(pos[2] for pos in positions)
  xmin = min(pos[0] for pos in positions)
  ymin = min(pos[1] for pos in positions)
  if three_d: zmin = min(pos[2] for pos in positions)
  
  # Set limits for all axes and hide tick labels
  for ax in axs:
    ax.set_xlim([xmin/xmax, 1])
    ax.set_ylim([ymin/ymax, 1])
    if three_d: ax.set_zlim([zmin/zmax, 1])
    
    # Hide axis tick labels
    ax.set_xticklabels([])
    ax.set_yticklabels([])
    if three_d: ax.set_zticklabels([])

  # Filter old PE data
  old_indices = [i for i, pe in enumerate(data_old_pe) if show_condition(pe)]
  old_x_filtered = [x[i] for i in old_indices]
  old_y_filtered = [y[i] for i in old_indices]
  if three_d: old_z_filtered = [z[i] for i in old_indices]
  old_colors_filtered = [colors[data_old_pe[i]] for i in old_indices]
  
  # Filter new PE data
  new_indices = [i for i, pe in enumerate(data_new_pe) if show_condition(pe)]
  new_x_filtered = [x[i] for i in new_indices]
  new_y_filtered = [y[i] for i in new_indices]
  if three_d: new_z_filtered = [z[i] for i in new_indices]
  new_colors_filtered = [colors[data_new_pe[i]] for i in new_indices]

  if show in ['old', 'both']:
    idx = 0
    if three_d:
      axs[idx].scatter(old_x_filtered, old_y_filtered, old_z_filtered, c=old_colors_filtered, s=100)
    else:
      axs[idx].scatter(old_x_filtered, old_y_filtered, c=old_colors_filtered, s=100)
    
    # Add legend to old subplot
    unique_pes = sorted(set(data_old_pe))
    handles = [Patch(color=colors[pe], label=f'{mode} {pe}') for pe in unique_pes]
    axs[idx].legend(handles=handles)
  
  if show in ['new', 'both']:
    idx = 1 if show == 'both' else 0
    if three_d:
      axs[idx].scatter(new_x_filtered, new_y_filtered, new_z_filtered, c=new_colors_filtered, s=100)
    else:
      axs[idx].scatter(new_x_filtered, new_y_filtered, c=new_colors_filtered, s=100)
    
    # Add legend to new subplot
    unique_pes = sorted(set(data_new_pe))
    handles = [Patch(color=colors[pe], label=f'{mode} {pe}') for pe in unique_pes]
    axs[idx].legend(handles=handles)

  plt.tight_layout()
  plt.show()

def plot(json_file, mode = 'pe', highlight = None, three_d=True, show='both'):
  """
  Plot PE/node mappings from JSON file.
  
  Parameters:
  - json_file: path to JSON file
  - mode: 'pe' or 'node' to plot PE or node mappings
  - highlight: list of PE/node IDs to highlight (None = show all)
  - three_d: whether to use 3D plots (auto-detected if not specified)
  - show: 'old', 'new', or 'both' to control which plots to display
  """
  with open(json_file, 'r') as f:
    data = json.load(f)
  
  objects = data['objData']
  data_old_pe = []
  data_new_pe = []
  positions = []
  old_nodes = []
  new_nodes = []
  num_nodes = data['n_nodes']
  
  print("Number of objects:", len(objects))
  print("Number of nodes:", num_nodes)
  
  for obj in objects:
    data_old_pe.append(objects[obj]['oldpe'])
    data_new_pe.append(objects[obj]['newpe'])
    positions.append((objects[obj]['position'][0], objects[obj]['position'][1], objects[obj]['position'][2]))
    if (num_nodes == 1):
      old_nodes.append(0)
      new_nodes.append(0)
    else:
      old_nodes.append(floor(objects[obj]['oldpe'] / (num_nodes  - 1)))
      new_nodes.append(floor(objects[obj]['newpe'] / (num_nodes  - 1)))
    
  if highlight is None:
    highlight = list(range(max(data_old_pe) + 1))
  
  three_d = False
  for pos in positions:
    if len(pos) < 3:
      break
    if pos[2] != 0.0:
      three_d = True
      break

  if (mode == 'node'):
    plot_pes(old_nodes, new_nodes, positions, mode, highlight, three_d, show)
  else:
    plot_pes(data_old_pe, data_new_pe, positions, mode, highlight, three_d, show)
    
  print("Number of objects:", len(data_old_pe))
  print("Number of PEs:", max(data_old_pe) + 1)
  print("Number of nodes:", num_nodes)
  print("Number of migrations:", sum(1 for old_pe, new_pe in zip(data_old_pe, data_new_pe) if old_pe != new_pe))
  
  # compute load per pe
  num_pes = max(data_old_pe) + 1
  load_old = [0] * num_pes
  load_new = [0] * num_pes
  
  for obj in objects:
    load_old[objects[obj]['oldpe']] += objects[obj]['wallTime']
    load_new[objects[obj]['newpe']] += objects[obj]['wallTime']
    
  print("before LB: max load =", max(load_old), "avg load =", sum(load_old)/num_pes)
  print("after  LB: max load =", max(load_new), "avg load =", sum(load_new)/num_pes)
  
  
  
# %%
plot("/Users/maya/ppl/pgms/charm++/lb_sim/greedy_refine_sim/lbdump.json", 'pe', highlight=None, show='old')

# %%
plot("/Users/maya/ppl/pgms/charm++/lb_sim/diffusion_sim/lbdump-cent.json", 'pe', highlight=None, show='new')

# %%
plot("/Users/maya/ppl/pgms/charm++/lb_sim/metis/lbdump.json", 'pe', highlight=None, show='new')

# %%
plot("/Users/maya/software/charm-diffusionlb/examples/charm++/load_balancing/stencil3d/lbdump.json", 'node', highlight=None)

# %%
