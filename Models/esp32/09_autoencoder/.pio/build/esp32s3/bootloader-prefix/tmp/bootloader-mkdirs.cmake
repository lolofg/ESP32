# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/berat/.platformio/packages/framework-espidf/components/bootloader/subproject")
  file(MAKE_DIRECTORY "/home/berat/.platformio/packages/framework-espidf/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "/home/berat/Documents/Master/Models/esp32/09_autoencoder/.pio/build/esp32s3/bootloader"
  "/home/berat/Documents/Master/Models/esp32/09_autoencoder/.pio/build/esp32s3/bootloader-prefix"
  "/home/berat/Documents/Master/Models/esp32/09_autoencoder/.pio/build/esp32s3/bootloader-prefix/tmp"
  "/home/berat/Documents/Master/Models/esp32/09_autoencoder/.pio/build/esp32s3/bootloader-prefix/src/bootloader-stamp"
  "/home/berat/Documents/Master/Models/esp32/09_autoencoder/.pio/build/esp32s3/bootloader-prefix/src"
  "/home/berat/Documents/Master/Models/esp32/09_autoencoder/.pio/build/esp32s3/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/berat/Documents/Master/Models/esp32/09_autoencoder/.pio/build/esp32s3/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/berat/Documents/Master/Models/esp32/09_autoencoder/.pio/build/esp32s3/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
