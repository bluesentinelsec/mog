/**
 * @file main.cpp
 * @brief Process entrypoint — thin shell over the library CLI use-case.
 *
 * No domain logic lives here. Front-end concerns (argc/argv, process exit) only.
 */

#include "mog/cli.hpp"

int main(int argc, char **argv)
{
    return mog::cli::RunArgv(argc, argv);
}
