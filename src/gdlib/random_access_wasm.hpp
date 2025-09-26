#pragma once

#if defined(__EMSCRIPTEN__)

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "gmsstrm.hpp"

namespace gdlib::random_access
{

struct WasmRangeFetchCallbacks
{
   using FetchFn = std::function<bool( void *userData, uint64_t offset, void *dst, size_t requested, size_t &out_read )>;
   using SizeFn = std::function<bool( void *userData, uint64_t &out_size )>;
   using PrefetchFn = std::function<void( void *userData, uint64_t offset, size_t length )>;
   using CloseFn = std::function<void( void *userData )>;

   void *userData { nullptr };
   FetchFn fetch;
   SizeFn getSize;
   PrefetchFn prefetch;
   CloseFn close;
};

void SetGlobalWasmRangeFetcher( WasmRangeFetchCallbacks callbacks );
void ClearGlobalWasmRangeFetcher();
bool HasGlobalWasmRangeFetcher();

std::unique_ptr<gmsstrm::RandomAccessProvider>
CreateWasmRandomAccessProvider( std::string resourceId, const WasmRangeFetchCallbacks *callbacks = nullptr );

void PrefetchWasmRange( uint64_t offset, size_t length );

} // namespace gdlib::random_access

#endif // defined(__EMSCRIPTEN__)
