#include "lamport_timestamp_provider.h"
#include <algorithm>

LamportTimestampProvider::LamportTimestampProvider()
	: timestamp{0}
{
}


LamportTimestampProvider::timestamp_type LamportTimestampProvider::registerEvent()
{
	return registerEvent(getCurrentTimestamp());
}

LamportTimestampProvider::timestamp_type LamportTimestampProvider::registerEvent(timestamp_type eventTimestamp)
{
	return (timestamp = std::max(timestamp, eventTimestamp) + 1);
}


LamportTimestampProvider::timestamp_type LamportTimestampProvider::getCurrentTimestamp() const
{
	return timestamp;
}
