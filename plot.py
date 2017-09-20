#!/usr/bin/python

import sys

import matplotlib.pyplot as plt
import numpy as np

dir = sys.argv[1]

def plotdata(xlabel, ylabel, x, y_mgm, y_distrm, file):
    fig = plt.figure()

    ax1 = fig.add_subplot(111)

    ax1.spines['top'].set_visible(False)
    ax1.spines['bottom'].set_visible(False)
    ax1.spines['right'].set_visible(False)
    ax1.spines['left'].set_visible(False)

    plt.grid(True, 'major', 'y', ls='-', lw=.5, c='k', alpha=.3)

    plt.tick_params(axis='both', which='both', bottom='off', top='off', labelbottom='on', left='off', right='off', labelleft='on')

    #ax1.set_title(title)
    ax1.set_xlabel(xlabel)
    ax1.set_ylabel(ylabel)

    ax1.plot(x, y_mgm, lw=2.5, c='#1b9e77', label='ResMGM')
    ax1.plot(x, y_distrm, lw=2.5, c='#d95f02', label='DistRM')

    leg = ax1.legend(loc=0, framealpha=0.5)

    fig.savefig(dir + 'plots/' + file + '.png')
    fig.savefig(dir + 'plots/' + file + '.pdf')

data = np.genfromtxt(dir + 'var_dom/plot-var_dom.csv', delimiter=';', names='tiles,tlmmgm,instmgm,tlmdistrm,instdistrm')

plotdata('Number of tiles', 'Number of TLM requests', data['tiles'], data['tlmmgm'], data['tlmdistrm'], 'var_dom-tlm')

plotdata('Number of tiles', 'Number of instructions', data['tiles'], data['instmgm'], data['instdistrm'], 'var_dom-inst')

data = np.genfromtxt(dir + '/var_ag/plot-var_ag.csv', delimiter=';', names='agents,tlmmgm,instmgm,tlmdistrm,instdistrm')

plotdata('Number of agents', 'Number of TLM requests', data['agents'], data['tlmmgm'], data['tlmdistrm'], 'var_ag-tlm')

plotdata('Number of agents', 'Number of instructions', data['agents'], data['instmgm'], data['instdistrm'], 'var_ag-inst')

