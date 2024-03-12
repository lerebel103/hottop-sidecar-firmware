
lint:
	cd ci && npm i && npm run lint
lint-fix:
	cd ci && npm i && npm run lint-fix


configure:
	rm -fr build || true
	cmake -DBUILD_STAGE=${STAGE} -DCMAKE_BUILD_TYPE=Release -S . -B build -G Ninja

build: configure
	cd build && ninja deploy

