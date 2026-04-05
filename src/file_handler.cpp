/**
 * @file file_handler.cpp
 * @brief File handling functions.
 */

// standard includes
#include <filesystem>
#include <fstream>

// local includes
#include "file_handler.h"
#include "logging.h"

namespace file_handler {

/**
 * @brief Read a file to string.
 * @param path The path of the file.
 * @return `std::string` : The contents of the file.
 *
 * EXAMPLES:
 * ```cpp
 * std::string contents = read_file("path/to/file");
 * ```
 */
std::string read_file(const char *path) {
  if (!std::filesystem::exists(path)) {
    BOOST_LOG(debug) << "Missing file: " << path;
    return {};
  }

  std::ifstream in(path);

  std::string input;
  std::string base64_cert;

  while (!in.eof()) {
    std::getline(in, input);
    base64_cert += input + '\n';
  }

  return base64_cert;
}

} // namespace file_handler
