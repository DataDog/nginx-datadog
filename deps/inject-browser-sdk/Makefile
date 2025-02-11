CIRCLE_CFG ?= .circleci/continue_config.yml
COMMIT_SHA ?= $(shell git rev-parse --short HEAD)
DOCKER_REPO ?= public.ecr.aws/b1o7r7e0/inject-browser-sdk

.PHONY: lint
lint:
	./scripts/codestyle.sh lint

.PHONY: format
format:
	./scripts/codestyle.sh format

.PHONY: doc
doc:
	./scripts/generate-documentation.sh

.PHONY: circleci-config
circleci-config:
	@echo "Compiling circleci config"
	circleci config pack .circleci/src > $(CIRCLE_CFG)
	@echo "Validating circleci config"
	circleci config validate $(CIRCLE_CFG)

.PHONY: push-ci-images
push-ci-images:
	docker build --progress=plain --build-arg ARCH=x86_64 --platform linux/amd64 -t $(DOCKER_REPO):builder-$(COMMIT_SHA)-amd64 .
	docker push $(DOCKER_REPO):builder-$(COMMIT_SHA)-amd64
	docker build --progress=plain --build-arg ARCH=aarch64 --platform linux/arm64 -t $(DOCKER_REPO):builder-$(COMMIT_SHA)-arm64 .
	docker push $(DOCKER_REPO):builder-$(COMMIT_SHA)-arm64
	docker buildx imagetools create -t $(DOCKER_REPO):builder-$(COMMIT_SHA) \
			$(DOCKER_REPO):builder-$(COMMIT_SHA)-amd64 \
			$(DOCKER_REPO):builder-$(COMMIT_SHA)-arm64
