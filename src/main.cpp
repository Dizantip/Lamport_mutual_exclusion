#include "mpi_mutex.h"
#include <iostream>
#include <mutex>
#include <mpi.h>

int main(int argc, char *argv[])
{
	int mpiProvidedLevel;

	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &mpiProvidedLevel);
	if (mpiProvidedLevel < MPI_THREAD_MULTIPLE)
	{
		std::cout << "MPI DOES NOT PROVIDES MPI_THREAD_MULTIPLE" << std::endl;
		return 1;
	}

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	{
		MPIMutex mutex({0, 1, 2});

		int iterations = 50;
		if (rank < 2)
		{
			iterations += 50;
			if (rank == 0)
			{
				iterations += 50;
			}
		}

		for (int i = 0; i < iterations; ++i)
		{
			std::lock_guard<decltype(mutex)> lock(mutex);
			std::cout << "Hello from " << rank << std::endl;
		}
	}

	MPI_Finalize();

	return 0;
}
