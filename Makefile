define MISSING_QT_ROOT
Error: QT_ROOT_DIR is not defined!

Please configure QT_ROOT_DIR before running `make`. Either:

  - Export it before calling `make`:
      export QT_ROOT_DIR=/path/to/Qt make <target>

  - Pass it on the `make` command line:
      make QT_ROOT_DIR=/path/to/Qt <target>

The folder /path/to/Qt must point to the appropriate version and architecture.
For example, for Qt 6.8.3 (Linux gcc_64): /opt/Qt/6.8.3/gcc_64.

You must fix this
endef

ifndef QT_ROOT_DIR
$(error $(MISSING_QT_ROOT))
endif

VERSION_TAG ?= "0.0.0"
BUILD_DIR ?= build
INSTALL_DIR ?= install
ABS_INSTALL_DIR := $(abspath $(INSTALL_DIR))
QT_ROOT_DIR_TARGET := $(abspath $(QT_ROOT_DIR)/../wasm_multithread)
QT_VERSION := $(notdir $(abspath $(QT_ROOT_DIR)/..))
QT_NAME := Qt$(shell echo $(QT_VERSION) | cut -c1)
QT_HOST_CMAKE_DIR := $(QT_ROOT_DIR)/lib/cmake
QT_MODULE_PATH := $(QT_ROOT_DIR_TARGET)/lib/cmake/$(QT_NAME)
QT_TOOLCHAIN := $(QT_ROOT_DIR_TARGET)/lib/cmake/$(QT_NAME)/qt.toolchain.cmake


all: wipe desktop

desktop:
	cmake -S . -B $(BUILD_DIR) \
	-DVERSION_TAG=$(VERSION_TAG) \
	-DCMAKE_BUILD_TYPE=Release \
	-DQT_ROOT_DIR=$(QT_ROOT_DIR) \
	-DCMAKE_INSTALL_PREFIX=$(ABS_INSTALL_DIR)
	cmake --build $(BUILD_DIR)
	cmake --install $(BUILD_DIR)

web: clean emsdk
	. ./emsdk/emsdk_env.sh && \
	./emsdk/upstream/emscripten/emcmake \
	cmake -S . -B $(BUILD_DIR) \
	-DVERSION_TAG=$(VERSION_TAG) \
	-DCMAKE_BUILD_TYPE=MinSizeRel \
	-DQT_ROOT_DIR=$(QT_ROOT_DIR) \
	-DEMSCRIPTEN=ON \
	-DCMAKE_PREFIX_PATH=$(QT_ROOT_DIR_TARGET) \
	-DCMAKE_INSTALL_PREFIX=$(ABS_INSTALL_DIR) \
	-DQt6_DIR=$(QT_MODULE_PATH) \
	-DCMAKE_TOOLCHAIN_FILE=$(QT_TOOLCHAIN) \
	-DCMAKE_PREFIX_PATH=$(QT_ROOT_DIR_TARGET)
	. ./emsdk/emsdk_env.sh && \
	cmake --build $(BUILD_DIR)

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
		. ./emsdk/emsdk_env.sh || { echo "Error: Failed to source environment"; exit 1; }; \
	fi

run-web:
	@if [ ! -f "./build/index.html" ]; then \
		echo "Error: Web build not found. Run 'make web' first."; \
		exit 1; \
	fi
	./emsdk/upstream/emscripten/emrun ./build/index.html --kill_start --kill_exit

clean:
	- rm -rI $(BUILD_DIR) $(ABS_INSTALL_DIR)

wipe:
	rm -rf $(BUILD_DIR) $(ABS_INSTALL_DIR)

.PHONY: desktop clean wipe web run-web emsdk
.IGNORE: clean
