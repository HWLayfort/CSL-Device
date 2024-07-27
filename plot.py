import matplotlib.pyplot as plt
import numpy as np

# Data
categories = ['Semaphore', 'Mutex', 'RW Lock', 'RW Semaphore']
data1 = [194, 1224, 1826, 1176]
data2 = [190, 1160, 1739, 1143]

#288, 289, 167, 166, 194, 190
#1183, 1181, 1889, 1885, 1224, 1160
#1423, 1385, 3038, 2856, 1826, 1739
#1385, 1292, 2901, 2821, 1176, 1143

x = np.arange(len(categories))  # label locations
width = 0.25  # bar width

fig, ax = plt.subplots()

# Bars
rects1 = ax.bar(x - width/2, data1, width, label='Sequential', color='lightgrey', edgecolor='black')
rects2 = ax.bar(x + width/2, data2, width, label='Random', color='white', hatch='//', edgecolor='black')

# Labels, titles, and legend
ax.set_ylabel('MiB/s')
ax.set_ylim(0, max(data1 + data2) * 1.1)
ax.set_title('Synchronization Type I/O Performance Comparison')
# ax.set_xlabel('numjobs')
ax.set_xticks(x)
ax.set_xticklabels(categories)
ax.legend()

# Grid
ax.yaxis.grid(True, linestyle='--', which='both', color='grey', alpha=0.7)

# Display the plot
plt.show()