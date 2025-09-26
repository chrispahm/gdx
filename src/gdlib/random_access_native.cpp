#include "random_access_native.hpp"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <utility>

#ifdef _WIN32
#   define NOMINMAX
#   include <windows.h>
#else
#   include <fcntl.h>
#   include <sys/stat.h>
#   include <sys/types.h>
#   include <unistd.h>
#endif

namespace gdlib::random_access
{

namespace
{

class NativeRandomAccessProvider final : public gmsstrm::RandomAccessProvider
{
public:
   explicit NativeRandomAccessProvider( std::string path )
       : FilePath { std::move( path ) }
   {}

   ~NativeRandomAccessProvider() override
   {
      Close();
   }

   bool ReadAt( uint64_t offset, void *dst, size_t requested, size_t &out_read ) override
   {
      out_read = 0;
      if( requested == 0 )
      {
         LastError = 0;
         return true;
      }
#ifdef _WIN32
      if( Handle == INVALID_HANDLE_VALUE ) return false;
      const size_t chunk = std::min( requested, static_cast<size_t>( std::numeric_limits<DWORD>::max() ) );
      OVERLAPPED ov {};
      ov.Offset = static_cast<DWORD>( offset & 0xFFFFFFFFu );
      ov.OffsetHigh = static_cast<DWORD>( ( offset >> 32u ) & 0xFFFFFFFFu );
      DWORD bytesRead = 0;
      if( !ReadFile( Handle, dst, static_cast<DWORD>( chunk ), &bytesRead, &ov ) )
      {
         LastError = static_cast<int>( GetLastError() );
         return false;
      }
      LastError = 0;
      out_read = static_cast<size_t>( bytesRead );
      return true;
#else
      if( FD < 0 ) return false;
      const size_t chunk = std::min( requested, static_cast<size_t>( std::numeric_limits<ssize_t>::max() ) );
      const auto res = ::pread( FD, dst, chunk, static_cast<off_t>( offset ) );
      if( res < 0 )
      {
         LastError = errno;
         return false;
      }
      LastError = 0;
      out_read = static_cast<size_t>( res );
      return true;
#endif
   }

   bool GetSize( uint64_t &out_size ) override
   {
#ifdef _WIN32
      if( Handle == INVALID_HANDLE_VALUE ) return false;
      LARGE_INTEGER size {};
      if( !GetFileSizeEx( Handle, &size ) )
      {
         LastError = static_cast<int>( GetLastError() );
         return false;
      }
      LastError = 0;
      out_size = static_cast<uint64_t>( size.QuadPart );
      return true;
#else
      if( FD < 0 ) return false;
      struct stat st
      {
      };
      if( ::fstat( FD, &st ) != 0 )
      {
         LastError = errno;
         return false;
      }
      LastError = 0;
      out_size = static_cast<uint64_t>( st.st_size );
      return true;
#endif
   }

   void Close() noexcept override
   {
#ifdef _WIN32
      if( Handle != INVALID_HANDLE_VALUE )
      {
         ::CloseHandle( Handle );
         Handle = INVALID_HANDLE_VALUE;
      }
#else
      if( FD >= 0 )
      {
         ::close( FD );
         FD = -1;
      }
#endif
   }

   [[nodiscard]] const std::string &Path() const noexcept
   {
      return FilePath;
   }

   void SetHandle( int fd )
   {
#ifndef _WIN32
      FD = fd;
#else
      (void) fd;
#endif
   }

   void SetHandle( void *handle )
   {
#ifdef _WIN32
      Handle = static_cast<HANDLE>( handle );
#else
      (void) handle;
#endif
   }

   [[nodiscard]] int GetLastErrorValue() const noexcept { return LastError; }

private:
   std::string FilePath;
   int LastError {};
#ifdef _WIN32
   HANDLE Handle { INVALID_HANDLE_VALUE };
#else
   int FD { -1 };
#endif
};

#ifdef _WIN32
std::wstring Utf8ToWide( const std::string &input )
{
   if( input.empty() ) return {};
   const int sizeNeeded = MultiByteToWideChar( CP_UTF8, 0, input.c_str(), static_cast<int>( input.size() ), nullptr, 0 );
   if( sizeNeeded <= 0 ) return {};
   std::wstring buffer( sizeNeeded, L'\0' );
   MultiByteToWideChar( CP_UTF8, 0, input.c_str(), static_cast<int>( input.size() ), buffer.data(), sizeNeeded );
   return buffer;
}
#endif

} // namespace

std::unique_ptr<gmsstrm::RandomAccessProvider>
CreateNativeRandomAccessProvider( const std::string &path, gmsstrm::FileAccessMode mode, int &osError )
{
   osError = 0;
   if( mode != gmsstrm::FileAccessMode::fmOpenRead && mode != gmsstrm::FileAccessMode::fmOpenReadWrite )
   {
#ifdef _WIN32
      osError = ERROR_ACCESS_DENIED;
#else
      osError = EINVAL;
#endif
      return nullptr;
   }

   auto provider = std::make_unique<NativeRandomAccessProvider>( path );

#ifdef _WIN32
   const auto desiredAccess = mode == gmsstrm::FileAccessMode::fmOpenRead ? GENERIC_READ : ( GENERIC_READ | GENERIC_WRITE );
   const auto shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
   const auto creationDisposition = OPEN_EXISTING;
   const auto flags = FILE_ATTRIBUTE_NORMAL;
   const std::wstring widePath = Utf8ToWide( path );
   HANDLE handle = INVALID_HANDLE_VALUE;
   if( !widePath.empty() )
      handle = CreateFileW( widePath.c_str(), desiredAccess, shareMode, nullptr, creationDisposition, flags, nullptr );
   if( handle == INVALID_HANDLE_VALUE )
   {
      osError = static_cast<int>( GetLastError() );
      return nullptr;
   }
   provider->SetHandle( handle );
#else
   int flags = mode == gmsstrm::FileAccessMode::fmOpenRead ? O_RDONLY : O_RDWR;
#ifdef O_CLOEXEC
   flags |= O_CLOEXEC;
#endif
   const int fd = ::open( path.c_str(), flags );
   if( fd < 0 )
   {
      osError = errno;
      return nullptr;
   }
   provider->SetHandle( fd );
#endif

   // Probe size once to ensure the handle is valid (and to report errors early)
   uint64_t sizeProbe {};
   if( !provider->GetSize( sizeProbe ) )
   {
      osError = provider->GetLastErrorValue();
      provider->Close();
      return nullptr;
   }

   return provider;
}

} // namespace gdlib::random_access
