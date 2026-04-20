IMAGE = sistema-busqueda
PORT = 8080
CONTAINER = sb-server

.PHONY: build run stop logs test clean docker-build docker-run

build:
	mkdir -p build && cd build && cmake .. && make

docker-build:
	docker build -t $(IMAGE) .

docker-run:
	docker run -d --name $(CONTAINER) -p $(PORT):$(PORT) $(IMAGE)

run: docker-build docker-run

stop:
	docker stop $(CONTAINER) && docker rm $(CONTAINER)

logs:
	docker logs -f $(CONTAINER)

exec:
	docker exec -it $(CONTAINER) bash

test:
	./test.sh

clean:
	rm -rf build
	docker rmi $(IMAGE) 2>/dev/null || true
