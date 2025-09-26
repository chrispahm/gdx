#pragma once

#include <memory>
#include <string>

#include "gmsstrm.hpp"

namespace gdlib::random_access
{

/**
 * Open a native filesystem-backed random-access provider for the given path.
 * @param path UTF-8 encoded path to the file to open.
 * @param mode Desired access mode (read-only or read/write).
 * @param osError Receives the platform-specific error code when the call fails.
 * @return A provider instance on success, or nullptr on failure.
 */
std::unique_ptr<gmsstrm::RandomAccessProvider>
CreateNativeRandomAccessProvider( const std::string &path, gmsstrm::FileAccessMode mode, int &osError );

}
