# Lamport_mutual_exclusion
C++ project that implements the algorithm of Lamport timestamps and MPI-based mutex for processes synchronization

## Simple make build
```
mkdir make && cd make && cmake ../cmake/ -G "Unix Makefiles"
```
After successful build you may run builded MPI program
```
mpirun -np 2 ./lamport_mutual_exclusion
```
