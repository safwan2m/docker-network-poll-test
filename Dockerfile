# Use an official minimal Linux image
FROM debian:latest

# Install necessary packages
RUN apt-get update && apt-get install -y \
    gcc \
    make \
    libc-dev \
    iproute2 \
    libcap2-bin \
    htop \
    && rm -rf /var/lib/apt/lists/*

# Set the working directory
WORKDIR /app

# Copy source code to the container
COPY net_poll.c .

# Compile the C program
RUN gcc -o net_poll net_poll.c -pthread

# Ensure the binary has the necessary capabilities
RUN setcap cap_net_raw,cap_net_admin+ep /app/net_poll

# Command to run the application
CMD ["/app/net_poll"]
