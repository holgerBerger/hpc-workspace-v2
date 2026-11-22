# Container test setup

## Docker

example how to run coverage analysis:

```
sudo docker build ubuntu-24.04 -t hpcw
sudo docker run --cap-add=DAC_READ_SEARCH hpcw lcov
mkdir coverage
sudo docker run --cap-add=DAC_READ_SEARCH -v $PWD/coverage:/ws/coverage_report hpcw lcov
sudo chown -R $USERNAME coverage
```

example how to run bats tests:

```
sudo docker run --cap-add=DAC_READ_SEARCH hpcw bats 
```

## Podman

example to to run unit tests and bats tests:

```
podman run --cap-add=DAC_READ_SEARCH -it --entrypoint /bin/bash hpcw
```
