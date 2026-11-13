#include "StarSwitch2GameAllocator.hpp"
#include <new>
namespace G=Star::Switch2GameAllocator;
void* operator new(size_t n){return G::cppAllocate(n);}
void* operator new[](size_t n){return G::cppAllocate(n);}
void* operator new(size_t n,std::nothrow_t const&) noexcept{return G::cppAllocateNoThrow(n);}
void* operator new[](size_t n,std::nothrow_t const&) noexcept{return G::cppAllocateNoThrow(n);}
void operator delete(void* p) noexcept{G::release(p);}
void operator delete[](void* p) noexcept{G::release(p);}
void operator delete(void* p,size_t) noexcept{G::release(p);}
void operator delete[](void* p,size_t) noexcept{G::release(p);}
void operator delete(void* p,std::nothrow_t const&) noexcept{G::release(p);}
void operator delete[](void* p,std::nothrow_t const&) noexcept{G::release(p);}
void* operator new(size_t n,std::align_val_t a){return G::cppAllocate(n,size_t(a));}
void* operator new[](size_t n,std::align_val_t a){return G::cppAllocate(n,size_t(a));}
void* operator new(size_t n,std::align_val_t a,std::nothrow_t const&) noexcept{return G::cppAllocateNoThrow(n,size_t(a));}
void* operator new[](size_t n,std::align_val_t a,std::nothrow_t const&) noexcept{return G::cppAllocateNoThrow(n,size_t(a));}
void operator delete(void* p,std::align_val_t a) noexcept{G::release(p,size_t(a));}
void operator delete[](void* p,std::align_val_t a) noexcept{G::release(p,size_t(a));}
void operator delete(void* p,size_t,std::align_val_t a) noexcept{G::release(p,size_t(a));}
void operator delete[](void* p,size_t,std::align_val_t a) noexcept{G::release(p,size_t(a));}
void operator delete(void* p,std::align_val_t a,std::nothrow_t const&) noexcept{G::release(p,size_t(a));}
void operator delete[](void* p,std::align_val_t a,std::nothrow_t const&) noexcept{G::release(p,size_t(a));}
