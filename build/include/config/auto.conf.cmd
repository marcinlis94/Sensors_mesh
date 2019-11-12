deps_config := \
	/home/Lisek/esp/esp-idf/components/app_trace/Kconfig \
	/home/Lisek/esp/esp-idf/components/aws_iot/Kconfig \
	/home/Lisek/esp/esp-idf/components/bt/Kconfig \
	/home/Lisek/esp/esp-idf/components/driver/Kconfig \
	/home/Lisek/esp/esp-idf/components/esp32/Kconfig \
	/home/Lisek/esp/esp-idf/components/esp_adc_cal/Kconfig \
	/home/Lisek/esp/esp-idf/components/esp_event/Kconfig \
	/home/Lisek/esp/esp-idf/components/esp_http_client/Kconfig \
	/home/Lisek/esp/esp-idf/components/esp_http_server/Kconfig \
	/home/Lisek/esp/esp-idf/components/ethernet/Kconfig \
	/home/Lisek/esp/esp-idf/components/fatfs/Kconfig \
	/home/Lisek/esp/esp-idf/components/freemodbus/Kconfig \
	/home/Lisek/esp/esp-idf/components/freertos/Kconfig \
	/home/Lisek/esp/esp-idf/components/heap/Kconfig \
	/home/Lisek/esp/esp-idf/components/libsodium/Kconfig \
	/home/Lisek/esp/esp-idf/components/log/Kconfig \
	/home/Lisek/esp/esp-idf/components/lwip/Kconfig \
	/home/Lisek/esp/esp-idf/components/mbedtls/Kconfig \
	/home/Lisek/esp/esp-idf/components/mdns/Kconfig \
	/home/Lisek/esp/esp-idf/components/mqtt/Kconfig \
	/home/Lisek/esp/esp-idf/components/nvs_flash/Kconfig \
	/home/Lisek/esp/esp-idf/components/openssl/Kconfig \
	/home/Lisek/esp/esp-idf/components/pthread/Kconfig \
	/home/Lisek/esp/esp-idf/components/spi_flash/Kconfig \
	/home/Lisek/esp/esp-idf/components/spiffs/Kconfig \
	/home/Lisek/esp/esp-idf/components/tcpip_adapter/Kconfig \
	/home/Lisek/esp/esp-idf/components/unity/Kconfig \
	/home/Lisek/esp/esp-idf/components/vfs/Kconfig \
	/home/Lisek/esp/esp-idf/components/wear_levelling/Kconfig \
	/home/Lisek/esp/esp-idf/components/app_update/Kconfig.projbuild \
	/home/Lisek/esp/esp-idf/components/bootloader/Kconfig.projbuild \
	/home/Lisek/esp/esp-idf/components/esptool_py/Kconfig.projbuild \
	/c/Users/Lisek/eclipsecpp-workspace/sensors_mesh/main/Kconfig.projbuild \
	/home/Lisek/esp/esp-idf/components/partition_table/Kconfig.projbuild \
	/home/Lisek/esp/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)

ifneq "$(IDF_TARGET)" "esp32"
include/config/auto.conf: FORCE
endif
ifneq "$(IDF_CMAKE)" "n"
include/config/auto.conf: FORCE
endif

$(deps_config): ;
