YOSYS ?= yosys
N ?= 4

.PHONY: all export check
all: export check

build:
	mkdir -p build

export: | build
	@python3 -c 'assert int("$(N)") >= 2, "N must be at least 2"'
	sed 's/^chparam -set N 4 /chparam -set N $(N) /' export.ys > build/export.ys
	$(YOSYS) -Q -T -l build/export.log -s build/export.ys
	$(YOSYS) -Q -T -p 'help chformal' > build/chformal-help.txt
	sed 's/^chparam -set N 4 /chparam -set N $(N) /' export_assert.ys > build/export_assert_base.ys
	@if grep -q -- '-lower' build/chformal-help.txt; then \
		sed '/^aigmap/i chformal -lower' build/export_assert_base.ys > build/export_assert.ys; \
	else \
		cp build/export_assert_base.ys build/export_assert.ys; \
	fi
	$(YOSYS) -Q -T -l build/export_assert.log -s build/export_assert.ys

check: | build
	sed 's/^chparam -set N 4 /chparam -set N $(N) /' check.ys > build/check.ys
	$(YOSYS) -Q -T -l build/check.log -s build/check.ys

.PHONY: clean
clean:
	rm -rf build
