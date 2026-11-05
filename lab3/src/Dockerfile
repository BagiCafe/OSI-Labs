FROM ubuntu:24.04

RUN apt-get update && apt-get install -y gcc strace procps && rm -rf /var/lib/apt/lists/*

WORKDIR /app

CMD ["/bin/bash"]
