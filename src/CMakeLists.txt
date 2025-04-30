
if(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE "Release" CACHE STRING
    "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
    FORCE)
endif(NOT CMAKE_BUILD_TYPE)

message("Build type:" ${CMAKE_BUILD_TYPE})

# AddressSanitizer only:
# add_compile_options(-fsanitize=address -g)
# add_link_options(-fsanitize=address)
# Or ThreadSanitizer only:
# add_compile_options(-fsanitize=thread -g)
# add_link_options(-fsanitize=thread)

add_subdirectory("server")
add_subdirectory("kernelmod")
add_subdirectory("searcher")