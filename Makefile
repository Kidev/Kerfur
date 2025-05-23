define MISSING_QT_ROOT
Error: QT_ROOT_DIR is not defined!

Please configure QT_ROOT_DIR before running `make`. Either:

  - Export it before calling `make`:
      export QT_ROOT_DIR=/path/to/Qt make <target>

  - Pass it on the `make` command line:
      make QT_ROOT_DIR=/path/to/Qt <target>

The folder /path/to/Qt must point to the appropriate version and architecture for `host`.
For example, for Qt 6.8.3 `gcc_64` (on Linux): `/opt/Qt/6.8.3/gcc_64`

You must fix this
endef

ifndef QT_ROOT_DIR
$(error $(MISSING_QT_ROOT))
endif

WASM_PROJECT_NAME ?= kerfur
WEBPAGE_TITLE ?= Kerfur
VERSION_TAG ?= "0.0.0"
ASSETS_DIR ?= assets
BUILD_DIR ?= build
INSTALL_DIR ?= install
WASM_ARCH ?= wasm_multithread
BUILD_MODE ?= Release
BUILD_MODE_WEB ?= MinSizeRel
BROTLI_EXTENSIONS ?= js css html wasm png ico wav mp3
ABS_INSTALL_DIR := $(abspath $(INSTALL_DIR))
QT_ROOT_DIR_TARGET ?= $(abspath $(QT_ROOT_DIR)/../$(WASM_ARCH))
QT_VERSION := $(notdir $(abspath $(QT_ROOT_DIR)/..))
QT_NAME := Qt$(shell echo $(QT_VERSION) | cut -c1)
QT_HOST_CMAKE_DIR := $(QT_ROOT_DIR)/lib/cmake
QT_MODULE_PATH := $(QT_ROOT_DIR_TARGET)/lib/cmake/$(QT_NAME)
QT_TOOLCHAIN := $(QT_ROOT_DIR_TARGET)/lib/cmake/$(QT_NAME)/qt.toolchain.cmake
SHELL = /bin/bash

all: wipe desktop

desktop:
	cmake -S . -B $(BUILD_DIR) \
	-DVERSION_TAG=$(VERSION_TAG) \
	-DCMAKE_BUILD_TYPE=$(BUILD_MODE) \
	-DQT_ROOT_DIR=$(QT_ROOT_DIR) \
	-DCMAKE_INSTALL_PREFIX=$(ABS_INSTALL_DIR)
	cmake --build $(BUILD_DIR) --config $(BUILD_MODE)
	cmake --install $(BUILD_DIR) --config $(BUILD_MODE)

emsdk:
	@EMSDK_VERSION=""; \
	case "$(QT_VERSION)" in \
		6.2*) EMSDK_VERSION="2.0.14" ;; \
		6.3*) EMSDK_VERSION="3.0.0" ;; \
		6.4*) EMSDK_VERSION="3.1.14" ;; \
		6.5*) EMSDK_VERSION="3.1.25" ;; \
		6.6*) EMSDK_VERSION="3.1.37" ;; \
		6.7*) EMSDK_VERSION="3.1.50" ;; \
		6.8*) EMSDK_VERSION="3.1.56" ;; \
		6.9*) EMSDK_VERSION="3.1.70" ;; \
		*) \
			echo "Error: Unsupported Qt version: $(QT_VERSION)"; \
			exit 1; \
			;; \
	esac; \
	if [ -z "$$EMSDK_VERSION" ]; then \
		echo "Error: Failed to determine Emscripten version for Qt $(QT_VERSION)"; \
		exit 1; \
	fi; \
	if [ ! -d "emsdk" ]; then \
		echo "Cloning emsdk repository..."; \
		git clone https://github.com/emscripten-core/emsdk.git || { echo "Error: Failed to clone emsdk"; exit 1; }; \
	fi; \
	echo "Using Emscripten version: $$EMSDK_VERSION for Qt $(QT_VERSION)"; \
	INSTALLED_VERSION=$$(./emsdk/emsdk list | grep '\*' | grep -o '[0-9]\+\.[0-9]\+\.[0-9]\+' || echo ""); \
	if [ "$$INSTALLED_VERSION" != "$$EMSDK_VERSION" ]; then \
		echo "Installing Emscripten version $$EMSDK_VERSION..."; \
		./emsdk/emsdk install $$EMSDK_VERSION || { echo "Error: Failed to install Emscripten"; exit 1; }; \
	fi; \
	ACTIVE_VERSION=$$(./emsdk/emsdk list | grep 'ACTIVE' | grep -o '[0-9]\+\.[0-9]\+\.[0-9]\+' || echo ""); \
	if [ "$$ACTIVE_VERSION" != "$$EMSDK_VERSION" ]; then \
		echo "Activating Emscripten version $$EMSDK_VERSION..."; \
		./emsdk/emsdk activate $$EMSDK_VERSION || { echo "Error: Failed to activate Emscripten"; exit 1; }; \
		source ./emsdk/emsdk_env.sh || { echo "Error: Failed to source environment"; exit 1; }; \
	fi

web: wipe emsdk
	@. ./emsdk/emsdk_env.sh && \
	emcmake \
	cmake -S . -B $(BUILD_DIR) \
	-DVERSION_TAG=$(VERSION_TAG) \
	-DCMAKE_BUILD_TYPE=$(BUILD_MODE_WEB) \
	-DQT_ROOT_DIR=$(QT_ROOT_DIR) \
	-DEMSCRIPTEN=ON \
	-DCMAKE_PREFIX_PATH=$(QT_ROOT_DIR_TARGET) \
	-DCMAKE_INSTALL_PREFIX=$(ABS_INSTALL_DIR) \
	-DQt6_DIR=$(QT_MODULE_PATH) \
	-DCMAKE_TOOLCHAIN_FILE=$(QT_TOOLCHAIN) \
	-DCMAKE_PREFIX_PATH=$(QT_ROOT_DIR_TARGET) && \
	cmake --build $(BUILD_DIR)

	mkdir -p $(ABS_INSTALL_DIR)
	cp -r $(ASSETS_DIR) $(ABS_INSTALL_DIR)

	cp $(BUILD_DIR)/$(WASM_PROJECT_NAME).html $(ABS_INSTALL_DIR)/index.html
	cp $(BUILD_DIR)/*.js $(ABS_INSTALL_DIR)
	cp $(BUILD_DIR)/*.wasm $(ABS_INSTALL_DIR)
	cp $(BUILD_DIR)/*.svg $(ABS_INSTALL_DIR)
	cp logo.png $(ABS_INSTALL_DIR)
	cp favicon.ico $(ABS_INSTALL_DIR)

	sed -i 's#<title>$(WASM_PROJECT_NAME)</title>#<title>$(WEBPAGE_TITLE) | Kidev.org<\/title><link rel="icon" href="favicon.ico" type="image/x-icon">#g' $(ABS_INSTALL_DIR)/index.html
	sed -i "s#<strong>Qt for WebAssembly: $(WASM_PROJECT_NAME)</strong>#<h1 style='color:\#ffffff;'><strong>$(WEBPAGE_TITLE)</strong></h1><span style='color:\#ffffff;'>Written by Kidev using Qt</span><br><br><img src='qtlogo.svg' width='160' height='100' style='display:block'>#g" $(ABS_INSTALL_DIR)/index.html
	sed -i "s# height: 100% }# height: 100%; background-color:\#01010c; }#g" $(ABS_INSTALL_DIR)/index.html
	sed -i 's#<img src="qtlogo.svg" width="320" height="200" style="display:block"></img>#<img src="logo.png" width="260" height="260" style="display:block">#g' $(ABS_INSTALL_DIR)/index.html
	sed -i 's#<div id="qtstatus">#<div id="qtstatus" style="color:\#ffffff; font-weight:bold">#g' $(ABS_INSTALL_DIR)/index.html
	sed -i 's#undefined ? ` with code ` :#undefined ? ` with code ${exitData.code}` :#g' $(ABS_INSTALL_DIR)/index.html
	sed -i 's#undefined ? ` ()` :#undefined ? `<br><span style="color:\#ff0000">Error: ${exitData.text}</span>` :#g' $(ABS_INSTALL_DIR)/index.html
	sed -i 's/\/\*.*\*\///g' $(ABS_INSTALL_DIR)/index.html
	sed -i '/<!--/,/-->/d' $(ABS_INSTALL_DIR)/index.html

	@extensions_expr=""; \
	for ext in $(BROTLI_EXTENSIONS); do \
		if [ -z "$$extensions_expr" ]; then \
			extensions_expr="-name \"*.$${ext}\""; \
		else \
			extensions_expr="$${extensions_expr} -o -name \"*.$${ext}\""; \
		fi; \
	done; \
	cmd="find $(ABS_INSTALL_DIR)/ -type f \( $$extensions_expr \) -exec brotli --best --force {} \;"; \
	echo "Compressing files with extensions: $(BROTLI_EXTENSIONS)"; \
	eval $$cmd

run-web:
	@if [ ! -f "$(ABS_INSTALL_DIR)/index.html" ]; then \
		echo "Error: Web build install folder not found. Run 'make web' first."; \
		exit 1; \
	fi; \
	source ./emsdk/emsdk_env.sh && \
	emrun $(ABS_INSTALL_DIR)/index.html --kill_start --kill_exit

clean:
	- rm -rI $(BUILD_DIR) $(ABS_INSTALL_DIR)

wipe:
	rm -rf $(BUILD_DIR) $(ABS_INSTALL_DIR) emsdk

.PHONY: desktop clean wipe web run-web emsdk
.IGNORE: clean
