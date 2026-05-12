THREADS_BUILD 	?= 1
THREADS 		?= 1

SUFFIX ?=

define tltest_path_with_suffix
$(if $(strip $(SUFFIX)),$1_$(SUFFIX),$1)
endef

TLTEST_BUILD_DIR := $(call tltest_path_with_suffix,./main/build)
TLTEST_VERILATED_DIR := $(call tltest_path_with_suffix,./verilated)
TLTEST_RUN_DIR := $(call tltest_path_with_suffix,./run)
TLTEST_VERILATED_PATH := $(abspath $(TLTEST_VERILATED_DIR))
TLTEST_VERILATED_CMAKE_ARG := -DVERILATED_PATH="$(TLTEST_VERILATED_PATH)"

CMAKE_CXX_COMPILER :=
ifneq ($(origin CXX_COMPILER), undefined)
	CMAKE_CXX_COMPILER := -DCMAKE_CXX_COMPILER=$(CXX_COMPILER)
endif

TLTEST_COMMON_ARGS := -DDUT_PATH="$(CURDIR)/dut/CoupledL2" \
	-DDUT_BUILD_PATH="$(CURDIR)/dut/CoupledL2/build" \
	-DCHISELDB_PATH="$(CURDIR)/dut/CoupledL2/build" \
	-DTLTEST_MEMORY=0

TLTEST_COMMON_ARGS_OPENLLC := -DDUT_PATH="$(CURDIR)/dut/OpenLLC" \
	-DDUT_BUILD_PATH="$(CURDIR)/dut/OpenLLC/build" \
	-DCHISELDB_PATH="$(CURDIR)/dut/OpenLLC/build" \
	-DTLTEST_MEMORY=0


init:
	git submodule update --init --recursive
	$(MAKE) -C ./dut/CoupledL2 init
	$(MAKE) -C ./dut/OpenLLC init

FORCE:


tltest-prepare-all:
	cmake ./main -B $(TLTEST_BUILD_DIR) -DBUILD_DPI=ON -DBUILD_V3=ON $(CMAKE_CXX_COMPILER) $(TLTEST_VERILATED_CMAKE_ARG)

tltest-prepare-dpi:
	cmake ./main -B $(TLTEST_BUILD_DIR) -DBUILD_DPI=ON -DBUILD_V3=OFF $(CMAKE_CXX_COMPILER) $(TLTEST_VERILATED_CMAKE_ARG)

tltest-prepare-v3:
	cmake ./main -B $(TLTEST_BUILD_DIR) -DBUILD_V3=ON -DBUILD_DPI=OFF $(CMAKE_CXX_COMPILER) $(TLTEST_VERILATED_CMAKE_ARG)


tltest-prepare-all-coupledL2:
	cmake ./main -B $(TLTEST_BUILD_DIR) -DBUILD_DPI=ON -DBUILD_V3=ON $(CMAKE_CXX_COMPILER) $(TLTEST_COMMON_ARGS) $(TLTEST_VERILATED_CMAKE_ARG)

tltest-prepare-dpi-coupledL2:
	cmake ./main -B $(TLTEST_BUILD_DIR) -DBUILD_DPI=ON -DBUILD_V3=OFF $(CMAKE_CXX_COMPILER) $(TLTEST_COMMON_ARGS) $(TLTEST_VERILATED_CMAKE_ARG)

tltest-prepare-v3-coupledL2:
	cmake ./main -B $(TLTEST_BUILD_DIR) -DBUILD_V3=ON -DBUILD_DPI=OFF $(CMAKE_CXX_COMPILER) $(TLTEST_COMMON_ARGS) $(TLTEST_VERILATED_CMAKE_ARG)


tltest-prepare-all-openLLC:
	cmake ./main -B $(TLTEST_BUILD_DIR) -DBUILD_DPI=ON -DBUILD_V3=ON $(CMAKE_CXX_COMPILER) $(TLTEST_COMMON_ARGS_OPENLLC) $(TLTEST_VERILATED_CMAKE_ARG)

tltest-prepare-dpi-openLLC:
	cmake ./main -B $(TLTEST_BUILD_DIR) -DBUILD_DPI=ON -DBUILD_V3=OFF $(CMAKE_CXX_COMPILER) $(TLTEST_COMMON_ARGS_OPENLLC) $(TLTEST_VERILATED_CMAKE_ARG)

tltest-prepare-v3-openLLC:
	cmake ./main -B $(TLTEST_BUILD_DIR) -DBUILD_V3=ON -DBUILD_DPI=OFF $(CMAKE_CXX_COMPILER) $(TLTEST_COMMON_ARGS_OPENLLC) $(TLTEST_VERILATED_CMAKE_ARG)


tltest-portgen:
	$(MAKE) -C $(TLTEST_BUILD_DIR) portgen -j$(THREADS_BUILD) -s --always-make

tltest-clean:
	$(MAKE) -C $(TLTEST_BUILD_DIR) clean -s

tltest-config-user:
	@test -d $(TLTEST_BUILD_DIR) || mkdir -p $(TLTEST_BUILD_DIR)
	@cat ./configs/user.tltest.ini
	@echo ""
	@cat ./configs/user.tltest.ini > $(TLTEST_BUILD_DIR)/tltest.ini
	@echo "" >> $(TLTEST_BUILD_DIR)/tltest.ini

tltest-config-coupledL2-test-l2l3: tltest-config-user
	@cat ./configs/coupledL2-test-l2l3.tltest.ini
	@echo ""
	@cat ./configs/coupledL2-test-l2l3.tltest.ini >> $(TLTEST_BUILD_DIR)/tltest.ini
	@echo "tltest-config-postbuild: tltest-config-coupledL2-test-l2l3" > $(TLTEST_BUILD_DIR)/Makefile.config

tltest-config-coupledL2-test-l2l3l2: tltest-config-user
	@cat ./configs/coupledL2-test-l2l3l2.tltest.ini
	@echo ""
	@cat ./configs/coupledL2-test-l2l3l2.tltest.ini >> $(TLTEST_BUILD_DIR)/tltest.ini
	@echo "tltest-config-postbuild: tltest-config-coupledL2-test-l2l3l2" > $(TLTEST_BUILD_DIR)/Makefile.config

tltest-config-coupledL2-test-matrix-trace: tltest-config-user
	@cat ./configs/coupledL2-test-matrix-trace.tltest.ini
	@echo ""
	@cat ./configs/coupledL2-test-matrix-trace.tltest.ini >> $(TLTEST_BUILD_DIR)/tltest.ini
	@echo "tltest-config-postbuild: tltest-config-coupledL2-test-matrix-trace" > $(TLTEST_BUILD_DIR)/Makefile.config

tltest-config-openLLC-test-l2l3: tltest-config-user
	@cat ./configs/openLLC-test-l2l3.tltest.ini
	@echo ""
	@cat ./configs/openLLC-test-l2l3.tltest.ini >> $(TLTEST_BUILD_DIR)/tltest.ini
	@echo "tltest-config-postbuild: tltest-config-openLLC-test-l2l3" > $(TLTEST_BUILD_DIR)/Makefile.config

tltest-config-openLLC-test-matrix-trace: tltest-config-user
	@cat ./configs/openLLC-test-matrix-trace.tltest.ini
	@echo ""
	@cat ./configs/openLLC-test-matrix-trace.tltest.ini >> $(TLTEST_BUILD_DIR)/tltest.ini
	@echo "tltest-config-postbuild: tltest-config-openLLC-test-matrix-trace" > $(TLTEST_BUILD_DIR)/Makefile.config

tltest-config-openLLC-test-l2l3l2: tltest-config-user
	@cat ./configs/openLLC-test-l2l3l2.tltest.ini
	@echo ""
	@cat ./configs/openLLC-test-l2l3l2.tltest.ini >> $(TLTEST_BUILD_DIR)/tltest.ini
	@echo "tltest-config-postbuild: tltest-config-openLLC-test-l2l3l2" > $(TLTEST_BUILD_DIR)/Makefile.config

tltest-config-postbuild:

tltest-build:
	$(MAKE) -C $(TLTEST_BUILD_DIR) -j$(THREADS_BUILD) -s
	$(MAKE) -C $(TLTEST_BUILD_DIR) portgen -j$(THREADS_BUILD) -s --always-make

tltest-build-all-coupledL2: tltest-prepare-all-coupledL2 tltest-build

tltest-build-dpi-coupledL2: tltest-prepare-dpi-coupledL2 tltest-build

tltest-build-v3-coupledL2: tltest-prepare-v3-coupledL2 tltest-build

tltest-build-all-openLLC: tltest-prepare-all-openLLC tltest-build

tltest-build-dpi-openLLC: tltest-prepare-dpi-openLLC tltest-build

tltest-build-v3-openLLC: tltest-prepare-v3-openLLC tltest-build


coupledL2-compile:
	$(MAKE) -C ./dut/CoupledL2 compile

coupledL2-verilog-test-top-l2l3:
	$(MAKE) -C ./dut/CoupledL2 test-top-l2l3

coupledL2-verilog-test-top-l2l3l2:
	$(MAKE) -C ./dut/CoupledL2 test-top-l2l3l2

coupledL2-verilog-test-top-matrix:
	$(MAKE) -C ./dut/CoupledL2 test-top-matrix

coupledL2-verilog-clean:
	$(MAKE) -C ./dut/CoupledL2 clean

openLLC-compile:
	$(MAKE) -C ./dut/OpenLLC compile

openLLC-verilog-test-top-l2l3:
	$(MAKE) -C ./dut/OpenLLC test-top-l2l3

openLLC-verilog-test-top-matrix:
	$(MAKE) -C ./dut/OpenLLC test-top-matrix

openLLC-verilog-test-top-l2l3l2:
	$(MAKE) -C ./dut/OpenLLC test-top-l2l3l2

openLLC-verilog-clean:
	$(MAKE) -C ./dut/OpenLLC clean


VERILATOR := verilator
COUPLEDL2_ALL_V_FILES := $(shell find ./dut/CoupledL2/build -type f -name '*.*v' | sort)
COUPLEDL2_VERILATOR_INC_DIRS := $(shell find ./dut/CoupledL2/build -type d | sort)
COUPLEDL2_VERILATOR_INC := $(addprefix -I,$(COUPLEDL2_VERILATOR_INC_DIRS))
VERILATOR_COMMON_ARGS_COUPLEDL2 := $(COUPLEDL2_ALL_V_FILES) \
		$(COUPLEDL2_VERILATOR_INC) \
		--Mdir $(TLTEST_VERILATED_DIR) \
		-O3 \
		--trace-fst \
		--top TestTop \
		--build-jobs $(THREADS_BUILD) --verilate-jobs $(THREADS_BUILD) \
		-DSIM_TOP_MODULE_NAME=TestTop \
		-Wno-fatal
VERILATOR_COMMON_ARGS_OPENLLC := ./dut/OpenLLC/build/*.*v \
		--Mdir $(TLTEST_VERILATED_DIR) \
		-O3 \
		--trace-fst \
		--top TestTop \
		--build-jobs $(THREADS_BUILD) --verilate-jobs $(THREADS_BUILD) \
		-DSIM_TOP_MODULE_NAME=TestTop \
		-Wno-fatal

coupledL2-verilate-cc:
	rm -rf $(TLTEST_VERILATED_DIR)
	mkdir -p $(TLTEST_VERILATED_DIR)
	$(VERILATOR) $(VERILATOR_COMMON_ARGS_COUPLEDL2) --cc

coupledL2-verilate-build:
	$(VERILATOR) $(VERILATOR_COMMON_ARGS_COUPLEDL2) --cc --exe --build -o tltest_v3lt \
		libtltest_v3lt.a \
		-LDFLAGS "-lsqlite3 -ldl" -CFLAGS "-rdynamic"

coupledL2-verilate:
	rm -rf $(TLTEST_VERILATED_DIR)
	mkdir -p $(TLTEST_VERILATED_DIR)
	verilator --trace-fst --cc --build --lib-create vltdut --Mdir $(TLTEST_VERILATED_DIR) $(COUPLEDL2_ALL_V_FILES) $(COUPLEDL2_VERILATOR_INC) -Wno-fatal \
		--top TestTop --build-jobs $(THREADS_BUILD) --verilate-jobs $(THREADS_BUILD) --threads $(THREADS) -DSIM_TOP_MODULE_NAME=TestTop

coupledL2-verilate-clean:
	rm -rf $(TLTEST_VERILATED_DIR)

openLLC-verilate-cc:
	rm -rf $(TLTEST_VERILATED_DIR)
	mkdir -p $(TLTEST_VERILATED_DIR)
	$(VERILATOR) $(VERILATOR_COMMON_ARGS_OPENLLC) --cc

openLLC-verilate-build:
	$(VERILATOR) $(VERILATOR_COMMON_ARGS_OPENLLC) --cc --exe --build -o tltest_v3lt \
		lbtltest_v3lt.a \
		-LDFLAGS "-lsqlite3 -ldl" -CFLAGS "-rdynamic"

openLLC-verilate:
	rm -rf $(TLTEST_VERILATED_DIR)
	mkdir -p $(TLTEST_VERILATED_DIR)
	verilator --trace-fst --cc --build --lib-create vltdut --Mdir $(TLTEST_VERILATED_DIR) ./dut/OpenLLC/build/*.*v -Wno-fatal \
		--top TestTop --build-jobs $(THREADS_BUILD) --verilate-jobs $(THREADS_BUILD) -DSIM_TOP_MODULE_NAME=TestTop

openLLC-verilate-clean:
	rm -rf $(TLTEST_VERILATED_DIR)


coupledL2-test-l2l3: coupledL2-compile coupledL2-verilog-test-top-l2l3 coupledL2-verilate \
					 tltest-config-coupledL2-test-l2l3 tltest-build-all-coupledL2

coupledL2-test-l2l3-v3: coupledL2-compile coupledL2-verilog-test-top-l2l3 coupledL2-verilate \
					    tltest-config-coupledL2-test-l2l3 tltest-build-v3-coupledL2

coupledL2-test-l2l3-dpi: coupledL2-compile coupledL2-verilog-test-top-l2l3 coupledL2-verilate \
						 tltest-config-coupledL2-test-l2l3 tltest-build-dpi-coupledL2


coupledL2-test-l2l3l2: coupledL2-compile coupledL2-verilog-test-top-l2l3l2 coupledL2-verilate \
					   tltest-config-coupledL2-test-l2l3l2 tltest-build-all-coupledL2

coupledL2-test-l2l3l2-v3: coupledL2-compile coupledL2-verilog-test-top-l2l3l2 coupledL2-verilate \
						  tltest-config-coupledL2-test-l2l3l2 tltest-build-v3-coupledL2

coupledL2-test-l2l3l2-dpi: coupledL2-compile coupledL2-verilog-test-top-l2l3l2 coupledL2-verilate \
						   tltest-config-coupledL2-test-l2l3l2 tltest-build-dpi-coupledL2

coupledL2-test-matrix-trace: coupledL2-compile coupledL2-verilog-test-top-matrix coupledL2-verilate \
					  tltest-config-coupledL2-test-matrix-trace tltest-build-all-coupledL2


openLLC-test-l2l3: openLLC-compile openLLC-verilog-test-top-l2l3 openLLC-verilate \
				   tltest-config-openLLC-test-l2l3 tltest-build-all-openLLC

openLLC-test-matrix-trace: openLLC-compile openLLC-verilog-test-top-matrix openLLC-verilate \
		   tltest-config-openLLC-test-matrix-trace tltest-build-all-openLLC

openLLC-test-l2l3-v3: openLLC-compile openLLC-verilog-test-top-l2l3 openLLC-verilate \
				      tltest-config-openLLC-test-l2l3 tltest-build-v3-openLLC

openLLC-test-l2l3-dpi: openLLC-compile openLLC-verilog-test-top-l2l3 openLLC-verilate \
				       tltest-config-openLLC-test-l2l3 tltest-build-dpi-openLLC


openLLC-test-l2l3l2: openLLC-compile openLLC-verilog-test-top-l2l3l2 openLLC-verilate \
				     tltest-config-openLLC-test-l2l3l2 tltest-build-all-openLLC

openLLC-test-l2l3l2-v3: openLLC-compile openLLC-verilog-test-top-l2l3l2 openLLC-verilate \
				        tltest-config-openLLC-test-l2l3l2 tltest-build-v3-openLLC

openLLC-test-l2l3l2-dpi: openLLC-compile openLLC-verilog-test-top-l2l3l2 openLLC-verilate \
				         tltest-config-openLLC-test-l2l3l2 tltest-build-dpi-openLLC


-include $(TLTEST_BUILD_DIR)/Makefile.config

run: FORCE tltest-config-postbuild
	@rm -rf $(TLTEST_RUN_DIR)
	@mkdir -p $(TLTEST_RUN_DIR)
	@cp $(TLTEST_BUILD_DIR)/tltest_v3lt $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest_portgen.so $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest.ini $(TLTEST_RUN_DIR)/
	@bash ./scripts/run_v3lt.sh $(TLTEST_RUN_DIR)

run_coupledL2-test-l2l3: FORCE tltest-config-coupledL2-test-l2l3
	@rm -rf $(TLTEST_RUN_DIR)
	@mkdir -p $(TLTEST_RUN_DIR)
	@cp $(TLTEST_BUILD_DIR)/tltest_v3lt $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest_portgen.so $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest.ini $(TLTEST_RUN_DIR)/
	@bash ./scripts/run_v3lt.sh $(TLTEST_RUN_DIR)

run_coupledL2-test-l2l3l2: FORCE tltest-config-coupledL2-test-l2l3l2
	@rm -rf $(TLTEST_RUN_DIR)
	@mkdir -p $(TLTEST_RUN_DIR)
	@cp $(TLTEST_BUILD_DIR)/tltest_v3lt $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest_portgen.so $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest.ini $(TLTEST_RUN_DIR)/
	@bash ./scripts/run_v3lt.sh $(TLTEST_RUN_DIR)

run_coupledL2-test-matrix-trace: FORCE tltest-config-coupledL2-test-matrix-trace
	@rm -rf $(TLTEST_RUN_DIR)
	@mkdir -p $(TLTEST_RUN_DIR)
	@cp $(TLTEST_BUILD_DIR)/tltest_v3lt $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest_portgen.so $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest.ini $(TLTEST_RUN_DIR)/
	@bash ./scripts/run_v3lt.sh $(TLTEST_RUN_DIR)

run_openLLC-test-l2l3: FORCE tltest-config-openLLC-test-l2l3
	@rm -rf $(TLTEST_RUN_DIR)
	@mkdir -p $(TLTEST_RUN_DIR)
	@cp $(TLTEST_BUILD_DIR)/tltest_v3lt $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest_portgen.so $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest.ini $(TLTEST_RUN_DIR)/
	@bash ./scripts/run_v3lt.sh $(TLTEST_RUN_DIR)

run_openLLC-test-matrix-trace: FORCE tltest-config-openLLC-test-matrix-trace
	@rm -rf $(TLTEST_RUN_DIR)
	@mkdir -p $(TLTEST_RUN_DIR)
	@cp $(TLTEST_BUILD_DIR)/tltest_v3lt $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest_portgen.so $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest.ini $(TLTEST_RUN_DIR)/
	@bash ./scripts/run_v3lt.sh $(TLTEST_RUN_DIR)

run_openLLC-test-l2l3l2: FORCE tltest-config-openLLC-test-l2l3l2
	@rm -rf $(TLTEST_RUN_DIR)
	@mkdir -p $(TLTEST_RUN_DIR)
	@cp $(TLTEST_BUILD_DIR)/tltest_v3lt $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest_portgen.so $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest.ini $(TLTEST_RUN_DIR)/
	@bash ./scripts/run_v3lt.sh $(TLTEST_RUN_DIR)


run-with-portgen: FORCE tltest-config-postbuild tltest-portgen
	@rm -rf $(TLTEST_RUN_DIR)
	@mkdir -p $(TLTEST_RUN_DIR)
	@cp $(TLTEST_BUILD_DIR)/tltest_v3lt $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest_portgen.so $(TLTEST_RUN_DIR)/
	@cp $(TLTEST_BUILD_DIR)/tltest.ini $(TLTEST_RUN_DIR)/
	@bash ./scripts/run_v3lt.sh $(TLTEST_RUN_DIR)


clean: coupledL2-verilate-clean coupledL2-verilog-clean openLLC-verilate-clean openLLC-verilog-clean
	rm -rf ./main/build ./main/build_*
	rm -rf ./verilated ./verilated_*
	rm -rf ./run ./run_*
	mkdir -p $(TLTEST_BUILD_DIR)
