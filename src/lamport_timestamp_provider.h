#ifndef LAMPORT_TIMESTAMP_PROVIDER
#define LAMPORT_TIMESTAMP_PROVIDER

#include <cstdint>

class LamportTimestampProvider
{
public:
	using timestamp_type = std::uint64_t;

	LamportTimestampProvider();

	timestamp_type registerEvent(timestamp_type eventTimestamp);
	timestamp_type registerEvent();

	timestamp_type getCurrentTimestamp() const;

private:
	timestamp_type timestamp;
};

#endif
