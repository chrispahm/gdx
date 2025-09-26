/*
 * GAMS - General Algebraic Modeling System GDX API
 *
 * Copyright (c) 2017-2025 GAMS Software GmbH
 *
 * Licensed under the MIT License. See LICENSE in the project root for license information.
 */

#include "doctest.hpp"

#include "../gdx.hpp"
#include "gdxtests.hpp"
#include "strindexbuf.hpp"

#include "gclgms.h"
#include "gdx_random_access.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <map>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>
#include <string>
#include <vector>

using namespace std::literals::string_literals;
using gdlib::strindexbuf::StrIndexBuffers;

namespace gdx::tests::randomaccess
{
TEST_SUITE_BEGIN( "Random access providers" );

namespace
{
struct MemoryRandomAccessContext
{
   const std::vector<uint8_t> *buffer {};
   size_t maxChunk { 0 };
   uint64_t reportedSize { 0 };
   bool enforceReportedSize { false };
};

int MemoryReadAt( void *userData, uint64_t offset, void *dst, size_t requested, size_t *out_read )
{
   auto *ctx = static_cast<MemoryRandomAccessContext *>( userData );
   if( !ctx || !ctx->buffer || !out_read ) return 0;
   const auto &blob = *ctx->buffer;
   *out_read = 0;

   if( requested == 0 ) return 1;

   const uint64_t blobSize = static_cast<uint64_t>( blob.size() );
   if( offset >= blobSize )
   {
      if( ctx->enforceReportedSize ) return 0;
      return 1;
   }

   if( ctx->enforceReportedSize && offset + static_cast<uint64_t>( requested ) > ctx->reportedSize )
      return 0;

   size_t available = static_cast<size_t>( blobSize - offset );
   if( ctx->enforceReportedSize )
      available = std::min( available, static_cast<size_t>( ctx->reportedSize - offset ) );

   size_t chunk = requested;
   if( ctx->maxChunk > 0 ) chunk = std::min( chunk, ctx->maxChunk );
   const size_t toCopy = std::min( available, chunk );
   if( toCopy == 0 )
   {
      if( ctx->enforceReportedSize ) return 0;
      return 1;
   }

   std::memcpy( dst, blob.data() + offset, toCopy );
   *out_read = toCopy;
   return 1;
}

int MemoryGetSize( void *userData, uint64_t *out_size )
{
   auto *ctx = static_cast<MemoryRandomAccessContext *>( userData );
   if( !ctx || !ctx->buffer || !out_size ) return 0;
   if( ctx->reportedSize == 0 ) ctx->reportedSize = static_cast<uint64_t>( ctx->buffer->size() );
   *out_size = ctx->reportedSize;
   return 1;
}

void MemoryClose( void * ) {}

struct SampleFixture
{
   std::filesystem::path path;
   std::vector<uint8_t> blob;
};

std::filesystem::path MakeTempPath()
{
   static int counter {};
   auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
   const std::string filename = "gdx-random-access-"s + std::to_string( stamp ) + "-"s + std::to_string( counter++ ) + ".gdx"s;
   return std::filesystem::temp_directory_path() / filename;
}

SampleFixture CreateSampleFixture()
{
   SampleFixture fixture;
   fixture.path = MakeTempPath();

   gdx::tests::gdxtests::testWrite( fixture.path.string(), []( TGXFileObj &pgx ) {
      std::array<double, GMS_VAL_MAX> values {};
      std::array<const char *, 1> domainIds {};
      std::array<std::string, 1> keyStorage {};

      REQUIRE( pgx.gdxDataWriteStrStart( "cities", "two element set", 1, dt_set, 0 ) );
      keyStorage[0] = "alpha"s;
      const char *setKey { keyStorage[0].c_str() };
      REQUIRE( pgx.gdxDataWriteStr( &setKey, values.data() ) );
      keyStorage[0] = "beta"s;
      setKey = keyStorage[0].c_str();
      REQUIRE( pgx.gdxDataWriteStr( &setKey, values.data() ) );
      REQUIRE( pgx.gdxDataWriteDone() );

      REQUIRE( pgx.gdxDataWriteStrStart( "ship_cost", "sample parameter", 1, dt_par, 0 ) );
      domainIds[0] = "cities";
      REQUIRE( pgx.gdxSymbolSetDomain( domainIds.data() ) );

      keyStorage[0] = "alpha"s;
      const char *paramKey { keyStorage[0].c_str() };
      values[GMS_VAL_LEVEL] = 1.5;
      REQUIRE( pgx.gdxDataWriteStr( &paramKey, values.data() ) );

      keyStorage[0] = "beta"s;
      paramKey = keyStorage[0].c_str();
      values[GMS_VAL_LEVEL] = 2.5;
      REQUIRE( pgx.gdxDataWriteStr( &paramKey, values.data() ) );
      REQUIRE( pgx.gdxDataWriteDone() );

      int rawKey = 0;
      REQUIRE( pgx.gdxDataWriteRawStart( "scalar", "baseline", 0, dt_par, 0 ) );
      values[GMS_VAL_LEVEL] = 42.0;
      REQUIRE( pgx.gdxDataWriteRaw( &rawKey, values.data() ) );
      REQUIRE( pgx.gdxDataWriteDone() );
   } );

   std::ifstream in( fixture.path, std::ios::binary );
   REQUIRE( in.good() );
   fixture.blob.assign( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
   return fixture;
}

void RemoveIfExists( const std::filesystem::path &p )
{
   std::error_code ec;
   std::filesystem::remove( p, ec );
}

gdx_random_access MakeRandomAccess( MemoryRandomAccessContext &ctx )
{
   gdx_random_access ra {};
   ra.user_data = &ctx;
   ra.read_at = &MemoryReadAt;
   ra.get_size = &MemoryGetSize;
   ra.close = &MemoryClose;
   return ra;
}

} // namespace

TEST_CASE( "Random-access provider can read an in-memory GDX blob" )
{
   auto fixture = CreateSampleFixture();
   MemoryRandomAccessContext ctx { &fixture.blob };
   ctx.reportedSize = static_cast<uint64_t>( fixture.blob.size() );
   auto ra = MakeRandomAccess( ctx );

   std::string errMsg;
   TGXFileObj pgx { errMsg };
   int errNr {};
   REQUIRE( pgx.gdxOpenReadFromRandomAccess( &ra, errNr ) );
   REQUIRE_EQ( 0, errNr );

   int numSymbols {}, numUels {};
   REQUIRE( pgx.gdxSystemInfo( numSymbols, numUels ) );
   CHECK_EQ( 3, numSymbols );
   CHECK_EQ( 2, numUels );

   int symNr {};
   REQUIRE( pgx.gdxFindSymbol( "ship_cost", symNr ) );
   std::array<double, GMS_VAL_MAX> values {};
   StrIndexBuffers keys;
   int numRecords {};
   REQUIRE( pgx.gdxDataReadStrStart( symNr, numRecords ) );
   CHECK_EQ( 2, numRecords );
   std::map<std::string, double> seen;
   for( int rec {}; rec < numRecords; ++rec )
   {
      int dimFirst {};
      REQUIRE( pgx.gdxDataReadStr( keys.ptrs(), values.data(), dimFirst ) );
      CHECK_EQ( 1, dimFirst );
      seen.emplace( keys[0].str(), values[GMS_VAL_LEVEL] );
   }
   REQUIRE( pgx.gdxDataReadDone() );
   CHECK_EQ( 1.5, doctest::Approx( seen.at( "alpha" ) ) );
   CHECK_EQ( 2.5, doctest::Approx( seen.at( "beta" ) ) );

   REQUIRE_EQ( 0, pgx.gdxClose() );
   RemoveIfExists( fixture.path );
}

TEST_CASE( "Native and random-access openings yield identical symbol counts" )
{
   auto fixture = CreateSampleFixture();

   std::string errMsgNative;
   TGXFileObj native { errMsgNative };
   int errNrNative {};
   REQUIRE( native.gdxOpenRead( fixture.path.string().c_str(), errNrNative ) );
   REQUIRE_EQ( 0, errNrNative );

   int nativeSymbols {}, nativeUels {};
   REQUIRE( native.gdxSystemInfo( nativeSymbols, nativeUels ) );

   MemoryRandomAccessContext ctx { &fixture.blob };
   ctx.reportedSize = static_cast<uint64_t>( fixture.blob.size() );
   auto ra = MakeRandomAccess( ctx );

   std::string errMsgRA;
   TGXFileObj randomAccess { errMsgRA };
   int errNr {};
   REQUIRE( randomAccess.gdxOpenReadFromRandomAccess( &ra, errNr ) );
   REQUIRE_EQ( 0, errNr );

   int raSymbols {}, raUels {};
   REQUIRE( randomAccess.gdxSystemInfo( raSymbols, raUels ) );
   CHECK_EQ( nativeSymbols, raSymbols );
   CHECK_EQ( nativeUels, raUels );

   REQUIRE_EQ( 0, native.gdxClose() );
   REQUIRE_EQ( 0, randomAccess.gdxClose() );
   RemoveIfExists( fixture.path );
}

TEST_CASE( "Random-access provider handles partial reads" )
{
   auto fixture = CreateSampleFixture();
   MemoryRandomAccessContext ctx { &fixture.blob };
   ctx.reportedSize = static_cast<uint64_t>( fixture.blob.size() );
   ctx.maxChunk = 1; // force byte-by-byte delivery
   auto ra = MakeRandomAccess( ctx );

   std::string errMsg;
   TGXFileObj pgx { errMsg };
   int errNr {};
   REQUIRE( pgx.gdxOpenReadFromRandomAccess( &ra, errNr ) );
   REQUIRE_EQ( 0, errNr );

   int numSymbols {}, numUels {};
   REQUIRE( pgx.gdxSystemInfo( numSymbols, numUels ) );
   CHECK_EQ( 3, numSymbols );
   CHECK_EQ( 2, numUels );

   REQUIRE_EQ( 0, pgx.gdxClose() );
   RemoveIfExists( fixture.path );
}

TEST_CASE( "Random-access provider reports size mismatches as an error" )
{
   auto fixture = CreateSampleFixture();
   MemoryRandomAccessContext ctx { &fixture.blob };
   ctx.reportedSize = static_cast<uint64_t>( fixture.blob.size() - 8 );
   ctx.enforceReportedSize = true;
   auto ra = MakeRandomAccess( ctx );

   std::string errMsg;
   TGXFileObj pgx { errMsg };
   int errNr {};
   CHECK_FALSE( pgx.gdxOpenReadFromRandomAccess( &ra, errNr ) );
   CHECK_NE( 0, errNr );
   CHECK_EQ( errNr, pgx.gdxGetLastError() );

   RemoveIfExists( fixture.path );
}

TEST_SUITE_END();

} // namespace gdx::tests::randomaccess
