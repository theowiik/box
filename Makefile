# The whole repo from the top. The box itself is firmware for an ESP-Mosaico
# (firmware/); everything else is a connector that POSTs events to it.
.PHONY: help firmware install logs screenshot status test-firmware check format \
        sources demo send gif

# Machine-specific settings (see .env.example); a real environment variable wins.
-include .env
export MOSAICO_WORKSPACE
# Prefix that runs a command with ESP-IDF loaded, if your shell does not have it
# already (on NixOS, the FHS wrapper). Empty means "already in this shell".
MOSAICO_SHELL ?=
MOSAICO = $(MOSAICO_SHELL) python $(MOSAICO_WORKSPACE)/mosaico.py --workspace $(MOSAICO_WORKSPACE)
FIRMWARE = $(CURDIR)/firmware
VISUAL ?= rainbow

help:                    ## list the targets
	@grep -hE '^[a-z-]+:.*## ' $(MAKEFILE_LIST) | sed 's/:.*## /\t/'

# --- the box (needs MOSAICO_WORKSPACE, and the board on USB for install) -----

firmware:                ## build the firmware
	cd firmware && $(MOSAICO_SHELL) idf.py -D IDF_TARGET=esp32s31 app

install:                 ## build and install the firmware on the board over USB
	$(MOSAICO) iris system-update --project $(FIRMWARE)

logs:                    ## follow the board's log
	$(MOSAICO) iris logs --project $(FIRMWARE)

screenshot:              ## save what the panel shows to box.png
	$(MOSAICO) iris screenshot $(CURDIR)/box.png --project $(FIRMWARE)

status:                  ## what the panel shows, over Wi-Fi
	@curl -fsS $${BOX_URL:-http://box.local}/api/display; echo

# --- checks --------------------------------------------------------------------

test-firmware:           ## the firmware's core, built and tested on this machine (ASan)
	cd firmware/host && $(MOSAICO_SHELL) sh -c \
	  'cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build build && ./build/box_core_tests'

check: test-firmware     ## everything: firmware core tests, connectors' lint and tests
	cd connectors/sources && uv run ruff format --check . && uv run ruff check . && uv run pytest
	cd connectors/gifcast && uv run ruff format --check . && uv run ruff check . && uv run pytest

format:                  ## format the connectors
	cd connectors/sources && uv run ruff format . && uv run ruff check --fix .
	cd connectors/gifcast && uv run ruff format . && uv run ruff check --fix .

# --- connectors (run from any machine on the box's network) ------------------

sources:                 ## run weather and feeds against the box until stopped
	cd connectors/sources && uv run box-sources run

demo:                    ## play a built-in visual: make demo VISUAL=beer
	cd connectors/sources && uv run box-sources visual $(VISUAL)

send:                    ## show some text: make send TEXT="hello"
	cd connectors/sources && uv run box-sources send "$(TEXT)"

gif:                     ## send a GIF: make gif GIF=party.gif
	cd connectors/gifcast && uv run gifcast $(abspath $(GIF))
