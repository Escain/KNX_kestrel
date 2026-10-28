# Author's single-header unit test library (MIT). Vendored: fix upstream, then re-vendor.
add_library(escain_unittests INTERFACE)
add_library(escain::unittests ALIAS escain_unittests)
target_include_directories(escain_unittests SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/escain_unittests)
