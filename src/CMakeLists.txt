
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

# C subprojects (strict warnings; each sets its own flags)
add_subdirectory("server")
add_subdirectory("kernelmod")
add_subdirectory("logger")
add_subdirectory("dispatcher")

# C++ subprojects (Qt-based; they define their own warning policy)
add_subdirectory("common")
add_subdirectory("extractor")
add_subdirectory("index")