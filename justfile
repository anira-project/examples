set shell := ["bash", "-cu"]

preset := "debug"

default:
    @just --list

# Fetch the models submodule
setup:
    git submodule update --init

# Configure + build every example
build:
    cmake --preset {{preset}} > /dev/null
    cmake --build --preset {{preset}}

# clang-format in place (the tracked sources only)
format:
    git ls-files '*.cpp' '*.h' | xargs clang-format -i

# clang-format check
format-check:
    git ls-files '*.cpp' '*.h' | xargs clang-format --dry-run --Werror

# Reinstall the tanh-tooling copies (.clang-*, cmake/tanh/) at the pinned version
tooling-install VERSION="v0.3.0":
    curl -fsSL https://raw.githubusercontent.com/tanh-lab/tanh-tooling/{{VERSION}}/install.sh | sh -s -- clang cmake

# Remove every build directory
clean:
    rm -rf build
