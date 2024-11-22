import matplotlib.pyplot as plt
import numpy as np
import math
import random

chares_per_node_x=2
chares_per_node_y=8
colors = [4, 7, 15, 2, 13, 3, 11, 10, 5, 1, 6, 12, 9, 15, 8,0]
#[4, 2, 15, 7, 0, 3, 8, 1, 5, 10, 6, 12, 9, 15, 11,13]
# random.sample(range(20), 16)
x_chares = 32
y_chares = 32

data_pe = np.loadtxt("pe",dtype=int)
data_x = np.loadtxt("x",dtype=int)
data_y = np.loadtxt("y",dtype=int)

total =[]
count = 0
for i in range(0,y_chares):
    row=[]
    for j in range(0,x_chares):
      pe_index = data_pe[count]

      #index = math.floor(i/chares_per_node_x)*chares_per_node_y+math.floor(j/chares_per_node_y)
      #if(j>=4 and j<=7 and i==3):
      #  index = index+4
      row.append(0)#colors[pe_index])
    total.append(row)
for i in range(0,x_chares*y_chares):
  total[data_y[i]][data_x[i]] = colors[data_pe[i]]
a = np.array(total)
plt.imshow(a, cmap="jet")
plt.show()
