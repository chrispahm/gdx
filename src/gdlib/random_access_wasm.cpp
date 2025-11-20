#include "random_access_wasm.hpp"

#if defined(__EMSCRIPTEN__)

#include <memory>
#include <mutex>
#include <utility>

namespace gdlib::random_access
{

namespace
{
class WasmRandomAccessProvider;

std::mutex &CallbackMutex()
{
   static std::mutex g_mutex;
   return g_mutex;
}

WasmRangeFetchCallbacks &GlobalCallbacks()
{
   static WasmRangeFetchCallbacks g_callbacks;
   return g_callbacks;
}

bool &GlobalCallbacksSet()
{
   static bool g_set = false;
   return g_set;
}

WasmRandomAccessProvider *&ActivePrefetchProvider()
{
   static WasmRandomAccessProvider *g_active = nullptr;
   return g_active;
}

class WasmRandomAccessProvider final : public gmsstrm::RandomAccessProvider
{
public:
   WasmRandomAccessProvider( std::string resourceId, WasmRangeFetchCallbacks callbacks )
       : ResourceId { std::move( resourceId ) }
       , Callbacks { std::move( callbacks ) }
   {}

   ~WasmRandomAccessProvider() override
   {
      Close();
      std::lock_guard<std::mutex> lock( CallbackMutex() );
      if( ActivePrefetchProvider() == this )
         ActivePrefetchProvider() = nullptr;
   }

   bool ReadAt( uint64_t offset, void *dst, size_t requested, size_t &out_read ) override
   {
      out_read = 0;
      if( !Callbacks.fetch ) return false;
      return Callbacks.fetch( Callbacks.userData, offset, dst, requested, out_read );
   }

   bool GetSize( uint64_t &out_size ) override
   {
      if( !Callbacks.getSize ) return false;
      return Callbacks.getSize( Callbacks.userData, out_size );
   }

   void Close() noexcept override
   {
      if( Callbacks.close )
      {
         Callbacks.close( Callbacks.userData );
         Callbacks.close = nullptr;
      }
   }

   void Prefetch( uint64_t offset, size_t length )
   {
      if( Callbacks.prefetch )
         Callbacks.prefetch( Callbacks.userData, offset, length );
   }

   [[nodiscard]] const std::string &Id() const noexcept { return ResourceId; }

private:
   std::string ResourceId;
   WasmRangeFetchCallbacks Callbacks;
};

} // namespace

void SetGlobalWasmRangeFetcher( WasmRangeFetchCallbacks callbacks )
{
   std::lock_guard<std::mutex> lock( CallbackMutex() );
   GlobalCallbacks() = std::move( callbacks );
   GlobalCallbacksSet() = GlobalCallbacks().fetch && GlobalCallbacks().getSize;
}

void ClearGlobalWasmRangeFetcher()
{
   std::lock_guard<std::mutex> lock( CallbackMutex() );
   GlobalCallbacks() = WasmRangeFetchCallbacks {};
   GlobalCallbacksSet() = false;
}

bool HasGlobalWasmRangeFetcher()
{
   std::lock_guard<std::mutex> lock( CallbackMutex() );
   return GlobalCallbacksSet();
}

std::unique_ptr<gmsstrm::RandomAccessProvider>
CreateWasmRandomAccessProvider( std::string resourceId, const WasmRangeFetchCallbacks *callbacks )
{
   WasmRangeFetchCallbacks activeCallbacks;
   {
      std::lock_guard<std::mutex> lock( CallbackMutex() );
      if( callbacks )
         activeCallbacks = *callbacks;
      else
         activeCallbacks = GlobalCallbacks();
   }

   if( !activeCallbacks.fetch || !activeCallbacks.getSize )
      return nullptr;

   auto provider = std::make_unique<WasmRandomAccessProvider>( std::move( resourceId ), std::move( activeCallbacks ) );
   {
      std::lock_guard<std::mutex> lock( CallbackMutex() );
      ActivePrefetchProvider() = provider.get();
   }
   return provider;
}

void PrefetchWasmRange( uint64_t offset, size_t length )
{
   std::lock_guard<std::mutex> lock( CallbackMutex() );
   if( auto *provider = ActivePrefetchProvider() )
   {
      provider->Prefetch( offset, length );
      return;
   }
   auto &callbacks = GlobalCallbacks();
   if( callbacks.prefetch )
      callbacks.prefetch( callbacks.userData, offset, length );
}

} // namespace gdlib::random_access

#endif // defined(__EMSCRIPTEN__)
