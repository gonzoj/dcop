__dcop__ is a framework that I wrote for my final thesis at university. The framework allows to evaluate different [DCOP](https://en.wikipedia.org/wiki/Distributed_constraint_optimization) algorithms regarding tbeir applicability for resource management in large many-core systems and is built on top of the [Sniper](http://snipersim.org/w/The_Sniper_Multi-Core_Simulator) Multi-Core Simulator.

In order to build and run dcop you have to provide an installation of Sniper 6.1 in ../sniper/sniper-6.1/ that you can download [here](http://snipersim.org/w/Download). dcop also requires Lua 5.1.

To build dcop type:
```sh
$ make
```

To run the tool type:
```sh
$ ./run-dcop ...
```

For a usage description type:
```sh
$ ./run-dcop -h
```
and also:
```sh
$ ./run-dcop -a -- --help
```

