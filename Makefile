THREADS_BUILD 	?= 1

CMAKE_CXX_COMPILER :=
ifneq ($(origin CXX_COMPILER), undefined)
	CMAKE_CXX_COMPILER := -DCMAKE_CXX_COMPILER=$(CXX_COMPILER)
endif


init:
	git submodule update --init --recursive
	$(MAKE) -C ./dut/CoupledL2 init

FORCE:


tltest-prepare-all:
	cmake ./main -B ./main/build -DBUILD_DPI=ON -DBUILD_V3=ON $(CMAKE_CXX_COMPILER)

tltest-prepare-dpi:
	cmake ./main -B ./main/build -DBUILD_DPI=ON -DBUILD_V3=OFF $(CMAKE_CXX_COMPILER)

tltest-prepare-v3:
	cmake ./main -B ./main/build -DBUILD_V3=ON -DBUILD_DPI=OFF $(CMAKE_CXX_COMPILER)

tltest-portgen:
	$(MAKE) -C ./main/build portgen -j$(THREADS_BUILD) -s --always-make

tltest-clean:
	$(MAKE) -C ./main/build clean -s

tltest-config-user:
	@test -d ./main/build || mkdir -p ./main/build
	@cat ./configs/user.tltest.ini
	@echo ""
	@cat ./configs/user.tltest.ini > ./main/build/tltest.ini
	@echo "" >> ./main/build/tltest.ini

tltest-config-coupledL2-test-l2l3: tltest-config-user
	@cat ./configs/coupledL2-test-l2l3.tltest.ini
	@echo ""
	@cat ./configs/coupledL2-test-l2l3.tltest.ini >> ./main/build/tltest.ini
	@echo "tltest-config-postbuild: tltest-config-coupledL2-test-l2l3" > ./main/build/Makefile.config

tltest-config-coupledL2-test-l2l3l2: tltest-config-user
	@cat ./configs/coupledL2-test-l2l3l2.tltest.ini
	@echo ""
	@cat ./configs/coupledL2-test-l2l3l2.tltest.ini >> ./main/build/tltest.ini
	@echo "tltest-config-postbuild: tltest-config-coupledL2-test-l2l3l2" > ./main/build/Makefile.config

tltest-config-postbuild:

tltest-build:
	$(MAKE) -C ./main/build -j$(THREADS_BUILD) -s
	$(MAKE) -C ./main/build portgen -j$(THREADS_BUILD) -s --always-make

tltest-build-all: tltest-prepare-all tltest-build

tltest-build-dpi: tltest-prepare-dpi tltest-build

tltest-build-v3: tltest-prepare-v3 tltest-build

tltest: tltest-prepare tltest-build


coupledL2-compile:
	$(MAKE) -C ./dut/CoupledL2 compile

coupledL2-verilog-test-top-l2l3:
	$(MAKE) -C ./dut/CoupledL2 test-top-l2l3

coupledL2-verilog-test-top-l2l3l2:
	$(MAKE) -C ./dut/CoupledL2 test-top-l2l3l2

coupledL2-verilog-clean:
	$(MAKE) -C ./dut/CoupledL2 clean


VERILATOR := verilator
VERILATOR_COMMON_ARGS := ./dut/CoupledL2/build/*.v \
		--Mdir ./verilated \
		-O3 \
		--trace-fst \
		--top TestTop \
		--build-jobs $(THREADS_BUILD) --verilate-jobs $(THREADS_BUILD) \
		-DSIM_TOP_MODULE_NAME=TestTop \
		-Wno-fatal

coupledL2-verilate-cc:
	rm -rf verilated
	mkdir verilated
	$(VERILATOR) $(VERILATOR_COMMON_ARGS) --cc

coupledL2-verilate-build:
	$(VERILATOR) $(VERILATOR_COMMON_ARGS) --cc --exe --build -o tltest_v3lt \
		libtltest_v3lt.a \
		-LDFLAGS "-lsqlite3 -ldl" -CFLAGS "-rdynamic"

coupledL2-verilate:
	rm -rf verilated
	mkdir verilated
	verilator --trace-fst --cc --build --lib-create vltdut --Mdir ./verilated ./dut/CoupledL2/build/*.v -Wno-fatal \
		--top TestTop --build-jobs $(THREADS_BUILD) --verilate-jobs $(THREADS_BUILD) -DSIM_TOP_MODULE_NAME=TestTop

coupledL2-verilate-clean:
	rm -rf verilated


coupledL2-test-l2l3: coupledL2-compile coupledL2-verilog-test-top-l2l3 coupledL2-verilate \
					 tltest-config-coupledL2-test-l2l3 tltest-build-all

coupledL2-test-l2l3-v3: coupledL2-compile coupledL2-verilog-test-top-l2l3 coupledL2-verilate \
					    tltest-config-coupledL2-test-l2l3 tltest-build-v3

coupledL2-test-l2l3-dpi: coupledL2-compile coupledL2-verilog-test-top-l2l3 coupledL2-verilate \
						 tltest-config-coupledL2-test-l2l3 tltest-build-dpi


coupledL2-test-l2l3l2: coupledL2-compile coupledL2-verilog-test-top-l2l3l2 coupledL2-verilate \
					   tltest-config-coupledL2-test-l2l3l2 tltest-build-all

coupledL2-test-l2l3l2-v3: coupledL2-compile coupledL2-verilog-test-top-l2l3l2 coupledL2-verilate \
						  tltest-config-coupledL2-test-l2l3l2 tltest-build-v3

coupledL2-test-l2l3l2-dpi: coupledL2-compile coupledL2-verilog-test-top-l2l3l2 coupledL2-verilate \
						   tltest-config-coupledL2-test-l2l3l2 tltest-build-dpi


-include ./main/build/Makefile.config

run: FORCE tltest-config-postbuild
	@rm -rf ./run
	@mkdir ./run
	@cp ./main/build/tltest_v3lt ./run/
	@cp ./main/build/tltest_portgen.so ./run/
	@cp ./main/build/tltest.ini ./run/
	@bash ./scripts/run_v3lt.sh

run-with-portgen: FORCE tltest-config-postbuild tltest-portgen
	@rm -rf ./run
	@mkdir ./run
	@cp ./main/build/tltest_v3lt ./run/
	@cp ./main/build/tltest_portgen.so ./run/
	@cp ./main/build/tltest.ini ./run/
	@bash ./scripts/run_v3lt.sh


clean: coupledL2-verilate-clean coupledL2-verilog-clean tltest-clean
