// Deliberately compiled into a separate shared library, without malloc wrappers.
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <string>

extern "C" void* boundarySdkAllocate() { return std::malloc(256); }
extern "C" void boundarySdkFree(void* p) { std::free(p); }
extern "C" char* boundaryCppAllocate() { return new char[256]; }
extern "C" void boundaryCppFree(char* p) { delete[] p; }
extern "C" void boundaryThrow() { throw std::runtime_error(std::string(256, 'b')); }
