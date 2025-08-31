ifneq ("$(wildcard .env)","")
  $(info A '.env' file was found and included)
  include .env
  export
endif
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

VCPKG_ROOT ?= $(shell pwd)/vcpkg
VCPKG_TRIPLET ?= $(shell \
    if [ "$$(uname -s)" = "Linux" ]; then \
        if [ "$$(uname -m)" = "x86_64" ]; then echo "x64-linux-release"; \
        elif [ "$$(uname -m)" = "aarch64" ]; then echo "arm64-linux-release"; \
        else echo "$$(uname -m)-linux-release"; fi; \
    elif [ "$$(uname -s)" = "Darwin" ]; then \
        if [ "$$(uname -m)" = "x86_64" ]; then echo "x64-osx-release"; \
        elif [ "$$(uname -m)" = "arm64" ]; then echo "arm64-osx-release"; \
        else echo "$$(uname -m)-osx-release"; fi; \
    elif echo "$$(uname -s)" | grep -q "MINGW\|MSYS\|CYGWIN"; then \
        if [ "$$(uname -m)" = "x86_64" ]; then echo "x64-windows-static-release"; \
        elif [ "$$(uname -m)" = "i686" ]; then echo "x86-windows-static-release"; \
        elif [ "$$(uname -m)" = "aarch64" ]; then echo "arm64-windows-static-release"; \
        else echo "x64-windows-static-release"; fi; \
    else echo "x64-linux-release"; fi)
VCPKG_TOOLCHAIN := $(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
OS_NAME_UNAME := $(shell uname -s)
WINDOWS_ENV := $(OS)
IS_WINDOWS := $(if $(or $(findstring MINGW,$(OS_NAME_UNAME)),$(findstring MSYS,$(OS_NAME_UNAME)),$(findstring CYGWIN,$(OS_NAME_UNAME)),$(findstring Windows_NT,$(WINDOWS_ENV))),1,0)
OS_NAME := $(if $(filter 1,$(IS_WINDOWS)),Windows,$(OS_NAME_UNAME))
EXE_EXT := $(if $(filter 1,$(IS_WINDOWS)),.exe,)
PROJECT_BINARY ?= kerfur
PROJECT_TITLE ?= Kerfur
PROJECT_ORG := kidev
PROJECT_TLD := org
PROJECT_DOMAIN := $(PROJECT_ORG).$(PROJECT_TLD)
VERSION_TAG ?= "0.0.0"
ASSETS_DIR := assets
BUILD_DIR := build
INSTALL_DIR := install
MODELS_DIR := models
WASM_ARCH ?= wasm_multithread
BUILD_NAME ?=
BUILD_TIME := $(shell date +"%Y-%m-%d %H:%M:%S")
BUILD_QUALIFIER := $(OS_NAME)$(BUILD_NAME)
BUILD_MODE ?= Release
BUILD_MODE_WEB_RELEASE ?= MinSizeRel
BROTLI_EXTENSIONS ?= js css html wasm png ico wav mp3
ABS_INSTALL_DIR := $(abspath $(INSTALL_DIR))
QT_ROOT_DIR_TARGET ?= $(abspath $(QT_ROOT_DIR)/../$(WASM_ARCH))
QT_ROOT_DIR_HOST ?= $(abspath $(QT_ROOT_DIR))
QT_VERSION := $(notdir $(abspath $(QT_ROOT_DIR)/..))
QT_NAME := Qt$(shell echo $(QT_VERSION) | cut -c1)
QT_HOST_CMAKE_DIR := $(QT_ROOT_DIR)/lib/cmake
QT_MODULE_PATH_TARGET := $(QT_ROOT_DIR_TARGET)/lib/cmake/$(QT_NAME)
QT_TOOLCHAIN_TARGET := $(QT_ROOT_DIR_TARGET)/lib/cmake/$(QT_NAME)/qt.toolchain.cmake
QT_MODULE_PATH_HOST := $(QT_ROOT_DIR_HOST)/lib/cmake/$(QT_NAME)
QT_TOOLCHAIN_HOST := $(QT_ROOT_DIR_HOST)/lib/cmake/$(QT_NAME)/qt.toolchain.cmake
QT_TOOLS_DIR := $(QT_ROOT_DIR)/../../Tools
SHELL = /bin/bash
QT_IFW_VERSION ?= $(shell ls "$(QT_TOOLS_DIR)/QtInstallerFramework" 2>/dev/null | grep -E '^[0-9]+\.[0-9]+' | sort -V | tail -1)
INSTALLER_BIN_DIR ?= $(QT_TOOLS_DIR)/QtInstallerFramework/$(QT_IFW_VERSION)/bin
REPO_NAME ?= $(PROJECT_TLD)_$(PROJECT_ORG)_$(PROJECT_BINARY)_$(BUILD_QUALIFIER)
TARGET_PACKAGE ?= $(PROJECT_TLD).$(PROJECT_ORG).$(PROJECT_BINARY).$(BUILD_QUALIFIER)
INSTALLER_NAME ?= $(PROJECT_TITLE)Installer-$(BUILD_QUALIFIER)
SCRIPT_EXT := $(if $(filter 1,$(IS_WINDOWS)),.bat,)
CDN_UPLOAD_SOCKET ?=
CDN_UPLOAD_USERNAME ?=
CDN_UPLOAD_PASSWORD ?=
WASM_UPLOAD_SOCKET ?=
WASM_UPLOAD_USERNAME ?=
WASM_UPLOAD_PASSWORD ?=
MODEL_VISION_DETECT ?= face_detection_yunet_2023mar.onnx
MODEL_VISION_DETECT_URL ?= https://huggingface.co/opencv/face_detection_yunet/resolve/main
MODEL_VISION_IDENTIFY ?= face_recognition_sface_2021dec.onnx
MODEL_VISION_IDENTIFY_URL ?= https://huggingface.co/opencv/face_recognition_sface/resolve/main
MODEL_VISION_IDENTIFY_GALLERY ?= assets/faces
MODEL_HEARING_GGML ?= ggml-small.bin
MODEL_HEARING_GGML_URL ?= https://huggingface.co/ggerganov/whisper.cpp/resolve/main
QRC_TEMPLATE := resources.qrc.template
QRC_OUTPUT := resources.qrc
QRC_PREFIX ?= /
EXTRA_QRC_FILES := \
	$(MODELS_DIR)/$(MODEL_VISION_DETECT) \
	$(MODELS_DIR)/$(MODEL_VISION_IDENTIFY) \
	$(MODELS_DIR)/$(MODEL_HEARING_GGML)

ifeq ($(DEBUG),1)
  BUILD_MODE := Debug
else
  ifeq ($(WEB),1)
    BUILD_MODE := $(BUILD_MODE_WEB_RELEASE)
  else
    BUILD_MODE := Release
  endif
endif

all:
	@echo "Building with Release configuration for desktop"
	@$(MAKE) DEBUG=0 WEB=0 clean desktop ensure-config

debug:
	@echo "Building with Debug configuration for desktop"
	@$(MAKE) DEBUG=1 WEB=0 clean desktop ensure-config

web:
	@echo "Building with MinSizeRel configuration for web"
	@$(MAKE) DEBUG=0 WEB=1 clean emsdk setup-vcpkg handle-resources web-build ensure-config

web-debug:
	@echo "Building with Debug configuration for web"
	@$(MAKE) DEBUG=1 WEB=1 clean emsdk setup-vcpkg handle-resources web-build ensure-config

setup-vcpkg:
	@if [ ! -d "$(VCPKG_ROOT)" ]; then \
		echo "Cloning vcpkg..."; \
		git clone https://github.com/Microsoft/vcpkg.git $(VCPKG_ROOT); \
	fi
	@if [ ! -f "$(VCPKG_ROOT)/vcpkg" ]; then \
		echo "Bootstrapping vcpkg..."; \
		cd $(VCPKG_ROOT) && ./bootstrap-vcpkg.sh -disableMetrics && \
		./vcpkg install --triplet $(VCPKG_TRIPLET) --disable-metrics; \
	fi

download-models:
	@if [ ! -f "${MODELS_DIR}/${MODEL_VISION_DETECT}" ]; then \
		mkdir -p ${MODELS_DIR}; \
		echo "Downloading face detection model..."; \
		wget -O "${MODELS_DIR}/${MODEL_VISION_DETECT}" "${MODEL_VISION_DETECT_URL}/${MODEL_VISION_DETECT}"; \
	else \
		echo "Face detection model found: ${MODEL_VISION_DETECT}"; \
	fi
	@if [ ! -f "${MODELS_DIR}/${MODEL_VISION_IDENTIFY}" ]; then \
		mkdir -p ${MODELS_DIR}; \
		echo "Downloading face recognition model..."; \
		wget -O "${MODELS_DIR}/${MODEL_VISION_IDENTIFY}" "${MODEL_VISION_IDENTIFY_URL}/${MODEL_VISION_IDENTIFY}"; \
	else \
		echo "Face recognition model found: ${MODEL_VISION_IDENTIFY}"; \
	fi
	@if [ ! -f "${MODELS_DIR}/${MODEL_HEARING_GGML}" ]; then \
		mkdir -p ${MODELS_DIR}; \
		echo "Downloading speech to text model..."; \
		wget -O "${MODELS_DIR}/${MODEL_HEARING_GGML}" "${MODEL_HEARING_GGML_URL}/${MODEL_HEARING_GGML}"; \
	else \
		echo "Speech to text model found: ${MODEL_HEARING_GGML}"; \
	fi

create-resources-file:
	@rm -f "$(QRC_OUTPUT)"; \
	extras="$(EXTRA_QRC_FILES)"; \
	awk -v prefix="$(QRC_PREFIX)" -v extras="$$extras" '\
	  BEGIN { n = split(extras, a, /[[:space:]]+/) } \
	  /<qresource[[:space:]]+prefix=/ { sub(/prefix="[^"]*"/, "prefix=\"" prefix "\"") } \
	  /<\/qresource>/ { \
	    for (i = 1; i <= n; ++i) if (length(a[i])) printf "        <file>%s</file>\n", a[i]; \
	  } \
	  { print }' \
	  "$(QRC_TEMPLATE)" > "$(QRC_OUTPUT)"; \
	echo "Generated $(QRC_OUTPUT) (prefix='$(QRC_PREFIX)', extras: $$([ -n "$$extras" ] && echo "$$extras" || echo none))"


handle-resources: download-models create-resources-file

repo:
	mkdir -p "installer/packages/$(TARGET_PACKAGE)/data"
	cp -Rf "$(ABS_INSTALL_DIR)" "installer/packages/$(TARGET_PACKAGE)/data/"
	mv -f "installer/packages/$(TARGET_PACKAGE)/data/$(INSTALL_DIR)" "installer/packages/$(TARGET_PACKAGE)/data/$(PROJECT_TITLE)"
	rm -rf $(REPO_NAME)
	$(INSTALLER_BIN_DIR)/repogen$(EXE_EXT) -p installer/packages -i $(TARGET_PACKAGE) $(REPO_NAME)

upload-repo:
	@if [ -n "$(CDN_UPLOAD_USERNAME)" ] && [ -n "$(CDN_UPLOAD_PASSWORD)" ] && [ -n "$(CDN_UPLOAD_SOCKET)" ]; then \
		lftp -u "$(CDN_UPLOAD_USERNAME),$(CDN_UPLOAD_PASSWORD)" "$(CDN_UPLOAD_SOCKET)" -e "\
		set ssl:verify-certificate no; \
		mirror -R --only-newer --verbose $(REPO_NAME) $(BUILD_QUALIFIER); \
		quit"; \
	else \
		echo "No CDN credentials, will NOT update the remote with repo $(REPO_NAME)"; \
	fi

upload-web:
	@if [ -n "$(WASM_UPLOAD_USERNAME)" ] && [ -n "$(WASM_UPLOAD_PASSWORD)" ] && [ -n "$(WASM_UPLOAD_SOCKET)" ]; then \
		lftp -u "$(WASM_UPLOAD_USERNAME),$(WASM_UPLOAD_PASSWORD)" "$(WASM_UPLOAD_SOCKET)" -e "\
		set ssl:verify-certificate no; \
		mirror -R --only-newer --verbose $(ABS_INSTALL_DIR) $(PROJECT_TITLE); \
		quit"; \
	else \
		echo "No WASM webserver credentials, will NOT update the WASM demo"; \
	fi

create-installer:
	rm -rf $(INSTALLER_NAME)
	$(INSTALLER_BIN_DIR)/binarycreator$(EXE_EXT) -p installer/packages -c installer/config/config.xml -e $(TARGET_PACKAGE) $(INSTALLER_NAME)

installer: setup-installer repo upload-repo create-installer

setup-installer:
	@echo "Setting up installer configuration..."

	mkdir -p installer/config/meta
	mkdir -p installer/packages/$(TARGET_PACKAGE)/meta

	@echo '<?xml version="1.0"?>' > installer/config/config.xml
	@echo '<Installer>' >> installer/config/config.xml
	@echo '    <Name>$(PROJECT_TITLE) Installer for $(BUILD_QUALIFIER)</Name>' >> installer/config/config.xml
	@echo '    <Version>$(VERSION_TAG)</Version>' >> installer/config/config.xml
	@echo '    <Title>$(PROJECT_TITLE) Installer</Title>' >> installer/config/config.xml
	@echo '    <Publisher>$(PROJECT_ORG)</Publisher>' >> installer/config/config.xml
	@echo '    <ProductUrl>https://www.$(PROJECT_DOMAIN)</ProductUrl>' >> installer/config/config.xml
	@echo '    <InstallerWindowIcon>icon</InstallerWindowIcon>' >> installer/config/config.xml
	@echo '    <InstallerApplicationIcon>icon</InstallerApplicationIcon>' >> installer/config/config.xml
	@echo '    <Banner>banner.png</Banner>' >> installer/config/config.xml
	@echo '    <Logo>logo.png</Logo>' >> installer/config/config.xml
	@echo '    <RunProgram>@TargetDir@/$(PROJECT_TITLE)/$(PROJECT_TITLE)$(SCRIPT_EXT)</RunProgram>' >> installer/config/config.xml
	@echo '    <RunProgramDescription>Run $(PROJECT_TITLE)</RunProgramDescription>' >> installer/config/config.xml
	@echo '    <RunProgramArguments></RunProgramArguments>' >> installer/config/config.xml
	@echo '    <StartMenuDir>$(PROJECT_TITLE)</StartMenuDir>' >> installer/config/config.xml
	@echo '    <MaintenanceToolName>$(PROJECT_TITLE)Updater</MaintenanceToolName>' >> installer/config/config.xml
	@echo '    <AllowNonAsciiCharacters>true</AllowNonAsciiCharacters>' >> installer/config/config.xml
	@echo '    <WizardStyle>Modern</WizardStyle>' >> installer/config/config.xml
	@echo '    <TargetDir>@ApplicationsDir@/$(PROJECT_TITLE)</TargetDir>' >> installer/config/config.xml
	@echo '    <AdminTargetDir>@ApplicationsDir@/$(PROJECT_TITLE)</AdminTargetDir>' >> installer/config/config.xml
	@echo '    <WizardDefaultWidth>800</WizardDefaultWidth>' >> installer/config/config.xml
	@echo '    <WizardDefaultHeight>500</WizardDefaultHeight>' >> installer/config/config.xml
	@echo '    <WizardMinimumWidth>800</WizardMinimumWidth>' >> installer/config/config.xml
	@echo '    <WizardMinimumHeight>500</WizardMinimumHeight>' >> installer/config/config.xml
	@echo '    <WizardShowPageList>false</WizardShowPageList>' >> installer/config/config.xml
	@echo '    <InstallActionColumnVisible>false</InstallActionColumnVisible>' >> installer/config/config.xml
	@echo '    <RepositorySettingsPageVisible>false</RepositorySettingsPageVisible>' >> installer/config/config.xml
	@echo '    <RemoteRepositories>' >> installer/config/config.xml
	@echo '        <Repository>' >> installer/config/config.xml
	@echo '            <DisplayName>$(PROJECT_DOMAIN) CDN</DisplayName>' >> installer/config/config.xml
	@echo '            <Url>https://cdn.$(PROJECT_DOMAIN)/$(BUILD_QUALIFIER)</Url>' >> installer/config/config.xml
	@echo '            <Enabled>1</Enabled>' >> installer/config/config.xml
	@echo '        </Repository>' >> installer/config/config.xml
	@echo '    </RemoteRepositories>' >> installer/config/config.xml
	@echo '</Installer>' >> installer/config/config.xml

	@echo '<?xml version="1.0"?>' > installer/config/meta/package.xml
	@echo '<Package>' >> installer/config/meta/package.xml
	@echo '    <DisplayName>$(PROJECT_TITLE)</DisplayName>' >> installer/config/meta/package.xml
	@echo '    <Description>$(PROJECT_TITLE) by $(PROJECT_DOMAIN)</Description>' >> installer/config/meta/package.xml
	@echo '    <Version>$(VERSION_TAG)</Version>' >> installer/config/meta/package.xml
	@echo '    <ReleaseDate>$(BUILD_TIME)</ReleaseDate>' >> installer/config/meta/package.xml
	@echo '    <Name>$(TARGET_PACKAGE)</Name>' >> installer/config/meta/package.xml
	@echo '    <Licenses>' >> installer/config/meta/package.xml
	@echo '        <License name="$(PROJECT_TITLE)'"'"'s Software License Agreement" file="license.txt" />' >> installer/config/meta/package.xml
	@echo '    </Licenses>' >> installer/config/meta/package.xml
	@echo '    <Script></Script>' >> installer/config/meta/package.xml
	@echo '    <SortingPriority>10</SortingPriority>' >> installer/config/meta/package.xml
	@echo '    <UpdateText>A cute little robot</UpdateText>' >> installer/config/meta/package.xml
	@echo '    <Default>true</Default>' >> installer/config/meta/package.xml
	@echo '    <RequiresAdminRights>false</RequiresAdminRights>' >> installer/config/meta/package.xml
	@echo '    <ForcedInstallation>true</ForcedInstallation>' >> installer/config/meta/package.xml
	@echo '    <ForcedUpdate>false</ForcedUpdate>' >> installer/config/meta/package.xml
	@echo '</Package>' >> installer/config/meta/package.xml

	cp -r installer/config/meta installer/packages/$(TARGET_PACKAGE)/

	@echo "Installer configuration completed!"
	@echo "Generated files:"
	@echo "  - installer/config/config.xml"
	@echo "  - installer/config/meta/package.xml"
	@echo "  - installer/packages/$(TARGET_PACKAGE)/meta/package.xml"


deskop-configure:
	cmake -S . -B $(BUILD_DIR) \
	-DVERSION_TAG=$(VERSION_TAG) \
	-DPROJECT_TITLE=$(PROJECT_TITLE) \
	-DPROJECT_ORG=$(PROJECT_ORG) \
	-DPROJECT_DOMAIN=$(PROJECT_DOMAIN) \
	-DMODEL_VISION_DETECT=$(MODEL_VISION_DETECT) \
	-DMODEL_VISION_IDENTIFY=$(MODEL_VISION_IDENTIFY) \
	-DMODEL_HEARING_GGML=$(MODEL_HEARING_GGML) \
	-DCMAKE_BUILD_TYPE=$(BUILD_MODE) \
	-DQT_ROOT_DIR=$(QT_ROOT_DIR) \
	-DEMSCRIPTEN=OFF \
	-DCMAKE_PREFIX_PATH=$(QT_ROOT_DIR_HOST) \
	-DCMAKE_INSTALL_PREFIX=$(ABS_INSTALL_DIR) \
	-DQt6_DIR=$(QT_MODULE_PATH_HOST) \
	-DBUILD_TIME="$(BUILD_TIME)" \
	-DBUILD_NAME="$(BUILD_NAME)" \
	-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_TOOLCHAIN) \
	-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$(QT_TOOLCHAIN_HOST) \
	-DVCPKG_TARGET_TRIPLET=$(VCPKG_TRIPLET)

desktop-build:
	cmake --build $(BUILD_DIR) --config $(BUILD_MODE)

desktop-install:
	cmake --install $(BUILD_DIR) --config $(BUILD_MODE)

desktop-fast: desktop-build

desktop: setup-vcpkg handle-resources deskop-configure desktop-build desktop-install shortcut

shortcut:
ifeq ($(IS_WINDOWS),1)
	@echo @echo off > "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE).bat"
	@echo set "HERE=%~dp0" >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE).bat"
	@echo set "PATH=%HERE%bin;%HERE%lib;%PATH%" >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE).bat"
	@echo 'if exist "%HERE%bin\$(PROJECT_BINARY).exe" (' >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE).bat"
	@echo '  "%HERE%bin\$(PROJECT_BINARY).exe" %*' >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE).bat"
	@echo ') else (' >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE).bat"
	@echo '  "%HERE%bin\$(PROJECT_BINARY)" %*' >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE).bat"
	@echo ')' >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE).bat"
	@echo Creating Windows launcher: $(PROJECT_TITLE).bat
else
	@echo '#!/bin/bash' > "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE)"
	@echo 'HERE="$$(dirname "$$(readlink -f "$${0}")")"' >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE)"
	@echo 'export LD_LIBRARY_PATH="$${HERE}/lib:$$LD_LIBRARY_PATH"' >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE)"
	@echo 'if [ -n "$$WAYLAND_DISPLAY" ] && command -v qt6-wayland >/dev/null 2>&1; then' >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE)"
	@echo '    export QT_QPA_PLATFORM=wayland' >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE)"
	@echo 'elif [ -n "$$DISPLAY" ]; then' >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE)"
	@echo '    export QT_QPA_PLATFORM=xcb' >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE)"
	@echo 'fi' >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE)"
	@echo 'exec "$${HERE}/bin/$(PROJECT_BINARY)" "$$@"' >> "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE)"
	@chmod +x "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE)"
	@echo Creating Linux launcher: $(PROJECT_TITLE)
endif

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
		6.10*) EMSDK_VERSION="3.1.70" ;; \
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

web-build:
	@. ./emsdk/emsdk_env.sh && \
	emcmake \
	cmake -S . -B $(BUILD_DIR) \
	-DVERSION_TAG=$(VERSION_TAG) \
	-DPROJECT_TITLE=$(PROJECT_TITLE) \
	-DPROJECT_ORG=$(PROJECT_ORG) \
	-DPROJECT_DOMAIN=$(PROJECT_DOMAIN) \
	-DMODEL_VISION_DETECT=$(MODEL_VISION_DETECT) \
	-DMODEL_VISION_IDENTIFY=$(MODEL_VISION_IDENTIFY) \
	-DMODEL_HEARING_GGML=$(MODEL_HEARING_GGML) \
	-DCMAKE_BUILD_TYPE=$(BUILD_MODE) \
	-DQT_ROOT_DIR=$(QT_ROOT_DIR) \
	-DEMSCRIPTEN=ON \
	-DCMAKE_PREFIX_PATH=$(QT_ROOT_DIR_TARGET) \
	-DCMAKE_INSTALL_PREFIX=$(ABS_INSTALL_DIR) \
	-DQt6_DIR=$(QT_MODULE_PATH_TARGET) \
	-DBUILD_TIME=$(BUILD_TIME) \
	-DBUILD_NAME=$(BUILD_NAME) \
	-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_TOOLCHAIN) \
	-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$(QT_TOOLCHAIN_TARGET) \
	-DVCPKG_TARGET_TRIPLET=$(VCPKG_TRIPLET) && \
	cmake --build $(BUILD_DIR)

	mkdir -p $(ABS_INSTALL_DIR)
	cp -r $(ASSETS_DIR) $(ABS_INSTALL_DIR)
	cp -r $(MODELS_DIR) $(ABS_INSTALL_DIR)

	cp $(BUILD_DIR)/$(PROJECT_BINARY).html $(ABS_INSTALL_DIR)/index.html
	cp $(BUILD_DIR)/*.js $(ABS_INSTALL_DIR)
	cp $(BUILD_DIR)/*.wasm $(ABS_INSTALL_DIR)
	cp $(BUILD_DIR)/*.svg $(ABS_INSTALL_DIR)
	cp $(ASSETS_DIR)/logo.png $(ABS_INSTALL_DIR)/logo.png
	cp $(ASSETS_DIR)/favicon.ico $(ABS_INSTALL_DIR)/favicon.ico

	sed -i 's#<title>$(PROJECT_BINARY)</title>#<title>$(PROJECT_TITLE) | Kidev.org<\/title><link rel="icon" href="favicon.ico" type="image/x-icon">#g' $(ABS_INSTALL_DIR)/index.html
	sed -i "s#<strong>Qt for WebAssembly: $(PROJECT_BINARY)</strong>#<h1 style='color:\#ffffff;'><strong>$(PROJECT_TITLE)</strong></h1><span style='color:\#ffffff;'>Written by Kidev using Qt</span><br><br><img src='qtlogo.svg' width='160' height='100' style='display:block'>#g" $(ABS_INSTALL_DIR)/index.html
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

ensure-config:
	@set -e; \
	os="$$(uname -s 2>/dev/null)"; \
	case "$$os" in \
	  Darwin*) conf_dir="$$HOME/Library/Preferences/$(PROJECT_ORG)";; \
	  MINGW*|MSYS*|CYGWIN*) \
	    appdata="$${APPDATA:-}"; \
	    if [ -z "$$appdata" ]; then conf_dir="$$HOME/AppData/Roaming/$(PROJECT_ORG)"; \
	    else appdata="$${appdata//\\//}"; conf_dir="$$appdata/$(PROJECT_ORG)"; fi;; \
	  *) conf_dir="$$HOME/.config/$(PROJECT_ORG)";; \
	esac; \
	conf_file="$$conf_dir/$(PROJECT_TITLE).conf"; \
	if [ -f "$$conf_file" ]; then \
	  echo "Found existing $(PROJECT_TITLE) config file: $$conf_file"; \
	else \
	  mkdir -p "$$conf_dir"; \
	  if [ $(WEB) == 1 ]; then \
	    echo "No $(PROJECT_TITLE) config file: creating web default config at $$conf_file"; \
	    cp "cfg/web.conf" "$$conf_file"; \
	  else \
	    echo "No $(PROJECT_TITLE) config file: creating desktop default config at $$conf_file"; \
	    cp "cfg/desktop.conf" "$$conf_file"; \
	  fi \
	fi;

clean:
	rm -rf $(BUILD_DIR) $(ABS_INSTALL_DIR) emsdk installer/packages installer/config/config.xml installer/config/meta/package.xml
	rm -rf $(INSTALLER_NAME) $(TARGET_PACKAGE) $(REPO_NAME) CMakeLists.txt.user

clean-vcpkg:
	rm -rf $(VCPKG_ROOT) $(VCPKG_ROOT)_installed

clean-models:
	rm -rf $(MODELS_DIR)
	rm $(QRC_OUTPUT)

clean-env:
	rm -rf .env

clean-all: clean clean-vcpkg clean-models

.PHONY: all debug web web-debug setup-vcpkg download-models create-resources-file handle-resources repo upload-repo upload-web create-installer installer setup-installer desktop-build desktop shortcut emsdk web-build run-web ensure-config clean clean-vcpkg clean-models clean-env clean-all
.IGNORE: clean clean-vcpkg clean-models clean-env clean-all
