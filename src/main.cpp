/**
 * @file main.cpp
 * @brief Program entrypoint (always src/main.cpp in cppboot projects).
 *
 * Keep this file thin: parse args / wire dependencies, then call library code.
 */

#include "mog/version.hpp"

#include <CLI/CLI.hpp>
#include <iostream>
#include <string>

/**
 * @brief Program entry.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status.
 */
int main(int argc, char **argv)
{
    CLI::App app{"mog — cppboot project"};
    app.set_version_flag("-V,--version", std::string{mog::Version()});
    CLI11_PARSE(app, argc, argv);

    std::cout << "mog " << mog::Version()
              << " — add components under src/ (see README.md / AGENTS.md)\n";
    return 0;
}
