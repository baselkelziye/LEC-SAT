YOSYS ?= yosys

.PHONY: all export check
all: export check

build:
	mkdir -p build

export: | build
	$(YOSYS) -Q -T -l build/export.log -s export.ys
	$(YOSYS) -Q -T -p 'help chformal' > build/chformal-help.txt
	@if grep -q -- '-lower' build/chformal-help.txt; then \
		sed '/^aigmap/i chformal -lower' export_assert.ys > build/export_assert.ys; \
	else \
		cp export_assert.ys build/export_assert.ys; \
	fi
	$(YOSYS) -Q -T -l build/export_assert.log -s build/export_assert.ys

check: | build
	$(YOSYS) -Q -T -l build/check.log -s check.ys
