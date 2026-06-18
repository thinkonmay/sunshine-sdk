.PHONY: ci ci-all ci-test ci-build configure

ci:
	./ci/local.sh

ci-all:
	./ci/local.sh all

ci-test:
	./ci/local.sh test

ci-build:
	./ci/local.sh build

configure:
	./ci/local.sh configure
