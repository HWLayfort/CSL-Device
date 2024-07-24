import matplotlib.pyplot as plt
import numpy as np

# Data
categories = ['1', '2', '4', '8', '16']
seq = [471, 947, 1733, 2590, 3404]
rand = [456, 743, 1556, 1300, 1009]

x = np.arange(len(categories))  # label locations
width = 0.35  # bar width

fig, ax = plt.subplots()

# Bars
rects1 = ax.bar(x - width/2, seq, width, label='Read', color='lightgrey', edgecolor='black')
rects2 = ax.bar(x + width/2, rand, width, label='Write', color='white', hatch='//', edgecolor='black')

# Labels, titles, and legend
ax.set_ylabel('MiB/s')
ax.set_ylim(0, 3500)
ax.set_title('Write-Read I/O Performance')
ax.set_xlabel('numjobs')
ax.set_xticks(x)
ax.set_xticklabels(categories)
ax.legend()

# Grid
ax.yaxis.grid(True, linestyle='--', which='both', color='grey', alpha=0.7)

# Display the plot
plt.show()