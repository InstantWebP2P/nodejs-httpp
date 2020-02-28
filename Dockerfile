#
# node-httpp build Dockerfile
#
# https://github.com/InstantWebP2P/node-httpp
#

# Pull base image.
FROM ubuntu:16.04

# Install.
RUN \
 apt-get -y update && \
 apt-get install -y python g++ make

# Add files.

# Set environment variables.
ENV HOME /root

# Define working directory.
WORKDIR /data

# Define default command.
CMD ["sh"]
