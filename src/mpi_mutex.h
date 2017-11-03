#ifndef MPI_MUTEX_H
#define MPI_MUTEX_H

#include <array>
#include <memory>
#include <mpi.h>

class MPIMutex
{
public:
	MPIMutex(const std::array<int, 3> &tags, MPI_Comm communicator = MPI_COMM_WORLD);
	~MPIMutex();

	void lock();
	void unlock();

private:
	class MessagesReceiver;


	const int messageLockRequest;
	const int messageLockPermission;
	const int messageLeave;

	const MPI_Comm communicator;
	const int commRank;
	const int commSize;

	const std::unique_ptr<MessagesReceiver> messagesReceiver;


	int getCommRank();
	int getCommSize();
};

#endif
