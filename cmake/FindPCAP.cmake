# Simple FindPCAP.cmake
find_path(PCAP_INCLUDE_DIR pcap.h)
find_library(PCAP_LIBRARY pcap)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PCAP DEFAULT_MSG PCAP_LIBRARY PCAP_INCLUDE_DIR)

if(PCAP_FOUND)
    set(PCAP_LIBRARIES ${PCAP_LIBRARY})
    set(PCAP_INCLUDE_DIRS ${PCAP_INCLUDE_DIR})
endif()