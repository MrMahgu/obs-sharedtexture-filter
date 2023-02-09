#pragma once

namespace SharedMemory {

// SharedMemory stuff

enum CreateStatus {
	SM_VALID,
	SM_ALREADY_EXISTS,
	SM_FAILED,
};

static CreateStatus create(void *data);
static bool map(void *data);
static void close(void *data);
static void unmap(void *data);

} // namespace SharedMemory
