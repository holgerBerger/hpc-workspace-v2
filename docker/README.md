# Container test setup

example how to run coverage analysis:

```
sudo docker build ubuntu-24.04 -t hpcw
sudo docker run --cap-add=ALL hpcw lcov
mkdir coverage
sudo docker run --cap-add=ALL -v $PWD/coverage:/ws/coverage_report hpcw lcov
sudo chown -R $USERNAME coverage
```
