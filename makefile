.PHONY: build clean test coverage coverage-report

MAKE_FLAGS=-j $$(nproc)

# ============================================================================ #
#  BUILD
# ============================================================================ #
BUILD_TYPE = "Release"

EXAMPLES = "ON"
STATS = "OFF"

# COVERAGE=1 selects the debug build type (-O0 -g) plus --coverage, so the
# objects are instrumented and libgcov is linked in. The flags are appended to
# whatever CMake sets for the build type.
ifeq ($(COVERAGE),1)
  BUILD_TYPE = "Debug"
  COVERAGE_FLAGS = -D CMAKE_C_FLAGS=--coverage \
                   -D CMAKE_CXX_FLAGS=--coverage \
                   -D CMAKE_EXE_LINKER_FLAGS=--coverage
endif

build:
	@mkdir -p build/ && cd build/ && cmake -D CMAKE_BUILD_TYPE=$(BUILD_TYPE) \
                                         -D BUDDY_STATS=$(STATS) \
                                         -D BUDDY_EXAMPLES=$(EXAMPLES) \
                                         $(COVERAGE_FLAGS) ..
	cd build && $(MAKE)

# ============================================================================ #
#  CLEAN
# ============================================================================ #
clean:
	@rm -rf build

# ============================================================================ #
#  Examples
# ============================================================================ #
test:
	@examples/check.sh -b build

# ============================================================================ #
#  Coverage
# ============================================================================ #
COVERAGE_XML = coverage.xml

# Instrumented build, the example suites, then a Cobertura report of src/.
# The suites that currently fail on main still count: `-` lets the report be
# produced even though check.sh exits non-zero.
coverage:
	$(MAKE) clean
	$(MAKE) build COVERAGE=1
	-$(MAKE) test
	$(MAKE) coverage-report

# Only the library is measured: gcovr searches build/src for the .gcda files
# and reports on src/. Searching all of build/ would also pick up CMake's
# compiler-identification objects, which gcovr cannot resolve, and the
# examples, which are the harness rather than the product.
coverage-report:
	gcovr --root . --filter 'src/' --xml-pretty --output $(COVERAGE_XML) \
	      --print-summary build/src
