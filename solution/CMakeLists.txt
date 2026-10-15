# GENERATED for relu/relu@1.0.0 — and yours to keep: the harness never
# overwrites this file, so a library your answer depends on stays linked here.
cmake_minimum_required(VERSION 3.16)
project(solution CXX)

# No version is asked for, and that is deliberate. The image installs exactly
# one under /usr/local, and which one is the image's to say: it is the same
# source in every image that makes a measurement of SEAL 4.7.0
# against the release before it a measurement of the release rather than of
# two solutions. SEAL's config file is compatible to the minor version, so a
# number here would refuse every image but one.
find_package(SEAL REQUIRED)

add_executable(solution main.cpp solve.cpp)

# SEAL::seal carries its own include directories, its C++ standard and the
# defines its ABI depends on — whether it was built with Intel HEXL among them
# — so nothing about the library is restated here.
target_link_libraries(solution SEAL::seal)
