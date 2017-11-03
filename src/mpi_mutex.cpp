#include "mpi_mutex.h"
#include "lamport_timestamp_provider.h"
#include <utility>
#include <mutex>
#include <thread>
#include <queue>
#include <vector>
#include <list>
#include <functional>
#include <algorithm>
#include <iostream>


#define MPI_ERROR_ASSERT(func) { const auto err = (func); if (err != MPI_SUCCESS) { std::cerr << "MPI ERROR AT " __FILE__ " (" << __LINE__ << "): " << err << std::endl; MPI_Abort(MPI_COMM_WORLD, err); } }
#define NON_MPI_ERROR_ASSERT(cond) { if (!(cond)) { std::cerr << "ASSERT FAILED AT " __FILE__ " (" << __LINE__ << ") : " << #cond << std::endl; MPI_Abort(MPI_COMM_WORLD, 1); } }


namespace
{
	// avoid using C++14
	template<typename T, typename... Args>
	std::unique_ptr<T> makeUnique(Args&&... args)
	{
		return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
	}


	struct Event
	{
		LamportTimestampProvider::timestamp_type timestamp;
		int rank;
	};


	bool operator==(const Event &left, const Event &right)
	{
		return ((left.timestamp == right.timestamp) && (left.rank == right.rank));
	}

	bool operator!=(const Event &left, const Event &right)
	{
		return !(left == right);
	}


	bool operator<(const Event &left, const Event &right)
	{
		return ( (left.timestamp < right.timestamp) || ((left.timestamp == right.timestamp) && (left.rank < right.rank)) );
	}

	bool operator>(const Event &left, const Event &right)
	{
		return (right < left);
	}


	bool operator<=(const Event &left, const Event &right)
	{
		return ((left < right) || (left == right));
	}

	bool operator>=(const Event &left, const Event &right)
	{
		return (right <= left);
	}
}


class MPIMutex::MessagesReceiver
{
public:
	MessagesReceiver(MPIMutex &owner)
		: owner(owner),
		  excludedNodes(owner.commSize, false),
		  events{eventGreater},
		  receivedPermissions(owner.commSize, false),
		  thisWantsLock(false), thisOwnsLock(false),
		  thisLockRequestEvent{0, 0}
	{
		MPI_ERROR_ASSERT(MPI_Barrier(owner.communicator))
		receiveThread = std::move(std::thread([this]{ receiveThreadRoutine(); }));
	}

	~MessagesReceiver()
	{
		exit();
		if (receiveThread.joinable())
		{
			receiveThread.join();
		}
	}


	void lock()
	{
		auto timestamp = registerEvent();
		MPI_ERROR_ASSERT(MPI_Ssend(&timestamp, 1, MPI_UNSIGNED_LONG_LONG, owner.commRank, owner.messageLockRequest, owner.communicator))
		MPI_ERROR_ASSERT(MPI_Recv(&timestamp, 1, MPI_UNSIGNED_LONG_LONG, owner.commRank, owner.messageLockPermission, owner.communicator, MPI_STATUS_IGNORE))
	}

	void unlock()
	{
		const auto timestamp = getCurrentTimestamp();
		MPI_ERROR_ASSERT(MPI_Ssend(&timestamp, 1, MPI_UNSIGNED_LONG_LONG, owner.commRank, owner.messageLockPermission, owner.communicator))
	}

private:
	static const std::greater<Event> eventGreater;

	MPIMutex &owner;
	LamportTimestampProvider timestampProvider;
	std::vector<bool> excludedNodes;
	std::priority_queue<Event, std::vector<Event>, std::greater<Event> > events;
	std::vector<bool> receivedPermissions;
	std::list<std::pair<LamportTimestampProvider::timestamp_type, MPI_Request> > sendedMessages;
	bool thisWantsLock, thisOwnsLock;
	Event thisLockRequestEvent;
	mutable std::mutex mutex;
	std::thread receiveThread;


	LamportTimestampProvider::timestamp_type registerEvent()
	{
		std::lock_guard<decltype(mutex)> lock(mutex);
		return timestampProvider.registerEvent();
	}

	LamportTimestampProvider::timestamp_type registerEvent(LamportTimestampProvider::timestamp_type event)
	{
		std::lock_guard<decltype(mutex)> lock(mutex);
		return timestampProvider.registerEvent(event);
	}

	LamportTimestampProvider::timestamp_type getCurrentTimestamp() const
	{
		std::lock_guard<decltype(mutex)> lock(mutex);
		return timestampProvider.getCurrentTimestamp();
	}


	void receiveThreadRoutine()
	{
		while (!isAllNodesExcluded())
		{
			Event event;
			MPI_Status status;

			MPI_ERROR_ASSERT(MPI_Recv(&event.timestamp, 1, MPI_UNSIGNED_LONG_LONG, MPI_ANY_SOURCE, MPI_ANY_TAG, owner.communicator, &status))
			event.rank = status.MPI_SOURCE;

			registerEvent(event.timestamp);

			if (status.MPI_TAG == owner.messageLockRequest)
			{
				if (thisOwnsLock)
				{
					NON_MPI_ERROR_ASSERT((!thisWantsLock) && (event.rank != owner.commRank))
					events.push(event);
				}
				else if (thisWantsLock)
				{
					NON_MPI_ERROR_ASSERT(event.rank != owner.commRank)
					if (!eventGreater(thisLockRequestEvent, event))
					{
						events.push(event);
					}
					else
					{
						event.timestamp = getCurrentTimestamp();
						sendMessage(event.rank, owner.messageLockPermission, event.timestamp);
					}
				}
				else
				{
					if (event.rank == owner.commRank)
					{
						thisWantsLock = true;
						thisLockRequestEvent = event;

						broadcastMessage(owner.messageLockRequest, thisLockRequestEvent.timestamp);
						tryLock();
					}
					else
					{
						event.timestamp = getCurrentTimestamp();
						sendMessage(event.rank, owner.messageLockPermission, event.timestamp);
					}
				}
			}
			else if (status.MPI_TAG == owner.messageLockPermission)
			{
				if (event.rank == owner.commRank)
				{
					NON_MPI_ERROR_ASSERT(thisOwnsLock)
					thisOwnsLock = false;
					sendEventsQueue();
				}
				else
				{
					NON_MPI_ERROR_ASSERT((thisWantsLock) && (!thisOwnsLock) && (!receivedPermissions[event.rank]))
					receivedPermissions[event.rank] = true;
					tryLock();
				}
			}
			else
			{
				NON_MPI_ERROR_ASSERT(status.MPI_TAG == owner.messageLeave)

				excludedNodes[event.rank] = true;

				if (event.rank == owner.commRank)
				{
					NON_MPI_ERROR_ASSERT((!thisWantsLock) && (!thisOwnsLock))
					broadcastMessage(owner.messageLeave, getCurrentTimestamp());
				}
				else
				{
					sendMessage(event.rank, owner.messageLeave, getCurrentTimestamp());

					if (thisWantsLock)
					{
						tryLock();
					}
				}
			}

			popDeliveredMessages();
		}

		waitMessagesDelivery();
	}


	void sendMessage(int rank, int message, LamportTimestampProvider::timestamp_type timestamp)
	{
		if (rank == owner.commRank)
		{
			NON_MPI_ERROR_ASSERT(message == owner.messageLockPermission)
			MPI_ERROR_ASSERT(MPI_Ssend(&timestamp, 1, MPI_UNSIGNED_LONG_LONG, rank, message, owner.communicator))
		}
		else
		{
			sendedMessages.push_back({timestamp, MPI_Request{}});
			const auto iter = --sendedMessages.end();
			MPI_ERROR_ASSERT(MPI_Isend(&iter->first, 1, MPI_UNSIGNED_LONG_LONG, rank, message, owner.communicator, &iter->second))
		}
	}

	void popDeliveredMessages()
	{
		for (auto iter = sendedMessages.begin(); iter != sendedMessages.end(); ++iter)
		{
			int testResult;
			MPI_ERROR_ASSERT(MPI_Test(&iter->second, &testResult, MPI_STATUS_IGNORE))

			if (testResult)
			{
				iter = sendedMessages.erase(iter);
			}
		}
	}

	void waitMessagesDelivery()
	{
		for (auto iter = sendedMessages.begin(); iter != sendedMessages.end(); ++iter)
		{
			MPI_ERROR_ASSERT(MPI_Wait(&iter->second, MPI_STATUS_IGNORE))
			iter = sendedMessages.erase(iter);
		}
	}


	void broadcastMessage(const int message, LamportTimestampProvider::timestamp_type timestamp)
	{
		for (int dstRank = 0; dstRank < owner.commSize; ++dstRank)
		{
			if ( (!excludedNodes[dstRank]) && (dstRank != owner.commRank) )
			{
				sendMessage(dstRank, message, timestamp);
			}
		}
	}


	void tryLock()
	{
		if (isAllPermissionsReceived())
		{
			std::fill(receivedPermissions.begin(), receivedPermissions.end(), false);

			thisWantsLock = false;
			thisOwnsLock = true;

			sendMessage(owner.commRank, owner.messageLockPermission, getCurrentTimestamp());
		}
	}

	bool isAllPermissionsReceived() const
	{
		for (int rank = 0; rank < owner.commSize; ++rank)
		{
			if ((!receivedPermissions[rank]) && (!excludedNodes[rank]) && (rank != owner.commRank))
			{
				return false;
			}
		}

		return true;
	}

	bool isAllNodesExcluded() const
	{
		for (bool excluded : excludedNodes)
		{
			if (!excluded)
			{
				return false;
			}
		}

		return true;
	}


	void sendEventsQueue()
	{
		const auto timestamp = getCurrentTimestamp();
		while (!events.empty())
		{
			const auto event = events.top();
			NON_MPI_ERROR_ASSERT(event.rank != owner.commRank)
			events.pop();

			if (!excludedNodes[event.rank])
			{
				sendMessage(event.rank, owner.messageLockPermission, timestamp);
			}
		}
	}


	void exit()
	{
		const auto timestamp = getCurrentTimestamp();
		MPI_ERROR_ASSERT(MPI_Ssend(&timestamp, 1, MPI_UNSIGNED_LONG_LONG, owner.commRank, owner.messageLeave, owner.communicator))
	}

};

const std::greater<Event> MPIMutex::MessagesReceiver::eventGreater;



MPIMutex::MPIMutex(const std::array<int, 3> &tags, MPI_Comm communicator)
	: messageLockRequest(tags[0]),
	  messageLockPermission(tags[1]),
	  messageLeave(tags[2]),
	  communicator(communicator),
	  commRank(getCommRank()), commSize(getCommSize()),
	  messagesReceiver(makeUnique<MessagesReceiver>(*this))
{
}

MPIMutex::~MPIMutex() = default;


void MPIMutex::lock()
{
	messagesReceiver->lock();
}

void MPIMutex::unlock()
{
	messagesReceiver->unlock();
}


int MPIMutex::getCommRank()
{
	int result;
	MPI_ERROR_ASSERT(MPI_Comm_rank(communicator, &result))
	return result;
}

int MPIMutex::getCommSize()
{
	int result;
	MPI_ERROR_ASSERT(MPI_Comm_size(communicator, &result))
	return result;
}


#undef NON_MPI_ERROR_ASSERT
#undef MPI_ERROR_ASSERT
