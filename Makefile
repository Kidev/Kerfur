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

OS_NAME := $(shell uname -s)
PROJECT_BINARY ?= kerfur
PROJECT_TITLE ?= Kerfur
VERSION_TAG ?= "0.0.0"
ASSETS_DIR ?= assets
BUILD_DIR ?= build
INSTALL_DIR ?= install
WASM_ARCH ?= wasm_multithread
BUILD_MODE ?= Release
BUILD_MODE_WEB ?= MinSizeRel
BROTLI_EXTENSIONS ?= js css html wasm png ico wav mp3
QT_IFW_VERSION ?= 4.9
ABS_INSTALL_DIR := $(abspath $(INSTALL_DIR))
QT_ROOT_DIR_TARGET ?= $(abspath $(QT_ROOT_DIR)/../$(WASM_ARCH))
QT_VERSION := $(notdir $(abspath $(QT_ROOT_DIR)/..))
QT_NAME := Qt$(shell echo $(QT_VERSION) | cut -c1)
QT_HOST_CMAKE_DIR := $(QT_ROOT_DIR)/lib/cmake
QT_MODULE_PATH := $(QT_ROOT_DIR_TARGET)/lib/cmake/$(QT_NAME)
QT_TOOLCHAIN := $(QT_ROOT_DIR_TARGET)/lib/cmake/$(QT_NAME)/qt.toolchain.cmake
QT_TOOLS_DIR := $(QT_ROOT_DIR)/../../Tools
SHELL = /bin/bash
INSTALLER_BIN_DIR ?= $(QT_TOOLS_DIR)/QtInstallerFramework/$(QT_IFW_VERSION)/bin
REPO_NAME ?= org_kidev_$(PROJECT_BINARY)_$(OS_NAME)
TARGET_PACKAGE ?= org.kidev.$(PROJECT_BINARY).$(OS_NAME)
INSTALLER_NAME ?= InstallKerfur-$(OS_NAME)
CDN_UPLOAD_SOCKET ?=
CDN_UPLOAD_USERNAME ?=
CDN_UPLOAD_PASSWORD ?=
WASM_UPLOAD_SOCKET ?=
WASM_UPLOAD_USERNAME ?=
WASM_UPLOAD_PASSWORD ?=

all: clean desktop

repo:
	mkdir -p "installer/packages/$(TARGET_PACKAGE)/data"
	cp -Rf "$(ABS_INSTALL_DIR)" "installer/packages/$(TARGET_PACKAGE)/data/"
	mv -f "installer/packages/$(TARGET_PACKAGE)/data/$(INSTALL_DIR)" "installer/packages/$(TARGET_PACKAGE)/data/$(PROJECT_TITLE)"
	rm -rf $(REPO_NAME)
	$(INSTALLER_BIN_DIR)/repogen -p installer/packages -i $(TARGET_PACKAGE) $(REPO_NAME)

upload-repo:
	@if [ -n "$(CDN_UPLOAD_USERNAME)" ] && [ -n "$(CDN_UPLOAD_PASSWORD)" ] && [ -n "$(CDN_UPLOAD_SOCKET)" ]; then \
		lftp -u "$(CDN_UPLOAD_USERNAME),$(CDN_UPLOAD_PASSWORD)" "$(CDN_UPLOAD_SOCKET)" -e "\
		set ssl:verify-certificate no; \
		mirror -R --only-newer --verbose $(REPO_NAME) $(OS_NAME); \
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

installer: setup-installer repo upload-repo
	rm -rf $(INSTALLER_NAME)
	$(INSTALLER_BIN_DIR)/binarycreator -p installer/packages -c installer/config/config.xml -e $(TARGET_PACKAGE) $(INSTALLER_NAME)

setup-installer:
	@echo "Setting up installer configuration..."

	mkdir -p installer/config/meta
	mkdir -p installer/packages/$(TARGET_PACKAGE)/meta

	@echo '<?xml version="1.0"?>' > installer/config/config.xml
	@echo '<Installer>' >> installer/config/config.xml
	@echo '    <Name>$(PROJECT_TITLE) Installer for $(OS_NAME)</Name>' >> installer/config/config.xml
	@echo '    <Version>$(VERSION_TAG)</Version>' >> installer/config/config.xml
	@echo '    <Title>$(PROJECT_TITLE) Installer</Title>' >> installer/config/config.xml
	@echo '    <Publisher>Kidev.org</Publisher>' >> installer/config/config.xml
	@echo '    <ProductUrl>https://www.kidev.org</ProductUrl>' >> installer/config/config.xml
	@echo '    <InstallerWindowIcon>icon</InstallerWindowIcon>' >> installer/config/config.xml
	@echo '    <InstallerApplicationIcon>icon</InstallerApplicationIcon>' >> installer/config/config.xml
	@echo '    <Banner>banner.gif</Banner>' >> installer/config/config.xml
	@echo '    <RunProgram>@TargetDir@/$(PROJECT_TITLE)/bin/$(PROJECT_BINARY)</RunProgram>' >> installer/config/config.xml
	@echo '    <RunProgramDescription>Run $(PROJECT_TITLE)</RunProgramDescription>' >> installer/config/config.xml
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
	@echo '    <WizardShowPageList>true</WizardShowPageList>' >> installer/config/config.xml
	@echo '    <InstallActionColumnVisible>true</InstallActionColumnVisible>' >> installer/config/config.xml
	@echo '    <RemoteRepositories>' >> installer/config/config.xml
	@echo '        <Repository>' >> installer/config/config.xml
	@echo '            <DisplayName>Kidev.org CDN</DisplayName>' >> installer/config/config.xml
	@echo '            <Url>https://cdn.kidev.org/$(OS_NAME)</Url>' >> installer/config/config.xml
	@echo '            <Enabled>1</Enabled>' >> installer/config/config.xml
	@echo '        </Repository>' >> installer/config/config.xml
	@echo '    </RemoteRepositories>' >> installer/config/config.xml
	@echo '</Installer>' >> installer/config/config.xml

	@echo '<?xml version="1.0"?>' > installer/config/meta/package.xml
	@echo '<Package>' >> installer/config/meta/package.xml
	@echo '    <DisplayName>$(PROJECT_TITLE)</DisplayName>' >> installer/config/meta/package.xml
	@echo '    <Description>$(PROJECT_TITLE) by Kidev.org</Description>' >> installer/config/meta/package.xml
	@echo '    <Version>$(VERSION_TAG)</Version>' >> installer/config/meta/package.xml
	@echo '    <ReleaseDate>'$$(date +%Y-%m-%d)'</ReleaseDate>' >> installer/config/meta/package.xml
	@echo '    <Name>$(TARGET_PACKAGE)</Name>' >> installer/config/meta/package.xml
	@echo '    <Licenses>' >> installer/config/meta/package.xml
	@echo '        <License name="$(PROJECT_TITLE)'"'"'s Software License Agreement" file="license.txt" />' >> installer/config/meta/package.xml
	@echo '    </Licenses>' >> installer/config/meta/package.xml
	@echo '    <Script>installer_script.qs</Script>' >> installer/config/meta/package.xml
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

web: clean emsdk
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

clean:
	rm -rf $(BUILD_DIR) $(ABS_INSTALL_DIR) emsdk installer/packages installer/config/config.xml installer/config/meta/package.xml
	rm -rf $(INSTALLER_NAME) $(TARGET_PACKAGE) $(REPO_NAME) CMakeLists.txt.user 

.PHONY: all repo upload-repo upload-web installer setup-installer desktop emsdk web run-web clean
.IGNORE: clean
