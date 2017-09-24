#!/usr/bin/python

import sys

import os

import matplotlib.pyplot as plt
import numpy as np

from itertools import chain

dir = sys.argv[1]

def plotdata(data1, data2, title1, title2, ylabel1, ylabel2, x1, x2, y1, y2, file):
    fig = plt.figure(figsize=(14, 8))

    ax = fig.add_subplot(121)

    ax.set_title(title1, fontsize=20)
    ax.title.set_position([0.5, 1.05])

    plt.grid(True, 'major', 'y', ls='-', lw=.5, c='k', alpha=.3)

    plt.tick_params(axis='both', which='both', bottom='on', top='off', labelbottom='on', left='on', right='off', labelleft='on')

    plt.xticks(fontsize=16)
    plt.yticks(fontsize=16)

    ax.set_ylabel(ylabel1, fontsize=19)

    ax2 = ax.twinx()
    ax2.yaxis.set_ticklabels([])

    mgm1 = ax.plot(data1[x1], data1[y1 + 'mgm'], lw=4.5, c='#1b9e77', label='ResMGM')
    mgm2 = ax2.plot(data1[x1], data1[y2 + 'mgm'], lw=4.5, c='#7570b3', label='ResMGM')
    distrm1 = ax.plot(data1[x1], data1[y1 + 'distrm'], lw=4.5, c='#d95f02', label='DistRM')
    distrm2 = ax2.plot(data1[x1], data1[y2 + 'distrm'], lw=4.5, c='#e7298a', label='DistRM')

    ylim = max(list(chain.from_iterable([ax.get_ylim(), ax2.get_ylim()])))
    ax.set_ylim(0, ylim)
    ax2.set_ylim(0, ylim)

    ax = fig.add_subplot(122)

    ax.set_title(title2, fontsize=20)
    ax.title.set_position([0.5, 1.05])

    plt.grid(True, 'major', 'y', ls='-', lw=.5, c='k', alpha=.3)

    plt.tick_params(axis='both', which='both', bottom='on', top='off', labelbottom='on', left='on', right='off', labelleft='on')

    plt.xticks(fontsize=16)
    plt.yticks(fontsize=16)

    ax.yaxis.set_ticklabels([])

    ax2 = ax.twinx()
    ax2.set_ylabel(ylabel2, fontsize=19)

    mgm1 = ax.plot(data2[x2], data2[y1 + 'mgm'], lw=4.5, c='#1b9e77', label='ResMGM')
    mgm2 = ax2.plot(data2[x2], data2[y2 + 'mgm'], lw=4.5, c='#7570b3', label='ResMGM')
    distrm1 = ax.plot(data2[x2], data2[y1 + 'distrm'], lw=4.5, c='#d95f02', label='DistRM')
    distrm2 = ax2.plot(data2[x2], data2[y2 + 'distrm'], lw=4.5, c='#e7298a', label='DistRM')

    ylim = max(list(chain.from_iterable([ax.get_ylim(), ax2.get_ylim()])))
    ax.set_ylim(0, ylim)
    ax2.set_ylim(0, ylim)

    lines1 = list(chain.from_iterable([mgm1, distrm1]))
    lines2 = list(chain.from_iterable([mgm2, distrm2]))

    fig.legend(lines1, ('ResMGM', 'DistRM'), loc='lower left', ncol=2, frameon=False, numpoints=16, fontsize=18)
    fig.legend(lines2, ('ResMGM', 'DistRM'), loc='lower right', ncol=2, frameon=False, numpoints=16, fontsize=18)

    plt.subplots_adjust(top=0.1, bottom=0.03)

    plt.tight_layout()

    fig.savefig(dir + 'plots/' + file + '.png')
    fig.savefig(dir + 'plots/' + file + '.pdf')

def addsubplot(fig, data, id, ylabel, x, y):
    ax = fig.add_subplot(id)

    plt.grid(True, 'major', 'y', ls='-', lw=.5, c='k', alpha=.3)

    plt.tick_params(axis='both', which='both', bottom='on', top='off', labelbottom='on', left='on', right='off', labelleft='on')

    plt.xticks(fontsize=16)
    plt.yticks(fontsize=16)

    ax.set_ylabel(ylabel, fontsize=19)

    mgm = ax.plot(data[x], data[y + 'mgm'], lw=4.5, c='#1b9e77', label='ResMGM')
    distrm = ax.plot(data[x], data[y + 'distrm'], lw=4.5, c='#d95f02', label='DistRM')

    return list(chain.from_iterable([mgm, distrm]))

def plot(title, x, data, file):
    fig = plt.figure(figsize=(14, 8))

    fig.suptitle(title, fontsize=20, ha='center')

    addsubplot(fig, data, 141, 'TLM requests', x, 'tlm')
    addsubplot(fig, data, 142, 'Instructions', x, 'inst')
    addsubplot(fig, data, 143, 'Maximum TLM used (bytes)', x, 'mem')
    lines = addsubplot(fig, data, 144, 'Time (nanoseconds)', x, 't')

    fig.legend(lines, ('ResMGM', 'DistRM'), loc='lower center', ncol=2, frameon=False, numpoints=16, markerscale=2.0, fontsize=18)

    plt.subplots_adjust(top=0.3, bottom=0.03)

    plt.tight_layout()

    fig.savefig(dir + 'plots/' + file + '.png')
    fig.savefig(dir + 'plots/' + file + '.pdf')

run1 = os.path.exists(dir + 'var_dom/plot-var_dom.csv')
if run1:
    data1 = np.genfromtxt(dir + 'var_dom/plot-var_dom.csv', delimiter=';', names='tiles,tlmmgm,instmgm,memmgm,msgmgm,tmgm,tlmdistrm,instdistrm,memdistrm,msgdistrm,tdistrm')

    plot('Increasing number of tiles', 'tiles', data1, 'var_dom')

run2 = os.path.exists(dir + 'var_ag/plot-var_ag.csv')
if run2:
    data2 = np.genfromtxt(dir + '/var_ag/plot-var_ag.csv', delimiter=';', names='agents,tlmmgm,instmgm,memmgm,msgmgm,tmgm,tlmdistrm,instdistrm,memdistrm,msgdistrm,tdistrm')

    plot('Increasing number of agents', 'agents', data2, 'var_ag')

if run1 and run2:
    plotdata(data1, data2, 'Increasing number of tiles', 'Increasing number of agents', 'TLM requests', 'Bytes sent', 'tiles', 'agents', 'tlm', 'msg', 'var_msg')

run3 = os.path.exists(dir + 'var_hyb/plot-var_hyb.csv')
if run3:
    data3 = np.genfromtxt(dir + '/var_hyb/plot-var_hyb.csv', delimiter=';', names='agents,tlmmgm,instmgm,memmgm,msgmgm,tmgm,tlmdistrm,instdistrm,memdistrm,msgdistrm,tdistrm')

    plot('Increasing number of tiles with 100% load', 'agents', data3, 'var_hyb')

