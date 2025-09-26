#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include "gdx.hpp"

namespace
{
constexpr const char *kMemfsPath = "/gdx_current.gdx";

struct Cursor
{
   int SymbolNr { 0 };
   int RecordCount { 0 };
   int NextRecord { 0 };
   int Dimension { 0 };
   int LastDimChanged { 0 };
   bool MappedMode { true };
   bool Finished { false };
   std::vector<int> Keys {};
   std::array<double, GMS_VAL_SCALE + 1> Values {};
};

struct Context
{
   std::unique_ptr<gdx::TGXFileObj> GdxInstance {};
   std::string ConstructorMsg {};
   std::string LastError {};
   std::string CurrentPath { kMemfsPath };
   bool FileOpen { false };
   std::vector<std::unique_ptr<Cursor>> Cursors {};
};

Context &context()
{
   static Context ctx;
   return ctx;
}

void clear_last_error( Context &ctx )
{
   ctx.LastError.clear();
}

void set_last_error( Context &ctx, std::string message )
{
   ctx.LastError = std::move( message );
}

void capture_gdx_error( Context &ctx, const char *fallback )
{
   if( !ctx.GdxInstance )
   {
      set_last_error( ctx, fallback ? std::string( fallback ) : std::string( "GDX instance unavailable" ) );
      return;
   }

   int errNr = ctx.GdxInstance->gdxGetLastError();
   if( errNr == 0 )
   {
      set_last_error( ctx, fallback ? std::string( fallback ) : std::string( "Unknown GDX error" ) );
      return;
   }

   char buffer[GMS_SSSIZE] {};
   if( ctx.GdxInstance->gdxErrorStr( errNr, buffer ) )
      set_last_error( ctx, buffer );
   else
      set_last_error( ctx, fallback ? std::string( fallback ) : std::string( "Unknown GDX error" ) );
}

void release_cursor( Context &ctx, std::uint32_t id )
{
   if( id >= ctx.Cursors.size() ) return;
   auto &slot = ctx.Cursors[id];
   if( !slot ) return;
   if( ctx.GdxInstance && !slot->Finished )
   {
      ctx.GdxInstance->gdxDataReadDone();
      slot->Finished = true;
   }
   slot.reset();
}

void release_all_cursors( Context &ctx )
{
   if( !ctx.Cursors.empty() )
   {
      for( std::uint32_t idx = 0; idx < ctx.Cursors.size(); ++idx )
         release_cursor( ctx, idx );
      ctx.Cursors.clear();
   }
}

Cursor *get_cursor( Context &ctx, std::uint32_t id )
{
   if( id >= ctx.Cursors.size() ) return nullptr;
   return ctx.Cursors[id].get();
}

int store_cursor( Context &ctx, std::unique_ptr<Cursor> cursor )
{
   for( std::size_t i = 0; i < ctx.Cursors.size(); ++i )
      if( !ctx.Cursors[i] )
      {
         ctx.Cursors[i] = std::move( cursor );
         return static_cast<int>( i );
      }

   ctx.Cursors.push_back( std::move( cursor ) );
   return static_cast<int>( ctx.Cursors.size() - 1 );
}

bool ensure_gdx( Context &ctx )
{
   if( ctx.GdxInstance ) return true;
   ctx.ConstructorMsg.clear();
   auto instance = std::make_unique<gdx::TGXFileObj>( ctx.ConstructorMsg );
   if( !ctx.ConstructorMsg.empty() )
   {
      set_last_error( ctx, ctx.ConstructorMsg );
      return false;
   }
   ctx.GdxInstance = std::move( instance );
   return true;
}

}

extern "C"
{

struct GdxWasmSymbolInfo
{
   char name[64];
   char explanation[256];
   std::int32_t type { 0 };
   std::int32_t dimension { 0 };
   std::int32_t recordCount { 0 };
   std::int32_t userInfo { 0 };
};

struct GdxWasmRecordView
{
   std::int32_t keyOffset { 0 };
   std::int32_t valueOffset { 0 };
   std::int32_t dimension { 0 };
   std::int32_t dimChanged { 0 };
};

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int gdx_wasm_init()
{
   auto &ctx = context();
   clear_last_error( ctx );

   release_all_cursors( ctx );
   if( ctx.GdxInstance && ctx.FileOpen )
   {
      ctx.GdxInstance->gdxClose();
      ctx.FileOpen = false;
   }
   ctx.GdxInstance.reset();

   if( !ensure_gdx( ctx ) ) return -1;

   clear_last_error( ctx );
   return 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void gdx_wasm_shutdown()
{
   auto &ctx = context();
   release_all_cursors( ctx );
   if( ctx.GdxInstance && ctx.FileOpen )
   {
      ctx.GdxInstance->gdxClose();
      ctx.FileOpen = false;
   }
   ctx.GdxInstance.reset();
   clear_last_error( ctx );
   std::remove( ctx.CurrentPath.c_str() );
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int gdx_wasm_open_buffer( std::uint8_t *buffer, std::uint32_t length )
{
   auto &ctx = context();
   clear_last_error( ctx );

   if( !ensure_gdx( ctx ) ) return -1;

   release_all_cursors( ctx );

   if( ctx.FileOpen )
   {
      ctx.GdxInstance->gdxClose();
      ctx.FileOpen = false;
   }

   if( buffer == nullptr || length == 0 )
   {
      set_last_error( ctx, "GDX buffer is empty" );
      return -1;
   }

   {
      std::ofstream out( ctx.CurrentPath, std::ios::binary | std::ios::trunc );
      if( !out )
      {
         set_last_error( ctx, "Unable to create temporary GDX file" );
         return -1;
      }

      out.write( reinterpret_cast<const char *>( buffer ), static_cast<std::streamsize>( length ) );
      if( !out )
      {
         set_last_error( ctx, "Failed to write GDX data" );
         return -1;
      }
   }

   int errNr { 0 };
   if( !ctx.GdxInstance->gdxOpenRead( ctx.CurrentPath.c_str(), errNr ) )
   {
      if( errNr != 0 )
      {
         char buffer[GMS_SSSIZE] {};
         ctx.GdxInstance->gdxErrorStr( errNr, buffer );
         set_last_error( ctx, buffer );
      }
      else
      {
         capture_gdx_error( ctx, "Failed to open GDX file" );
      }
      return -1;
   }

   ctx.FileOpen = true;
   clear_last_error( ctx );
   return 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
std::int32_t gdx_wasm_symbol_count()
{
   auto &ctx = context();
   if( !ctx.GdxInstance || !ctx.FileOpen )
   {
      set_last_error( ctx, "No GDX file loaded" );
      return -1;
   }

   int symbolCount { 0 }, uelCount { 0 };
   ctx.GdxInstance->gdxSystemInfo( symbolCount, uelCount );
   clear_last_error( ctx );
   return static_cast<std::int32_t>( symbolCount );
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int gdx_wasm_symbol_info( std::uint32_t index, GdxWasmSymbolInfo *outInfo )
{
   auto &ctx = context();
   if( !outInfo )
   {
      set_last_error( ctx, "Output buffer is null" );
      return 0;
   }

   std::memset( outInfo, 0, sizeof( *outInfo ) );

   if( !ctx.GdxInstance || !ctx.FileOpen )
   {
      set_last_error( ctx, "No GDX file loaded" );
      return 0;
   }

   int syNr = static_cast<int>( index ) + 1;

   char nameBuf[64] {};
   int dim { 0 }, type { 0 };
   if( !ctx.GdxInstance->gdxSymbolInfo( syNr, nameBuf, dim, type ) )
   {
      capture_gdx_error( ctx, "Failed to retrieve symbol info" );
      return 0;
   }

   int recCnt { 0 }, userInfo { 0 };
   char explBuf[256] {};
   ctx.GdxInstance->gdxSymbolInfoX( syNr, recCnt, userInfo, explBuf );

   std::strncpy( outInfo->name, nameBuf, sizeof( outInfo->name ) - 1 );
   std::strncpy( outInfo->explanation, explBuf, sizeof( outInfo->explanation ) - 1 );
   outInfo->type = static_cast<std::int32_t>( type );
   outInfo->dimension = static_cast<std::int32_t>( dim );
   outInfo->recordCount = static_cast<std::int32_t>( recCnt );
   outInfo->userInfo = static_cast<std::int32_t>( userInfo );

   clear_last_error( ctx );
   return 1;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
std::int32_t gdx_wasm_start_symbol( std::uint32_t index, std::uint32_t mode )
{
   auto &ctx = context();
   clear_last_error( ctx );

   if( !ctx.GdxInstance || !ctx.FileOpen )
   {
      set_last_error( ctx, "No GDX file loaded" );
      return -1;
   }

   if( mode != 0 )
   {
      set_last_error( ctx, "Only mapped read mode (0) is supported" );
      return -1;
   }

   int syNr = static_cast<int>( index ) + 1;
   int dim = ctx.GdxInstance->gdxSymbolDim( syNr );
   if( dim < 0 )
   {
      capture_gdx_error( ctx, "Invalid symbol index" );
      return -1;
   }

   int nrRecs { 0 };
   if( !ctx.GdxInstance->gdxDataReadRawStart( syNr, nrRecs ) )
   {
      capture_gdx_error( ctx, "Unable to start reading symbol" );
      return -1;
   }

   auto cursor = std::make_unique<Cursor>();
   cursor->SymbolNr = syNr;
   cursor->Dimension = dim;
   cursor->RecordCount = nrRecs;
   cursor->MappedMode = false;
   cursor->Keys.assign( std::max( 1, dim ), 0 );
   cursor->Values.fill( 0.0 );

   if( nrRecs == 0 )
   {
      ctx.GdxInstance->gdxDataReadDone();
      cursor->Finished = true;
   }

   int handle = store_cursor( ctx, std::move( cursor ) );

   clear_last_error( ctx );
   return static_cast<std::int32_t>( handle );
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int gdx_wasm_next_record( std::uint32_t cursorId, GdxWasmRecordView *recordView )
{
   auto &ctx = context();
   if( recordView ) std::memset( recordView, 0, sizeof( *recordView ) );

   if( !ctx.GdxInstance || !ctx.FileOpen )
   {
      set_last_error( ctx, "No GDX file loaded" );
      return -1;
   }

   Cursor *cursor = get_cursor( ctx, cursorId );
   if( !cursor )
   {
      set_last_error( ctx, "Invalid cursor handle" );
      return -1;
   }

   if( cursor->Finished || cursor->NextRecord >= cursor->RecordCount )
   {
      if( ctx.GdxInstance && !cursor->Finished )
      {
         ctx.GdxInstance->gdxDataReadDone();
         cursor->Finished = true;
      }
      release_cursor( ctx, cursorId );
      clear_last_error( ctx );
      return 0;
   }

   int dimFirst { 0 };
   if( cursor->MappedMode )
   {
      if( !ctx.GdxInstance->gdxDataReadMap( cursor->NextRecord + 1, cursor->Keys.data(), cursor->Values.data(), dimFirst ) )
      {
         capture_gdx_error( ctx, "Failed to read record" );
         release_cursor( ctx, cursorId );
         return -1;
      }
   }
   else
   {
      if( !ctx.GdxInstance->gdxDataReadRaw( cursor->Keys.data(), cursor->Values.data(), dimFirst ) )
      {
         capture_gdx_error( ctx, "Failed to read record" );
         release_cursor( ctx, cursorId );
         return -1;
      }
   }
   cursor->NextRecord++;
   cursor->LastDimChanged = dimFirst;

   if( cursor->NextRecord >= cursor->RecordCount )
   {
      if( ctx.GdxInstance )
      {
         ctx.GdxInstance->gdxDataReadDone();
         cursor->Finished = true;
      }
   }

   if( recordView )
   {
      recordView->keyOffset = static_cast<std::int32_t>( reinterpret_cast<std::uintptr_t>( cursor->Keys.data() ) );
      recordView->valueOffset = static_cast<std::int32_t>( reinterpret_cast<std::uintptr_t>( cursor->Values.data() ) );
      recordView->dimension = cursor->Dimension;
      recordView->dimChanged = cursor->LastDimChanged;
   }

   clear_last_error( ctx );
   return 1;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void gdx_wasm_cursor_close( std::uint32_t cursorId )
{
   auto &ctx = context();
   release_cursor( ctx, cursorId );
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int gdx_wasm_get_uel( std::uint32_t index, char *dest, std::uint32_t destLen )
{
   auto &ctx = context();
   if( !dest || destLen == 0 )
   {
      set_last_error( ctx, "Destination buffer is invalid" );
      return 0;
   }

   if( !ctx.GdxInstance || !ctx.FileOpen )
   {
      set_last_error( ctx, "No GDX file loaded" );
      return 0;
   }

   char buffer[GMS_SSSIZE] {};
   if( !ctx.GdxInstance->gdxGetUEL( static_cast<int>( index ) + 1, buffer ) )
   {
      capture_gdx_error( ctx, "Failed to fetch UEL" );
      return 0;
   }

   std::strncpy( dest, buffer, destLen - 1 );
   dest[destLen - 1] = '\0';
   clear_last_error( ctx );
   return 1;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
const char *gdx_wasm_last_error()
{
   auto &ctx = context();
   return ctx.LastError.empty() ? "" : ctx.LastError.c_str();
}
}

int main()
{
   return 0;
}
