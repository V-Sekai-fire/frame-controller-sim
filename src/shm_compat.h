// Cross-platform shared memory for the vpen driver and its feeder. POSIX shm on
// Linux (the Frame); a file-backed named mapping under ProgramData on Windows, so
// vrserver and the feeder (which may be in different sessions) reach one segment.
#ifndef VPEN_SHM_COMPAT_H
#define VPEN_SHM_COMPAT_H
#include <cstddef>
#include "vpen_shm.h"

#ifdef _WIN32
#include <windows.h>

static inline void *vpen_shm_map(size_t size) {
	CreateDirectoryA("C:\\ProgramData\\vpen", nullptr);
	HANDLE file = CreateFileA("C:\\ProgramData\\vpen\\vpen_controllers.bin",
			GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
			OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return nullptr;
	HANDLE map = CreateFileMappingA(file, nullptr, PAGE_READWRITE, 0, (DWORD)size, nullptr);
	void *p = map ? MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, size) : nullptr;
	// file/map handles are kept for the process lifetime (the mapping outlives them
	// only while a view is open, which it is); closing the view unmaps in unmap.
	return p;
}

static inline void vpen_shm_unmap(void *p, size_t) {
	if (p)
		UnmapViewOfFile(p);
}

#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static inline void *vpen_shm_map(size_t size) {
	int fd = shm_open(VPEN_SHM_NAME, O_RDWR | O_CREAT, 0600);
	if (fd < 0)
		return nullptr;
	void *p = nullptr;
	if (ftruncate(fd, size) == 0) {
		p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (p == MAP_FAILED)
			p = nullptr;
	}
	close(fd);
	return p;
}

static inline void vpen_shm_unmap(void *p, size_t size) {
	if (p)
		munmap(p, size);
}
#endif

#endif
