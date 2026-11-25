# The portable core: scheduling, scenes, the event contract and the LED look.
# It includes no ESP-IDF headers (firmware/host builds and tests it on a PC);
# cJSON is its only dependency, declared in idf_component.yml.
idf_component_register(SRC_DIRS "src"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src")
