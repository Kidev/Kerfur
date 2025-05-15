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

all: wipe desktop

desktop:
	cmake -S . -B $(BUILD_DIR) -DVERSION_TAG=$(VERSION_TAG) -DCMAKE_BUILD_TYPE=Release -DQT_ROOT_DIR=$(QT_ROOT_DIR) -DCMAKE_INSTALL_PREFIX=$(ABS_INSTALL_DIR)
	cmake --build $(BUILD_DIR)
	cmake --install $(BUILD_DIR)

clean:
	- rm -rI $(BUILD_DIR) $(ABS_INSTALL_DIR)

wipe:
	rm -rf $(BUILD_DIR) $(ABS_INSTALL_DIR)

.PHONY: desktop clean wipe
.IGNORE: clean
